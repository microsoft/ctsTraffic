/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <memory>
#include <algorithm>
#include <functional>
#include <vector>
// os headers
#include <excpt.h>
#include <Windows.h>
#include <winsock2.h>
// ct headers
#include "ctVersionConversion.hpp"
#include "ctException.hpp"


namespace ctl {

    ///
    /// not using an unnamed namespace as debugging this is unnecessarily difficult with Windows debuggers
    ///
    ///
    /// typedef used for the std::function to be given to ctThreadIocpCallbackInfo
    /// - constructed by ctThreadIocp
    ///
    typedef std::function<void(OVERLAPPED*)> ctThreadIocpCallback_t;
    ///
    /// structure passed to the ctThreadIocp IO completion function
    /// - to allow the callback function to find the callback
    ///   associated with that completed OVERLAPPED* 
    ///
    struct ctThreadIocpCallbackInfo {
        OVERLAPPED ov;
        PVOID _padding; // required padding before the std::function for the below C_ASSERT alignment/sizing to be correct
        ctThreadIocpCallback_t callback;

        ctThreadIocpCallbackInfo(ctThreadIocpCallback_t&& _callback)
        : callback(std::move(_callback))
        {
            ::ZeroMemory(&ov, sizeof ov);
        }

        // non-copyable
        ctThreadIocpCallbackInfo(const ctThreadIocpCallbackInfo&) = delete;
        ctThreadIocpCallbackInfo& operator=(const ctThreadIocpCallbackInfo&) = delete;
    };
    /// asserting at compile time, as we assume this when we reinterpret_cast in the callback
    C_ASSERT(sizeof(ctThreadIocpCallbackInfo) == sizeof(OVERLAPPED) +sizeof(PVOID) +sizeof(ctThreadIocpCallback_t));


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctThreadIocp
    ///
    /// class that encapsulates the new-to-Vista ThreadPool APIs around OVERLAPPED IO completion ports
    ///
    /// it creates a handle to the system-managed thread pool, 
    /// - and exposes a method to get an OVERLAPPED* for asynchronous Win32 API calls which use OVERLAPPED I/O
    ///
    /// Basic usage:
    /// - construct a ctThreadIocp object by passing in the HANDLE/SOCKET on which overlapped IO calls will be made
    /// - call new_request to get an OVERLAPPED* for an asynchronous Win32 API call the associated HANDLE/SOCKET
    ///   - additionally pass a function to be invoked on IO completion 
    ///     + optional context to be associated with that IO request which is passed to the callback function
    /// - if the Win32 API succeeds or returns ERROR_IO_PENDING:
    ///    - the user's callback function will be called on completion [if succeeds or fails]
    ///    - from the callback function, the user then calls GetOverlappedResult/WSAGetOverlappedResult 
    ///      on the given OVERLAPPED* to get further details of the IO request [status, bytes transferred]
    /// - if the Win32 API fails with an error other than ERROR_IO_PENDING
    ///    - the user *must* call cancel_request, providing the OVERLAPPED* used in the failed API call
    ///    - that OVERLAPPED* is no longer valid and cannot be reused 
    ///      [new_request must be called again for another OVLERAPPED*]
    ///
    /// Additional notes regarding OVERLAPPED I/O:
    /// - the user must call new_request to get a new OVERLAPPED* before every Win32 API being made
    ///   - an OVERLAPPED* is valid only for that one API call and is invalid once the corresponding callback completes
    /// - if the IO call must be canceled after is completed successfully or returned ERROR_IO_PENDING, 
    ///   the user should take care to call the appropriate API (CancelIo, CancelIoEx, CloseHandle, closesocket)
    ///   - the user should then expect the callback to be invoked for all IO requests on that HANDLE/SOCKET
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctThreadIocp {
    public:
        ///
        /// These c'tors can fail under low resources
        /// - ctl::ctException (from the ThreadPool APIs)
        ///
        ctThreadIocp(_In_ HANDLE _handle, _In_opt_ PTP_CALLBACK_ENVIRON _ptp_env = NULL)
        {
            ptp_io = ::CreateThreadpoolIo(_handle, IoCompletionCallback, nullptr, _ptp_env);
            if (nullptr == ptp_io) {
                throw ctException(::GetLastError(), L"CreateThreadpoolIo", L"ctl::ctThreadIocp::ctThreadIocp", false);
            }
        }
        ctThreadIocp(_In_ SOCKET _socket, _In_opt_ PTP_CALLBACK_ENVIRON _ptp_env = NULL)
        {
            ptp_io = ::CreateThreadpoolIo(reinterpret_cast<HANDLE>(_socket), IoCompletionCallback, nullptr, _ptp_env);
            if (nullptr == ptp_io) {
                throw ctException(::GetLastError(), L"CreateThreadpoolIo", L"ctl::ctThreadIocp::ctThreadIocp", false);
            }
        }
        ~ctThreadIocp()
        {
            // wait for all callbacks
            ::WaitForThreadpoolIoCallbacks(this->ptp_io, FALSE);
            ::CloseThreadpoolIo(this->ptp_io);
        }
        ///
        /// new_request is expected to be called before each call to a Win32 function taking an OVLERAPPED*
        /// - which the caller expects to have their function <typename F> invoked with the following signature:
        ///     void callback_function(OVERLAPPED* _overlapped, ...)
        /// - the elipsis is to be replaced with the (optional) context types that were passed to new_request
        ///
        /// All arguments (function F as well as all Context parameters) are captured by value and thus will invoke a copy
        /// - those copies will persist until the callback invokes the function F
        ///
        /// The OVERLAPPED* returned is always owned by the object - never by the caller
        /// - the caller is expected to pass it directly to a Win32 API
        /// - after the callback completes, the OVERLAPPED* is no longer valid
        ///
        /// If the API which is given the associated HANDLE/SOCKET + OVERLAPPED* succeeds or returns ERROR_IO_PENDING,
        /// - the OVERLAPPED* is now in-use and should not be touched until it's passed to the user's callback function
        ///   [unless the user needs to cancel the IO request with another Win32 API call like CancelIoEx or closesocket]
        ///
        /// If the API which is given the associated HANDLE/SOCKET + OVERLAPPED* fails with an error other than ERROR_IO_PENDING
        /// - the caller should immediately call cancel_request() with the corresponding OVERLAPPED*
        ///
        /// Callers can invoke new_request to issue separate and concurrent IO over the same HANDLE/SOCKET
        /// - each call will return a unique OVERLAPPED*
        /// - the callback will be given the OVERLAPPED* matching the IO that completed
        ///
        template <typename F>
        OVERLAPPED* new_request(F _function)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            ctThreadIocpCallbackInfo* new_callback = new ctThreadIocpCallbackInfo(
                [_function]                    // lambda capture
                (OVERLAPPED* _pov) -> void     // lambda parameters
                { _function(_pov); });         // lambda body

            // once creating a new request succeeds, start the IO
            // - all below calls are no-fail calls
            ::StartThreadpoolIo(this->ptp_io);
            ::ZeroMemory(&new_callback->ov, sizeof OVERLAPPED);
            return &new_callback->ov;
        }
        template <typename F, typename C>
        OVERLAPPED* new_request(F _function, C _context)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            ctThreadIocpCallbackInfo* new_callback = new ctThreadIocpCallbackInfo(
                [_function, _context]              // lambda capture
                (OVERLAPPED* _pov) -> void         // lambda parameters
                { _function(_pov, _context); });   // lambda body

            // once creating a new request succeeds, start the IO
            // - all below calls are no-fail calls
            ::StartThreadpoolIo(this->ptp_io);
            ::ZeroMemory(&new_callback->ov, sizeof OVERLAPPED);
            return &new_callback->ov;
        }
        template <typename F, typename C1, typename C2>
        OVERLAPPED* new_request(F _function, C1 _context1, C2 _context2)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            ctThreadIocpCallbackInfo* new_callback = new ctThreadIocpCallbackInfo(
                [_function, _context1, _context2]             // lambda capture
                (OVERLAPPED* _pov) -> void                    // lambda parameters
                { _function(_pov, _context1, _context2); });  // lambda body

            // once creating a new request succeeds, start the IO
            // - all below calls are no-fail calls
            ::StartThreadpoolIo(this->ptp_io);
            ::ZeroMemory(&new_callback->ov, sizeof OVERLAPPED);
            return &new_callback->ov;
        }
        template <typename F, typename C1, typename C2, typename C3>
        OVERLAPPED* new_request(F _function, C1 _context1, C2 _context2, C3 _context3)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            ctThreadIocpCallbackInfo* new_callback = new ctThreadIocpCallbackInfo(
                [_function, _context1, _context2, _context3]            // lambda capture
                (OVERLAPPED* _pov) -> void                              // lambda parameter
                { _function(_pov, _context1, _context2, _context3); }); // lambda body

            // once creating a new request succeeds, start the IO
            // - all below calls are no-fail calls
            ::StartThreadpoolIo(this->ptp_io);
            ::ZeroMemory(&new_callback->ov, sizeof OVERLAPPED);
            return &new_callback->ov;
        }
        ///
        /// This function should be called only if the Win32 API call which was given the OVERLAPPED* from new_request
        /// - failed with an error other than ERROR_IO_PENDING
        ///
        /// *Note*
        /// This function does *not* cancel the IO call (e.g. does not cancel the ReadFile or WSARecv request)
        /// - it is only to notify the threadpool that there will not be any IO over the OVERLAPPED*
        ///
        void cancel_request(OVERLAPPED* _pov) NOEXCEPT
        {
            ::CancelThreadpoolIo(this->ptp_io);
            ctThreadIocpCallbackInfo* old_request = reinterpret_cast<ctThreadIocpCallbackInfo*>(_pov);
            delete old_request;
        }

        ///
        /// No default c'tor - preventing zombie objects
        /// No copy c'tors
        ///
        ctThreadIocp() = delete;
        ctThreadIocp(const ctThreadIocp&) = delete;
        ctThreadIocp& operator=(const ctThreadIocp&) = delete;

    private:
        PTP_IO ptp_io = nullptr;

        static void CALLBACK IoCompletionCallback(
            PTP_CALLBACK_INSTANCE /*_instance*/,
            PVOID /*_context*/,
            PVOID _overlapped,
            ULONG /*_ioresult*/,
            ULONG_PTR /*_numberofbytestransferred*/,
            PTP_IO /*_io*/)
        {
            // this code may look really odd 
            // the Win32 TP APIs eat stack overflow exceptions and reuses the thread for the next TP request
            // it is *not* expected that callers can/will harden their callback functions to be resilient to running out of stack at any momemnt
            // since we *do* hit this in stress, and we face ugly lock-related breaks since an SEH was swallowed while a callback held a lock, 
            // we're working really hard to break and never let TP swalling SEH exceptions
            EXCEPTION_POINTERS* exr = nullptr;
            __try {
                ctThreadIocpCallbackInfo* _request = reinterpret_cast<ctThreadIocpCallbackInfo*>(_overlapped);
                _request->callback(static_cast<OVERLAPPED*>(_overlapped));
                delete _request;
            }
            __except ((exr = GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER)
            {
                __try {
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

