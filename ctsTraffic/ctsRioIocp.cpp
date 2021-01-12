/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <atomic>
#include <array>
#include <memory>
#include <utility>
// os headers
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
// ctl headers
#include <ctSocketExtensions.hpp>
#include <ctSockaddr.hpp>
// local headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"

namespace ctsTraffic
{
    namespace Rioiocp
    {
        //
        // constants for everything related to ctsRioIocp
        //
        constexpr uint32_t c_rioResultArrayLength = 20;
        constexpr ULONG_PTR c_exitCompletionKey = 0xffffffff;
        //
        // forward-declaring CQ-functions leveraging the below variables
        //
        static DWORD MakeRoomInCq(uint32_t newSlots) noexcept;
        static void ReleaseRoomInCompletionQueue(uint32_t slots) noexcept;
        static ULONG DequeFromCompletionQueue(_Out_writes_(RioResultArrayLength) RIORESULT* rioResults) noexcept;
        static void DeleteAllCompletionQueues() noexcept;
        //
        // Forward-declaring the IOCP threadpool function
        //
        static DWORD WINAPI RioIocpThreadProc(LPVOID) noexcept;  // NOLINT(bugprone-exception-escape)
        //
        // Management of the CQ and its corresponding threadpool implemented in this unnamed namespace
        // - initialized with InitOneExecuteOnce
        // 
        static BOOL CALLBACK InitOnceRioiocp(PINIT_ONCE, PVOID, PVOID*) noexcept;
        // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
        static INIT_ONCE g_sharedbufferInitializer = INIT_ONCE_STATIC_INIT;

        // global CRITICAL_SECTION no deleting on exit - not racing during process exit
        static CRITICAL_SECTION g_queueLock {};  // NOLINT(cppcoreguidelines-interfaces-global-init, clang-diagnostic-exit-time-destructors)
        static RIO_NOTIFICATION_COMPLETION g_rioNotifySettings;
        // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
        static RIO_CQ g_rioCompletionQueue = RIO_INVALID_CQ;
        static uint32_t g_rioComplectionQueueSize = 0;
        static uint32_t g_rioCompletionQueueUsed = 0;
        static HANDLE* g_pRioWorkerThreads = nullptr;
        static uint32_t g_rioWorkerThreadCount = 0;

        static DWORD MakeRoomInCq(uint32_t newSlots) noexcept
        {
            const auto lock = wil::EnterCriticalSection(&g_queueLock);

            const ULONG newCqUsed = g_rioCompletionQueueUsed + newSlots;
            if (g_rioComplectionQueueSize < newCqUsed)
            {
                // fail hard if we are already at the max CQ size and can't grow it for more IO
                FAIL_FAST_IF_MSG(
                    (RIO_MAX_CQ_SIZE == g_rioComplectionQueueSize) || (newCqUsed > RIO_MAX_CQ_SIZE),
                    "ctsRioIocp: attempting to grow the CQ beyond RIO_MAX_CQ_SIZE");

                // multiply new_cq_used by 1.25 for bettery growth patterns
                auto newCqSize = static_cast<ULONG>(newCqUsed * 1.25);
                if (newCqSize > RIO_MAX_CQ_SIZE)
                {
                    static_assert(MAXLONG / 1.5 > RIO_MAX_CQ_SIZE, "rio_cq_size can overflow");
                    newCqSize = RIO_MAX_CQ_SIZE;
                }

                PRINT_DEBUG_INFO(
                    L"\t\tctsRioIocp: Resizing the CQ from %u to %u (used slots = %u increasing used slots to %u)\n",
                    g_rioComplectionQueueSize,
                    newCqSize,
                    g_rioCompletionQueueUsed,
                    newCqUsed);

                if (!ctl::ctRIOResizeCompletionQueue(g_rioCompletionQueue, newCqSize))
                {
                    const auto gle = WSAGetLastError();
                    ctsConfig::PrintErrorIfFailed("ctRIOResizeCompletionQueue", gle);
                    return gle;
                }

                g_rioComplectionQueueSize = newCqSize;
            }

            g_rioCompletionQueueUsed = newCqUsed;
            return ERROR_SUCCESS;
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Release slots in the CQ
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        static void ReleaseRoomInCompletionQueue(uint32_t slots) noexcept
        {
            const auto lock = wil::EnterCriticalSection(&g_queueLock);

            FAIL_FAST_IF_MSG(
                g_rioCompletionQueueUsed < slots,
                "ctsRioIocp::release_room_in_cq(%u): underflow - current rio_cq_used value (%u)",
                slots, g_rioCompletionQueueUsed);

            PRINT_DEBUG_INFO(
                L"\t\tctsRioIocp: Reducing the CQ used slots from %u to %u\n",
                g_rioCompletionQueueUsed,
                g_rioCompletionQueueUsed - slots);

            g_rioCompletionQueueUsed -= slots;
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Safely dequeus from the CQ into the supplied RIORESULT vector
        /// - will always post a Notify with proper synchronization
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        static ULONG DequeFromCompletionQueue(_Out_writes_(RioResultArrayLength) RIORESULT* rioResults) noexcept
        {
            const auto lock = wil::EnterCriticalSection(&g_queueLock);

            const auto dequeResultCount = ctl::ctRIODequeueCompletion(g_rioCompletionQueue, rioResults, c_rioResultArrayLength);

            // We were notified there were completions, but we can't dequeue any IO
            // - something has gone horribly wrong - likely our CQ is corrupt
            // Will kill the test into the debugger to investigate
            FAIL_FAST_IF_MSG(
                // ReSharper disable once CppRedundantParentheses
                (0 == dequeResultCount) || (RIO_CORRUPT_CQ == dequeResultCount),
                "ctRIODequeueCompletion on(%p) returned [%u] : expected to have dequeued IO after being signaled",
                g_rioCompletionQueue, dequeResultCount);

            // Immediately after invoking Dequeue, post another Notify
            const auto notifyResult = ctl::ctRIONotify(g_rioCompletionQueue);

            // if notify fails, we can't reliably know when the next IO completes
            // - this will cause everything to come to a grinding halt
            // Will kill the test into the debugger to investigate
            FAIL_FAST_IF_MSG(
                notifyResult != 0,
                "RIONotify(%p) failed [%d]", g_rioCompletionQueue, notifyResult);

            return dequeResultCount;
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Shutdown all IOCP threads and close the CQ
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        static void DeleteAllCompletionQueues() noexcept
        {
            unsigned threadsAlive = 0;

            // send an exit key to all threads, then wait on all threads to exit
            for (auto loopWorkers = 0ul; loopWorkers < g_rioWorkerThreadCount; ++loopWorkers)
            {
                // queue an exit key to the worker thread
                if (g_pRioWorkerThreads[loopWorkers] != nullptr)
                {
                    ++threadsAlive;
                    if (!PostQueuedCompletionStatus(
                        g_rioNotifySettings.Iocp.IocpHandle,
                        0,
                        c_exitCompletionKey,
                        static_cast<OVERLAPPED*>(g_rioNotifySettings.Iocp.Overlapped)))
                    {
                        // if can't indicate to exit, kill the process to see why
                        FAIL_FAST_MSG(
                            "PostQueuedCompletionStatus(%p) failed [%u] to tear down the threadpool",
                            g_rioNotifySettings.Iocp.IocpHandle, GetLastError());
                    }
                }
            }

            // wait for threads to exit
            if (threadsAlive > 0)
            {
                if (WaitForMultipleObjects(
                    threadsAlive,
                    &g_pRioWorkerThreads[0],
                    TRUE,
                    INFINITE) != WAIT_OBJECT_0)
                {
                    // if can't wait for the worker threads, kill the process to see why
                    FAIL_FAST_MSG(
                        "WaitForMultipleObjects(%p) failed [%u] to wait on the threadpool",
                        &g_pRioWorkerThreads[0], GetLastError());
                }
            }

            // now can close the thread handles
            for (auto loopWorkers = 0ul; loopWorkers < g_rioWorkerThreadCount; ++loopWorkers)
            {
                if (g_pRioWorkerThreads[loopWorkers] != nullptr)
                {
                    CloseHandle(g_pRioWorkerThreads[loopWorkers]);
                }
            }

            free(g_pRioWorkerThreads);
            g_pRioWorkerThreads = nullptr;
            g_rioWorkerThreadCount = 0;

            // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
            if (g_rioCompletionQueue != RIO_INVALID_CQ)
            {
                ctl::ctRIOCloseCompletionQueue(g_rioCompletionQueue);
                // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
                g_rioCompletionQueue = RIO_INVALID_CQ;
            }

            if (g_rioNotifySettings.Iocp.IocpHandle != nullptr)
            {
                CloseHandle(g_rioNotifySettings.Iocp.IocpHandle);
                g_rioNotifySettings.Iocp.IocpHandle = nullptr;
            }

            free(g_rioNotifySettings.Iocp.Overlapped);
            g_rioNotifySettings.Iocp.Overlapped = nullptr;

            g_rioComplectionQueueSize = 0;
            g_rioCompletionQueueUsed = 0;
        }


        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Singleton initialization routine for the global CQ and its corresponding IOCP thread pool
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        static BOOL CALLBACK InitOnceRioiocp(PINIT_ONCE, PVOID, PVOID*) noexcept
        {
            FAIL_FAST_IF(!InitializeCriticalSectionEx(&g_queueLock, ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock, 0));

            // delete all cq's on error
            auto deleteAllCqsOnError = wil::scope_exit([&]() noexcept { DeleteAllCompletionQueues(); });

            ::ZeroMemory(&g_rioNotifySettings, sizeof g_rioNotifySettings);
            // completion key for RioNotify IOCP is the ctsRioIocpImpl*
            g_rioNotifySettings.Type = RIO_IOCP_COMPLETION;
            g_rioNotifySettings.Iocp.CompletionKey = nullptr;
            g_rioNotifySettings.Iocp.Overlapped = nullptr;
            g_rioNotifySettings.Iocp.IocpHandle = nullptr;

            g_rioNotifySettings.Iocp.Overlapped = calloc(1, sizeof OVERLAPPED);
            if (!g_rioNotifySettings.Iocp.Overlapped)
            {
                ctsConfig::PrintException(WSAENOBUFS, L"calloc (OVERLAPPED)", L"ctsRioIocp");
                SetLastError(WSAENOBUFS);
                return FALSE;
            }
            // free the OVERLAPPED on error
            auto freeOverlappedOnError = wil::scope_exit([&]() noexcept { free(g_rioNotifySettings.Iocp.Overlapped); });

            g_rioNotifySettings.Iocp.IocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
            if (!g_rioNotifySettings.Iocp.IocpHandle)
            {
                const auto gle = GetLastError();
                ctsConfig::PrintException(gle, L"CreateIoCompletionPort", L"ctsRioIocp");
                SetLastError(gle);
                return FALSE;
            }
            // close the IOCP handle on error
            auto deleteIocpOnError = wil::scope_exit([&]() noexcept { CloseHandle(g_rioNotifySettings.Iocp.IocpHandle); });

            constexpr uint32_t rioDefaultCqSize = 1000;
            // with RIO, we don't associate the IOCP handle with the socket like 'typical' sockets
            // - instead we directly pass the IOCP handle through RIOCreateCompletionQueue
            g_rioCompletionQueue = ctl::ctRIOCreateCompletionQueue(rioDefaultCqSize, &g_rioNotifySettings);
            // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
            if (RIO_INVALID_CQ == g_rioCompletionQueue)
            {
                const auto gle = WSAGetLastError();
                ctsConfig::PrintException(gle, L"ctRIOCreateCompletionQueue", L"ctsRioIocp");
                SetLastError(gle);
                return FALSE;
            }
            // close the RIO CQ on error
            auto closeCompletionQueueOnError = wil::scope_exit([&]() noexcept { ctl::ctRIOCloseCompletionQueue(g_rioCompletionQueue); });

            // now that the CQ is created, update info
            g_rioComplectionQueueSize = rioDefaultCqSize;
            g_rioCompletionQueueUsed = 0;

            // reserve space for handles
            SYSTEM_INFO systemInfo;
            GetSystemInfo(&systemInfo);
            g_pRioWorkerThreads = static_cast<HANDLE*>(calloc(systemInfo.dwNumberOfProcessors, sizeof HANDLE));
            if (!g_pRioWorkerThreads)
            {
                ctsConfig::PrintException(ERROR_OUTOFMEMORY, L"calloc", L"ctsRioIocp");
                SetLastError(WSAENOBUFS);
                return FALSE;
            }
            // free the handle array on error
            auto freeHandleArrayOnError = wil::scope_exit([&]() noexcept { free(g_pRioWorkerThreads); });

            g_rioWorkerThreadCount = systemInfo.dwNumberOfProcessors;

            // now that we are ready to go, kick off our thread-pool
            for (auto loopWorkers = 0ul; loopWorkers < g_rioWorkerThreadCount; ++loopWorkers)
            {
                g_pRioWorkerThreads[loopWorkers] = CreateThread(nullptr, 0, RioIocpThreadProc, nullptr, 0, nullptr);
                if (!g_pRioWorkerThreads[loopWorkers])
                {
                    const auto gle = GetLastError();
                    ctsConfig::PrintException(gle, L"CreateThread", L"ctsRioIocp");
                    SetLastError(gle);
                    return FALSE;
                }
            }
            // scopedDeleteAllCqs will take care of cleaning up these threads on failure

            // if everything succeeds, post a Notify to catch the first set of IO
            const auto notify = ctl::ctRIONotify(g_rioCompletionQueue);
            if (notify != NO_ERROR)
            {
                ctsConfig::PrintException(notify, L"ctRIONotify", L"ctsRioIocp");
                SetLastError(notify);
                return FALSE;
            }

            // dismiss all scope guards - successfully initialized
            freeHandleArrayOnError.release();
            closeCompletionQueueOnError.release();
            deleteIocpOnError.release();
            freeOverlappedOnError.release();
            deleteAllCqsOnError.release();
            return TRUE;
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// RioSocketContext
    ///
    /// This ptr is passed through RIO APIs as the SOCKET context for that IO operation
    /// 
    /// This stores all relevant information with regards to the RIO SOCKET
    /// Including encapsulating the RIO_RQ associated with the socket
    /// 
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class RioSocketContext
    {
        wil::critical_section m_lock{ ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock };
        std::weak_ptr<ctsSocket> m_weakSocket;
        ctl::ctSockaddr m_remoteSockaddr;
        RIO_BUF m_rioRemoteAddress{};
        RIO_RQ m_rioRequestQueue = RIO_INVALID_RQ;

        const uint32_t m_rioRqGrowthFactor = 4;
        uint32_t m_requestQueueSendSize = m_rioRqGrowthFactor / 2;
        uint32_t m_requestQueueRecvSize = m_rioRqGrowthFactor / 2;
        uint32_t m_outstandingSends = 0;
        uint32_t m_outstandingRecvs = 0;
        // pre-allocate all ctsTasks needed so we don't alloc/free with each IO request
        std::vector<ctsTask> m_tasks;

        // Guarantees that there is roon in the RQ for the next IO request
        // Returns NO_ERROR for success, or a Win32 error on failure
        // Requires m_lock to be held
        std::tuple<DWORD, ctsTask*> MakeRoomInRequestQueue(const ctsTask& nextTask) noexcept
        {
            auto newSendSize = m_requestQueueSendSize;
            auto newRecvSize = m_requestQueueRecvSize;

            switch (nextTask.m_ioAction)
            {
                case ctsTaskAction::Send:
                    if (m_outstandingSends >= m_requestQueueSendSize)
                    {
                        newSendSize = m_requestQueueSendSize + m_rioRqGrowthFactor;
                    }
                    break;
                case ctsTaskAction::Recv:
                    if (m_outstandingRecvs >= m_requestQueueRecvSize)
                    {
                        newRecvSize = m_requestQueueRecvSize + m_rioRqGrowthFactor;
                    }
                    break;
                default:
                    FAIL_FAST();
            }

            // guarantee room in the RQ for this next IO
            if (newSendSize > m_requestQueueSendSize || newRecvSize > m_requestQueueRecvSize)
            {
                const auto error = Rioiocp::MakeRoomInCq(m_rioRqGrowthFactor);
                if (error != NO_ERROR)
                {
                    return std::make_tuple(error, nullptr);
                }

                if (!ctl::ctRIOResizeRequestQueue(
                    m_rioRequestQueue,
                    newRecvSize,
                    newSendSize))
                {
                    const auto gle = WSAGetLastError();
                    ctsConfig::PrintErrorIfFailed("RIOResizeRequestQueue", gle);
                    Rioiocp::ReleaseRoomInCompletionQueue(m_rioRqGrowthFactor);
                    return std::make_tuple(gle, nullptr);
                }

                // since it succeeded, update members with the new sizes
                m_requestQueueSendSize = newSendSize;
                m_requestQueueRecvSize = newRecvSize;
            }

            switch (nextTask.m_ioAction)
            {
                case ctsTaskAction::Send:
                    ++m_outstandingSends;
                    break;
                case ctsTaskAction::Recv:
                    ++m_outstandingRecvs;
                    break;
                default:
                    FAIL_FAST();
            }

            if (m_tasks.empty())
            {
                FAIL_FAST();
            }

            ctsTask* pReturnTask{ nullptr };
            for (auto& possibleTask : m_tasks)
            {
                if (possibleTask.m_rioBufferid == RIO_INVALID_BUFFERID)
                {
                    FAIL_FAST_IF(nextTask.m_rioBufferid == RIO_INVALID_BUFFERID);
                    // populate the task with the new task value
                    possibleTask = nextTask;
                    // return to the caller the address of this task to use for the request context
                    // possibleTask is now tracked as in-use because m_rioBufferid is set to a valid id
                    pReturnTask = &possibleTask;
                    break;
                }
            }

            FAIL_FAST_IF(pReturnTask == nullptr);
            return std::make_tuple(NO_ERROR, pReturnTask);
        }

        // Guarantees that there is roon in the RQ for the next IO request
        // Requires m_lock to be held
        void ReleaseRoomInRequestQueue(ctsTask* const pCompletedTask) noexcept
        {
            switch (pCompletedTask->m_ioAction)
            {
                case ctsTaskAction::Send:
                    --m_outstandingSends;
                    break;
                case ctsTaskAction::Recv:
                    --m_outstandingRecvs;
                    break;
                default:
                    FAIL_FAST();
            }

            // pCompletedTask is pointing to a ctsTask in m_tasks
            // update the task so it's now available to be used for future IO
            pCompletedTask->m_rioBufferid = RIO_INVALID_BUFFERID;
        }

    public:
        explicit RioSocketContext(std::weak_ptr<ctsSocket> weakSocket)
            : m_weakSocket(std::move(weakSocket))
        {
            m_rioRemoteAddress.BufferId = RIO_INVALID_BUFFERID;
            m_rioRemoteAddress.Length = 0;
            m_rioRemoteAddress.Offset = 0;

            const auto sharedSocket = m_weakSocket.lock();
            if (!sharedSocket)
            {
                THROW_WIN32_MSG(WSAECONNABORTED, "ctsRioIocp: null socket given to RioSocketContext");
            }

            // lock the socket when doing IO on it
            const auto socketReference(sharedSocket->AcquireSocketLock());
            const SOCKET socket = socketReference.Get();
            if (INVALID_SOCKET == socket)
            {
                THROW_WIN32_MSG(WSAECONNABORTED, "ctsRioIocp: invalid socket given to RioSocketContext");
            }

            // hold a reference on the iopattern to ask for the RIO IO count
            auto lockedPattern(sharedSocket->LockIoPattern());
            if (!lockedPattern)
            {
                THROW_WIN32_MSG(WSAECONNABORTED, "ctsRioIocp: failed to get a lock on the ctsIoPatter");
            }

            // guarantee we have the maximum number of possible IOs that could be sent or received
            m_tasks.resize(lockedPattern->GetRioBufferIdCount());

            const auto error = Rioiocp::MakeRoomInCq(m_rioRqGrowthFactor);
            if (error != NO_ERROR)
            {
                THROW_WIN32_MSG(WSAENOBUFS, "ctsRioIocp: failed to make room in the cq");
            }
            auto releaseRoomInCqOnFailure = wil::scope_exit([&]() noexcept { Rioiocp::ReleaseRoomInCompletionQueue(m_rioRqGrowthFactor); });

            constexpr uint32_t rioMaxDataBuffers = 1; // this is the only value accepted as of Win8
            // create the RQ for this socket
            // don't need a scope guard to close the RQ on error - the RQ is freed when the RIO socket is closed
            m_rioRequestQueue = ctl::ctRIOCreateRequestQueue(
                socket,
                m_requestQueueRecvSize, rioMaxDataBuffers,
                m_requestQueueSendSize, rioMaxDataBuffers,
                Rioiocp::g_rioCompletionQueue,
                Rioiocp::g_rioCompletionQueue,
                this);
            // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
            if (RIO_INVALID_RQ == m_rioRequestQueue)
            {
                THROW_WIN32_MSG(WSAGetLastError(), "RIOCreateRequestQueue");
            }

            // now register the target remote address for UDP for RIOSendTo
            if (ctsConfig::ProtocolType::UDP == ctsConfig::g_configSettings->Protocol)
            {
                m_remoteSockaddr = sharedSocket->GetRemoteSockaddr();
                m_rioRemoteAddress.Length = static_cast<ULONG>(sizeof SOCKADDR_INET);
                m_rioRemoteAddress.BufferId =
                    ctl::ctRIORegisterBuffer(
                        reinterpret_cast<PCHAR>(m_remoteSockaddr.sockaddr_inet()),
                        static_cast<DWORD>(sizeof SOCKADDR_INET));
                if (RIO_INVALID_BUFFERID == m_rioRemoteAddress.BufferId)
                {
                    THROW_WIN32_MSG(WSAGetLastError(), "RIORegisterBuffer");
                }
            }

            // no failures
            releaseRoomInCqOnFailure.release();
        }

        ~RioSocketContext() noexcept
        {
            // release all the space in the CQ for this RQ
            Rioiocp::ReleaseRoomInCompletionQueue(static_cast<ULONG>(m_requestQueueSendSize + m_requestQueueRecvSize));

            if (m_rioRemoteAddress.BufferId != RIO_INVALID_BUFFERID)
            {
                ctl::ctRIODeregisterBuffer(m_rioRemoteAddress.BufferId);
            }
        }

        RioSocketContext(const RioSocketContext&) = delete;
        RioSocketContext(RioSocketContext&&) = delete;
        RioSocketContext& operator=(const RioSocketContext&) = delete;
        RioSocketContext& operator=(RioSocketContext&&) = delete;

        // 
        // Should be called once for every IO that was completed
        // Returns the current # of outstanding IO on the socket
        //
        LONG CompleteRequest(ctsTask* const pTask, ULONG transferred, LONG status) noexcept
        {
            // get a reference on the ctsSocket and IOPattern
            const auto sharedSocket(m_weakSocket.lock());
            if (!sharedSocket)
            {
                const auto lock = m_lock.lock();
                // release the RQ and the ctsTask back to the RioSocketContext object before returning
                ReleaseRoomInRequestQueue(pTask);
                return m_outstandingRecvs + m_outstandingSends;
            }

            // Must lock the socket before doing anything on it
            const auto socketReference(sharedSocket->AcquireSocketLock());

            // hold a reference on the iopattern
            auto lockedPattern(sharedSocket->LockIoPattern());
            if (!lockedPattern)
            {
                const auto lock = m_lock.lock();
                // release the RQ and the ctsTask back to the RioSocketContext object before returning
                ReleaseRoomInRequestQueue(pTask);
                return m_outstandingRecvs + m_outstandingSends;
            }

            // take a lock on our RioSocketContext before evaluating changes
            const auto lock = m_lock.lock();

            // decrement the counter in our RQ for the completed IO
            const auto* const functionName = pTask->m_ioAction == ctsTaskAction::Recv ?
                "RIOReceive" : "RIOSend";

            if (status != NO_ERROR)
            {
                PRINT_DEBUG_INFO(
                    L"\t\tIO Failed: %hs (%d) [ctsRioIocp]\n", functionName, status);
            }

            // CompleteIo() to see if the protocol needs to issue more IO
            // - will return this error unless the protocol wants more IO 
            // - if the protocol wants more IO even though this failed, 
            //   will return the error from the next IO
            DWORD error;
            const auto protocolStatus = lockedPattern->CompleteIo(*pTask, transferred, status);
            switch (protocolStatus)
            {
                case ctsIoStatus::ContinueIo:
                    // more IO is requested from the protocol
                    // launch the next IO while holding the socket lock in complete_io
                    error = InitiateRequest();
                    break;

                case ctsIoStatus::CompletedIo:
                    // no more IO is requested from the protocol
                    error = NO_ERROR;
                    break;

                case ctsIoStatus::FailedIo:
                    // write out the error
                    ctsConfig::PrintErrorIfFailed(functionName, status);

                    // protocol sees this as a failure - capture the error the protocol recorded
                    error = lockedPattern->GetLastPatternError();
                    break;

                default:
                    FAIL_FAST_MSG("ctsSendRecvIocp: unknown ctsSocket::IOStatus - %u\n", static_cast<unsigned>(protocolStatus));
            }

            // release the RQ and the ctsTask back to the RioSocketContext object before returning
            ReleaseRoomInRequestQueue(pTask);

            // finally decrement the IO counter for the completed IO that triggered this complete_io function call
            const auto currentIo = sharedSocket->DecrementIo();
            if (0 == currentIo)
            {
                sharedSocket->CompleteState(error);
            }

            FAIL_FAST_IF(currentIo != static_cast<long>(m_outstandingRecvs + m_outstandingSends));
            return currentIo;
        }

        // Attempts to send/recv IO on the socket
        // Returns the counter of pended IO on the socket
        LONG InitiateRequest() noexcept
        {
            auto sharedSocket(m_weakSocket.lock());
            FAIL_FAST_IF_MSG(
                !sharedSocket,
                "RioSocketContext::execute_io (this == %p): the ctsSocket should always be valid - it's now nullshared_socket, get() should always return a valid ptr", this);
            // hold onto the RIO socket lock while posting IO on it
            const auto socketReference(sharedSocket->AcquireSocketLock());
            SOCKET rioSocket = socketReference.Get();
            if (INVALID_SOCKET == rioSocket)
            {
                return WSAECONNABORTED;
            }

            // hold a reference on the iopattern
            // must maintain a lock to guarantee the order is correct:
            // we must send or recv the buffers that the IOPattern returns us in the order they give to us
            // i.e. without a lock we could have 2 threads get 2 buffers but randomly send them in different orders
            auto lockedPattern(sharedSocket->LockIoPattern());
            if (!lockedPattern)
            {
                return WSAECONNABORTED;
            }

            // can't initialize to zero - zero indicates to complete_state()
            long ioRefcount = -1;
            // loop until complete_io() doesn't offer IO
            bool continueIo = true;
            // take a lock on our RioSocketContext before evaluating changes
            const auto lock = m_lock.lock();
            while (continueIo)
            {
                // push IO until None is returned
                const ctsTask nextTask = lockedPattern->InitiateIo();
                if (ctsTaskAction::None == nextTask.m_ioAction)
                {
                    break;
                }

                if (ctsTaskAction::GracefulShutdown == nextTask.m_ioAction)
                {
                    auto error = NO_ERROR;
                    if (0 != shutdown(rioSocket, SD_SEND))
                    {
                        error = WSAGetLastError();
                    }
                    continueIo = lockedPattern->CompleteIo(nextTask, 0, error) == ctsIoStatus::ContinueIo;
                    continue;
                }

                if (ctsTaskAction::HardShutdown == nextTask.m_ioAction)
                {
                    // pass through -1 to force an RST with the closesocket
                    const auto error = sharedSocket->CloseSocket(-1);
                    rioSocket = INVALID_SOCKET;

                    continueIo = lockedPattern->CompleteIo(nextTask, 0, error) == ctsIoStatus::ContinueIo;
                    continue;
                }

                // if we're here, we're attempting IO
                // pre-incremenet IO tracking on the socket before issuing the IO
                ioRefcount = sharedSocket->IncrementIo();

                // must ensure we have room in the RQ & CQ before initiating the IO
                // as well as getting a ctsTask* that we'll be using for this IO
                // it can't be nextTask because that's on the stack, and the ctsTask
                // is used for the per-Request context pointer for each IO request
                PCSTR pRioFunction = "RIOResizeRequestQueue";
                auto [error, pNextTask] = MakeRoomInRequestQueue(nextTask);

                if (NO_ERROR == error)
                {
                    // with the IOTask, we can construct the RIO_BUF to send/recv
                    RIO_BUF rioBuffer{};
                    rioBuffer.BufferId = pNextTask->m_rioBufferid;
                    rioBuffer.Length = pNextTask->m_bufferLength;
                    rioBuffer.Offset = pNextTask->m_bufferOffset;

                    // invoke the requested IO now that we have room in our queues
                    switch (pNextTask->m_ioAction)
                    {
                        case ctsTaskAction::Recv:
                            pRioFunction = "RIOReceive";
                            if (!ctl::ctRIOReceive(m_rioRequestQueue, &rioBuffer, 1, 0, pNextTask))
                            {
                                error = WSAGetLastError();
                            }
                            break;
                        case ctsTaskAction::Send:
                            pRioFunction = "RIOSend";
                            if (!ctl::ctRIOSend(m_rioRequestQueue, &rioBuffer, 1, 0, pNextTask))
                            {
                                error = WSAGetLastError();
                            }
                            break;
                        default: FAIL_FAST();
                    }

                    if (error != NO_ERROR)
                    {
                        // IO failed so release the task back to the RQ
                        ReleaseRoomInRequestQueue(pNextTask);
                    }
                }

                // if IO was not initiated, complete the IO back the IO pattern
                if (error != NO_ERROR)
                {
                    ctsConfig::PrintErrorIfFailed(pRioFunction, error);

                    continueIo = lockedPattern->CompleteIo(nextTask, 0, error) == ctsIoStatus::ContinueIo;
                    ioRefcount = sharedSocket->DecrementIo();
                }
            } // while (...)

            return ioRefcount;
        }
    };


    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Logic for the thread pool function
    ///
    /// - Wait for Notify to wake up the IOCP
    /// - once notified, take a reader lock over the cq (hold off the writers)
    ///   - subsequently taking the CS
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static DWORD WINAPI Rioiocp::RioIocpThreadProc(LPVOID) noexcept  // NOLINT(bugprone-exception-escape)
    {
        std::array<RIORESULT, c_rioResultArrayLength> rioResultArray{};

        for (;;)
        {
            DWORD transferred{};
            ULONG_PTR pKey{};
            OVERLAPPED* pOverlapped{};
            //
            // Wait for the IOCP to be queued from RIO that we have results in our CQ
            //
            if (!GetQueuedCompletionStatus(
                g_rioNotifySettings.Iocp.IocpHandle,
                &transferred,
                &pKey,
                &pOverlapped,
                INFINITE))
            {
                // ReSharper disable once CppDeclaratorNeverUsed
                const auto gle = GetLastError();

                // IO was dequeued from the IOCP, meaning the deque operation failed
                // - if Deque failed, we don't know what is going on
                //   This should mean the CQ is in a bad state ???
                //   Will kill the test into the debugger to investigate
                //     - this is not recoverable other than closing every socket 
                //       and making a new CQ
                FAIL_FAST_IF_MSG(
                    nullptr != pOverlapped,
                    "GetQueuedCompletionStatus(%p) dequeued a failed IO [%u] - OVERLAPPED [%p]",
                    Rioiocp::g_rioNotifySettings.Iocp.IocpHandle, gle, pOverlapped);
            }

            if (c_exitCompletionKey == pKey)
            {
                break;
            }

            //
            // Dequeue from the RIO socket under our locks
            // - note: Dequeue will invoke a RIONotify
            //
            const ULONG completionCount = DequeFromCompletionQueue(rioResultArray.data());

            // Now that we have dequeued the IO
            // - iterate through each one and take next steps:
            //   - once we have no more IO on that socket (returned from complete_io)
            //   - delete the socket context
            //   - note: interactions with the ctsSocket* are all contained in the socket_context
            //           never directly interacting with the ctsSocket* here
            for (ULONG iterResults = 0; iterResults < completionCount; ++iterResults)
            {
                const auto bytesTransferred = rioResultArray[iterResults].BytesTransferred;
                const auto status = rioResultArray[iterResults].Status;
                auto* const requestContext = reinterpret_cast<ctsTask*>(rioResultArray[iterResults].RequestContext);
                auto* const socketContext = reinterpret_cast<RioSocketContext*>(rioResultArray[iterResults].SocketContext);

                // Complete the dequeued IO to track the IO
                // - will kick off another IO if required
                // Returns the # of IO outstanding on that socket
                // - if zero, we're done with it
                if (0 == socketContext->CompleteRequest(requestContext, bytesTransferred, status))
                {
                    delete socketContext;
                }
            } // for (iter_results)
        } // for (;;)

        return 0;
    } // RioIocpThreadProc


    void ctsRioIocp(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
    {
        // attempt to get a reference to the socket
        auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        //
        // guarantee fully initialized
        //
        if (!InitOnceExecuteOnce(&Rioiocp::g_sharedbufferInitializer, Rioiocp::InitOnceRioiocp, nullptr, nullptr))
        {
            auto gle = GetLastError();
            if (0 == gle)
            {
                gle = WSAENOBUFS;
            }
            ctsConfig::PrintException(gle, L"InitOnceExecuteOnce", L"ctsRioIocp");
            sharedSocket->CompleteState(gle);
            return;
        }

        //
        // context ptrs are declared outside the try block
        // - in the case something fails
        // - we must pass these as raw ptrs to RIO, so can't use std containers
        //
        long ioCount = 0;
        DWORD error = 0;
        RioSocketContext* socketContext = nullptr;
        try
        {
            // Allocate the socket context to pass through every IO on this socket
            socketContext = new RioSocketContext(weakSocket);
            // kick off IO on this RIO socket
            ioCount = socketContext->InitiateRequest();
        }
        catch (...)
        {
            error = ctsConfig::PrintThrownException();
        }

        // complete the socket state back to the parent if there is no pended IO
        if (0 == ioCount && sharedSocket)
        {
            sharedSocket->CompleteState(error);
            delete socketContext;
        }
    }
}
