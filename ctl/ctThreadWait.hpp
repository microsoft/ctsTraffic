/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <utility>
#include <functional>
// os headers
#include <excpt.h>
#include <Windows.h>
// ct headers
#include "ctException.hpp"


namespace ctl
{
    //
    // typedef used for the std::function to be given to ctThreadIocpCallbackInfo
    // - constructed by ctThreadIocp
    //
    typedef std::function<void(void)> ctThreadWaitCallback_t;

    //
    // structure passed to the ctThreadWait IO completion function
    //
    struct ctThreadWaitCallbackInfo
    {
        HANDLE h = nullptr;
        PVOID _padding{}; // required padding before the std::function for the below C_ASSERT alignment/sizing to be correct
        ctThreadWaitCallback_t callback;

        // ReSharper disable once CppPossiblyUninitializedMember
        explicit ctThreadWaitCallbackInfo(ctThreadWaitCallback_t&& _callback)
            : callback(std::move(_callback))
        {
        }
        ~ctThreadWaitCallbackInfo() noexcept = default;
        // non-copyable
        ctThreadWaitCallbackInfo(const ctThreadWaitCallbackInfo&) = delete;
        ctThreadWaitCallbackInfo& operator=(const ctThreadWaitCallbackInfo&) = delete;
        ctThreadWaitCallbackInfo(ctThreadWaitCallbackInfo&&) = delete;
        ctThreadWaitCallbackInfo& operator=(ctThreadWaitCallbackInfo&&) = delete;
    };

    // asserting at compile time, as we assume this when we reinterpret_cast in the callback
    C_ASSERT(sizeof(ctThreadWaitCallbackInfo) == sizeof(HANDLE) + sizeof(PVOID) + sizeof(ctThreadWaitCallback_t));

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctThreadWait
    ///
    /// class that encapsulates the new-to-Vista ThreadPool APIs around waiting on event handles
    ///
    /// it creates a handle to the system-managed thread pool, 
    /// - and exposes a method to get a HANDLE for asynchronous Win32 API calls which take an event HANDLE
    ///
    /// Basic usage:
    /// - construct a ctThreadWait object
    /// - call new_request to get an HANDLE for an asynchronous Win32 API call
    ///   - additionally pass a function to be invoked on IO completion 
    /// - if the Win32 API succeeds or returns ERROR_IO_PENDING:
    ///    - the user's callback function will be called on completion [if succeeds or fails]
    ///    - from the callback function, the user's function is invoked
    /// - if the Win32 API fails with an error other than ERROR_IO_PENDING
    ///    - the user *must* call cancel_request
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctThreadWait
    {
    public:
        //
        // These c'tors can fail under low resources
        // - ctl::ctException (from the ThreadPool APIs)
        //
        explicit ctThreadWait(_In_opt_ PTP_CALLBACK_ENVIRON _ptp_env = nullptr)
        {
            ptp_wait = ::CreateThreadpoolWait(WaitCallback, nullptr, _ptp_env);
            if (!ptp_wait)
            {
                throw ctException(::GetLastError(), L"CreateThreadpoolWait", L"ctl::ctThreadWait::ctThreadWait", false);
            }
        }

        ~ctThreadWait()
        {
            // could have been moved out of
            if (ptp_wait)
            {
                // wait for all callbacks
                ::WaitForThreadpoolWaitCallbacks(ptp_wait, FALSE);
                ::CloseThreadpoolWait(ptp_wait);
            }
        }

        ctThreadWait(ctThreadWait&& rhs) noexcept
            : ptp_wait(rhs.ptp_wait)
        {
            // null out the moved-from object's TP ptr since this object now has ownership
            rhs.ptp_wait = nullptr;
        }
        ctThreadWait& operator=(ctThreadWait&& rhs) noexcept
        {
            ptp_wait = rhs.ptp_wait;
            // null out the moved-from object's TP ptr since this object now has ownership
            rhs.ptp_wait = nullptr;
            return *this;
        }

        //
        // new_request is expected to be called before each call to a Win32 function taking an OVLERAPPED*
        // - which the caller expects to have their std::function invoked with the following signature:
        //     void callback_function()
        //
        HANDLE new_request(std::function<void(void)> _callback) const
        {
            // this can fail by throwing std::bad_alloc
            // ReSharper disable CppNonReclaimedResourceAcquisition
            auto new_callback = new ctThreadWaitCallbackInfo(std::move(_callback));
            // ReSharper restore CppNonReclaimedResourceAcquisition

            // once creating a new request succeeds, start the IO
            // - all below calls are no-fail calls
            new_callback.h = ::CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_MODIFY_STATE);
            if (!ptp_wait)
            {
                throw ctException(::GetLastError(), L"CreateEventExW", L"ctl::ctThreadWait::new_request", false);
            }
            ::SetThreadpoolwait(ptp_wait, new_callback.h);
            return new_callback->h;
        }

        HANDLE reuse_request(HANDLE evt, std::function<void(void)> _callback) const noexcept
        {
            const auto old_request = reinterpret_cast<ctThreadWaitCallbackInfo*>(evt);
            old_request.callback = std::move(_callback);
            ::SetThreadpoolwait(ptp_wait, new_callback.h);
            return new_callback->h;
        }

        //
        // This function should be called only if the Win32 API call which was given the HANDLE from new_request failed
        //
        void cancel_request(HANDLE evt) const noexcept
        {
            const auto old_request = reinterpret_cast<ctThreadWaitCallbackInfo*>(evt);
            ::CloseHandle(old_request.h);
            delete old_request;
        }

        //
        // No default c'tor - preventing zombie objects
        // No copy c'tors
        //
        ctThreadWait() = delete;
        ctThreadWait(const ctThreadWait&) = delete;
        ctThreadWait& operator=(const ctThreadWait&) = delete;

    private:
        PTP_WAIT ptp_wait = nullptr;

        static void CALLBACK WaitCallback(
            _Inout_     PTP_CALLBACK_INSTANCE Instance,
            _Inout_opt_ PVOID                 Context,
            _Inout_     PTP_WAIT              Wait,
            _In_        TP_WAIT_RESULT        WaitResult) noexcept
        {
    
            // this code may look really odd 
            // the Win32 TP APIs eat stack overflow exceptions and reuses the thread for the next TP request
            // it is *not* expected that callers can/will harden their callback functions to be resilient to running out of stack at any momemnt
            // since we *do* hit this in stress, and we face ugly lock-related breaks since an SEH was swallowed while a callback held a lock, 
            // we're working really hard to break and never let TP swalling SEH exceptions
            EXCEPTION_POINTERS* exr = nullptr;
            __try
            {
                auto* _request = static_cast<ctThreadIocpCallbackInfo*>(_overlapped);
                _request->callback(static_cast<OVERLAPPED*>(_overlapped));
                delete _request;
            }
            // ReSharper disable once CppAssignedValueIsNeverUsed (exr is used in the except handler)
            __except ((exr = GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER)
            {
                __try
                {
                    ::RaiseFailFastException(exr->ExceptionRecord, exr->ContextRecord, 0);
                }
#pragma warning(suppress: 6320) // not hiding exceptions: RaiseFailFastException is fatal - this creates a break to help debugging in some scenarios
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    __debugbreak();
                }
            }
        }
    };
} // namespace
