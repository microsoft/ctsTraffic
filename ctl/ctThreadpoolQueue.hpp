/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <variant>
#include <Windows.h>
#include <wil/resource.h>

namespace ctl
{
// forward-declare classes that can instantiate a ctThreadpoolQueueWaitableResult object
class ctThreadpoolQueue;

namespace details
{
    // forward-declare details classes that can instantiate a ThreadPoolWaitableResult object
    template <typename TReturn>
    class ctThreadpoolQueueWaitableWorkItemWithResults;
} // namespace details

class ctThreadpoolQueueWaitableResultInterface
{
public:
    virtual ~ctThreadpoolQueueWaitableResultInterface() = default;

    ctThreadpoolQueueWaitableResultInterface(ctThreadpoolQueueWaitableResultInterface&&) noexcept = default;
    ctThreadpoolQueueWaitableResultInterface& operator=(ctThreadpoolQueueWaitableResultInterface&&) noexcept = default;
    ctThreadpoolQueueWaitableResultInterface(const ctThreadpoolQueueWaitableResultInterface&) = delete;
    ctThreadpoolQueueWaitableResultInterface& operator=(const ctThreadpoolQueueWaitableResultInterface&) = delete;

private:
    // limit who can run() and abort()
    friend class ctThreadpoolQueue;

    virtual void run() noexcept = 0;
    virtual void abort() noexcept = 0;
};

template <typename TReturn>
class ctThreadpoolQueueWaitableResult final : public ctThreadpoolQueueWaitableResultInterface
{
public:
    // throws a wil exception on failure
    template <typename FunctorType>
    explicit ctThreadpoolQueueWaitableResult(FunctorType&& functor) :
        m_function(std::forward<FunctorType>(functor))
    {
    }

    ~ctThreadpoolQueueWaitableResult() override = default;

    ctThreadpoolQueueWaitableResult(ctThreadpoolQueueWaitableResult&&) noexcept = default;
    ctThreadpoolQueueWaitableResult& operator=(ctThreadpoolQueueWaitableResult&&) noexcept = default;

    // returns ERROR_SUCCESS if the callback ran to completion
    // returns ERROR_TIMEOUT if this wait timed out
    // - this can be called multiple times if needing to probe
    // any other error code resulted from attempting to run the callback
    // - meaning it did *not* run to completion
    DWORD wait(DWORD timeout) const noexcept
    {
        if (!m_completionSignal.wait(timeout))
        {
            // not setting m_internalError to timeout
            // since the caller is allowed to try to wait() again later
            return ERROR_TIMEOUT;
        }
        const auto lock = m_lock.lock_shared();
        return m_internalError;
    }

    // waitable event handle, signaled when the callback has run to completion (or failed)
    HANDLE notification_event() const noexcept
    {
        return m_completionSignal.get();
    }

    const TReturn& read_result() const noexcept
    {
        return m_result;
    }

    // move the result out of the object for move-only types
    TReturn move_result() noexcept
    {
        TReturn moveOut(std::move(m_result));
        return moveOut;
    }

    // non-copyable
    ctThreadpoolQueueWaitableResult(const ctThreadpoolQueueWaitableResult&) = delete;
    ctThreadpoolQueueWaitableResult& operator=(const ctThreadpoolQueueWaitableResult&) = delete;

private:
    // limit who can run() and abort()
    friend class ctThreadpoolQueue;

    void run() noexcept override
    {
        // we are now running in the TP callback
        {
            const auto lock = m_lock.lock_exclusive();
            if (m_runStatus != RunStatus::NotYetRun)
            {
                // return early - the caller has already canceled this
                return;
            }
            m_runStatus = RunStatus::Running;
        }

        DWORD error = NO_ERROR;
        try
        {
            m_result = std::move(m_function());
        }
        catch (...)
        {
            HRESULT hr = wil::ResultFromCaughtException();
            // HRESULT_TO_WIN32
            error = HRESULT_FACILITY(hr) == FACILITY_WIN32 ? HRESULT_CODE(hr) : hr;
        }

        const auto lock = m_lock.lock_exclusive();
        WI_ASSERT(m_runStatus == RunStatus::Running);
        m_runStatus = RunStatus::RanToCompletion;
        m_internalError = error;
        m_completionSignal.SetEvent();
    }

    void abort() noexcept override
    {
        const auto lock = m_lock.lock_exclusive();
        // only override the error if we know we haven't started running their functor
        if (m_runStatus == RunStatus::NotYetRun)
        {
            m_runStatus = RunStatus::Canceled;
            m_internalError = ERROR_CANCELLED;
            m_completionSignal.SetEvent();
        }
    }

    std::function<TReturn()> m_function;
    wil::unique_event m_completionSignal{wil::EventOptions::ManualReset};
    mutable wil::srwlock m_lock;
    TReturn m_result{};
    DWORD m_internalError = NO_ERROR;

    enum class RunStatus
    {
        NotYetRun,
        Running,
        RanToCompletion,
        Canceled
    } m_runStatus{RunStatus::NotYetRun};
};


class ctThreadpoolQueue
{
public:
    enum class QueueGrowthPolicy
    {
        Growable,
        Flat
    };

    explicit ctThreadpoolQueue(QueueGrowthPolicy growthPolicy = QueueGrowthPolicy::Growable) :
        m_tpEnvironment(0, 1), m_growthPolicy(growthPolicy)
    {
        // create a single-threaded threadpool
        m_tpHandle = m_tpEnvironment.create_tp(WorkCallback, this);
    }

    template <typename TReturn, typename FunctorType>
    std::shared_ptr<ctThreadpoolQueueWaitableResult<TReturn>> submit_with_results(FunctorType&& functor) noexcept try
    {
        FAIL_FAST_IF(m_tpHandle.get() == nullptr);

        std::shared_ptr<ctThreadpoolQueueWaitableResult<TReturn>> returnResult;
        // scope to the queue lock
        {
            const auto queueLock = m_lock.lock_exclusive();
            if (CanSubmit())
            {
                returnResult = std::make_shared<ctThreadpoolQueueWaitableResult<TReturn>>(std::forward<FunctorType>(functor));
                m_workItems.emplace_back(returnResult);
            }
        }

        if (returnResult)
        {
            // always maintain a 1:1 ratio for calls to SubmitWorkWithResults() and ::SubmitThreadpoolWork
            SubmitThreadpoolWork(m_tpHandle.get());
        }
        return returnResult;
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        return nullptr;
    }

    template <typename FunctorType>
    bool submit(FunctorType&& functor) noexcept try
    {
        FAIL_FAST_IF(m_tpHandle.get() == nullptr);

        auto submit = false;
        // scope to the queue lock
        {
            const auto queueLock = m_lock.lock_exclusive();
            if (CanSubmit())
            {
                m_workItems.emplace_back(std::forward<SimpleFunctionT>(functor));
                submit = true;
            }
        }

        if (submit)
        {
            // always maintain a 1:1 ratio for calls to SubmitWork() and ::SubmitThreadpoolWork
            SubmitThreadpoolWork(m_tpHandle.get());
        }
        return true;
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        return false;
    }

    // functors must return type HRESULT
    template <typename FunctorType>
    HRESULT submit_and_wait(FunctorType&& functor) noexcept try
    {
        // this is not applicable for flat queues
        FAIL_FAST_IF(m_growthPolicy == QueueGrowthPolicy::Flat);

        HRESULT hr = HRESULT_FROM_WIN32(ERROR_OUTOFMEMORY);
        if (const auto waitableResult = submit_with_results<HRESULT>(std::forward<FunctorType>(functor)))
        {
            hr = HRESULT_FROM_WIN32(waitableResult->wait(INFINITE));
            if (SUCCEEDED(hr))
            {
                hr = waitableResult->read_result();
            }
        }
        return hr;
    }
    CATCH_RETURN()

    // cancels anything queued to the TP - this ctThreadpoolQueue instance can no longer be used
    void cancel() noexcept try
    {
        if (m_tpHandle)
        {
            // immediately release anyone waiting for these workitems not yet run
            {
                const auto queueLock = m_lock.lock_exclusive();

                for (const auto& work : m_workItems)
                {
                    // signal that these are canceled before we shutdown the TP which they could be scheduled
                    if (const auto* pWaitableWorkitem = std::get_if<WaitableFunctionT>(&work))
                    {
                        (*pWaitableWorkitem)->abort();
                    }
                }

                m_workItems.clear();
            }

            // force the m_tpHandle to wait and close the TP
            m_tpHandle.reset();
        }
    }
    CATCH_LOG()

    bool IsRunningInQueue() const noexcept
    {
        const auto currentThreadId = GetThreadId(GetCurrentThread());
        return currentThreadId == static_cast<DWORD>(InterlockedCompareExchange64(&m_threadpoolThreadId, 0ll, 0ll));
    }

    ~ctThreadpoolQueue() noexcept
    {
        cancel();
    }

    ctThreadpoolQueue(const ctThreadpoolQueue&) = delete;
    ctThreadpoolQueue& operator=(const ctThreadpoolQueue&) = delete;
    ctThreadpoolQueue(ctThreadpoolQueue&&) = delete;
    ctThreadpoolQueue& operator=(ctThreadpoolQueue&&) = delete;

private:
    struct TpEnvironment
    {
        TP_CALLBACK_ENVIRON m_tpEnvironment{};

        using unique_tp_pool = wil::unique_any<PTP_POOL, decltype(&CloseThreadpool), CloseThreadpool>;
        unique_tp_pool m_threadPool;

        TpEnvironment(DWORD countMinThread, DWORD countMaxThread)
        {
            InitializeThreadpoolEnvironment(&m_tpEnvironment);

            m_threadPool.reset(CreateThreadpool(nullptr));
            THROW_LAST_ERROR_IF_NULL(m_threadPool.get());

            // Set min and max thread counts for custom thread pool
            THROW_LAST_ERROR_IF(!SetThreadpoolThreadMinimum(m_threadPool.get(), countMinThread));
            SetThreadpoolThreadMaximum(m_threadPool.get(), countMaxThread);
            SetThreadpoolCallbackPool(&m_tpEnvironment, m_threadPool.get());
        }

        wil::unique_threadpool_work create_tp(PTP_WORK_CALLBACK callback, void* pv)
        {
            wil::unique_threadpool_work newThreadpool(CreateThreadpoolWork(callback, pv, m_threadPool ? &m_tpEnvironment : nullptr));
            THROW_LAST_ERROR_IF_NULL(newThreadpool.get());
            return newThreadpool;
        }
    };

    using SimpleFunctionT = std::function<void()>;
    using WaitableFunctionT = std::shared_ptr<ctThreadpoolQueueWaitableResultInterface>;
    using FunctionVariantT = std::variant<SimpleFunctionT, WaitableFunctionT>;

    // the lock must be destroyed *after* the TP object (thus must be declared first)
    // since the lock is used in the TP callback
    // the lock is mutable to allow us to acquire the lock in const methods
    mutable wil::srwlock m_lock;
    TpEnvironment m_tpEnvironment;
    wil::unique_threadpool_work m_tpHandle;
    std::deque<FunctionVariantT> m_workItems;
    mutable LONG64 m_threadpoolThreadId{0}; // useful for callers to assert they are running within the queue
    const QueueGrowthPolicy m_growthPolicy;

    constexpr bool CanSubmit() const noexcept
    {
        if (m_growthPolicy == QueueGrowthPolicy::Growable)
        {
            return true;
        }

        return m_workItems.empty();
    }

    static void CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) noexcept try
    {
        auto* pThis = static_cast<ctThreadpoolQueue*>(context);

        FunctionVariantT work;
        {
            const auto queueLock = pThis->m_lock.lock_exclusive();

            if (pThis->m_workItems.empty())
            {
                // pThis object is being destroyed and the queue was cleared
                return;
            }

            std::swap(work, pThis->m_workItems.front());
            pThis->m_workItems.pop_front();

            InterlockedExchange64(&pThis->m_threadpoolThreadId, GetThreadId(GetCurrentThread()));
        }

        // run the tasks outside the ctThreadpoolQueue lock
        const auto resetThreadIdOnExit = wil::scope_exit([pThis] { InterlockedExchange64(&pThis->m_threadpoolThreadId, 0ll); });
        if (work.index() == 0)
        {
            const auto& workItem = std::get<SimpleFunctionT>(work);
            workItem();
        }
        else
        {
            const auto& waitableWorkItem = std::get<WaitableFunctionT>(work);
            waitableWorkItem->run();
        }
    }
    CATCH_LOG()
};
} // namespace
