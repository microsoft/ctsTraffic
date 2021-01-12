/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// ReSharper disable CppInconsistentNaming
// ReSharper disable CppClangTidyClangDiagnosticLanguageExtensionToken
#pragma once

// cpp headers
#include <utility>
#include <functional>
// os headers
#include <excpt.h>
#include <Windows.h>
#include <WinSock2.h>
// wil headers
#include <wil/resource.h>


namespace ctl
{
    //
    // not using an unnamed namespace as debugging this is unnecessarily difficult with Windows debuggers
    //
    //
    // typedef used for the std::function to be given to ctThreadIocpCallbackInfo
    // - constructed by ctThreadIocp
    //
    typedef std::function<void(OVERLAPPED*)> ctThreadIocpCallback_t;

    //
    // structure passed to the ctThreadIocp IO completion function
    // - to allow the callback function to find the callback
    //   associated with that completed OVERLAPPED* 
    //
    struct ctThreadIocpCallbackInfo
    {
        OVERLAPPED ov{};
        PVOID padding{}; // required padding before the std::function for the below static_assert alignment/sizing to be correct
        ctThreadIocpCallback_t callback;

        // ReSharper disable once CppPossiblyUninitializedMember
        explicit ctThreadIocpCallbackInfo(ctThreadIocpCallback_t&& _callback) noexcept
            : callback(std::move(_callback))
        {
            ::ZeroMemory(&ov, sizeof ov);
        }
        ~ctThreadIocpCallbackInfo() noexcept = default;
        // non-copyable
        ctThreadIocpCallbackInfo(const ctThreadIocpCallbackInfo&) = delete;
        ctThreadIocpCallbackInfo& operator=(const ctThreadIocpCallbackInfo&) = delete;
        ctThreadIocpCallbackInfo(ctThreadIocpCallbackInfo&&) = delete;
        ctThreadIocpCallbackInfo& operator=(ctThreadIocpCallbackInfo&&) = delete;
    };

    // asserting at compile time, as we assume this when we reinterpret_cast in the callback
    static_assert(sizeof(ctThreadIocpCallbackInfo) == sizeof(OVERLAPPED) + sizeof(PVOID) + sizeof(ctThreadIocpCallback_t));


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
    class ctThreadIocp
    {
    public:
        //
        // These c'tors can fail under low resources
        // - wil::ResultException (from the ThreadPool APIs)
        //
        explicit ctThreadIocp(HANDLE _handle, _In_opt_ PTP_CALLBACK_ENVIRON _ptp_env = nullptr)
        {
            ptp_io = CreateThreadpoolIo(_handle, IoCompletionCallback, nullptr, _ptp_env);
            if (!ptp_io)
            {
                THROW_WIN32_MSG(GetLastError(), "CreateThreadpoolIo");
            }
        }

        explicit ctThreadIocp(SOCKET _socket, _In_opt_ PTP_CALLBACK_ENVIRON _ptp_env = nullptr)
        {
            ptp_io = CreateThreadpoolIo(reinterpret_cast<HANDLE>(_socket), IoCompletionCallback, nullptr, _ptp_env);
            if (!ptp_io)
            {
                THROW_WIN32_MSG(GetLastError(), "CreateThreadpoolIo");
            }
        }

        ~ctThreadIocp()
        {
            // could have been moved out of
            if (ptp_io)
            {
                // wait for all callbacks
                WaitForThreadpoolIoCallbacks(ptp_io, FALSE);
                CloseThreadpoolIo(ptp_io);
            }
        }

        ctThreadIocp(ctThreadIocp&& rhs) noexcept
            : ptp_io(rhs.ptp_io)
        {
            // null out the moved-from object's TP ptr since this object now has ownership
            rhs.ptp_io = nullptr;
        }
        ctThreadIocp& operator=(ctThreadIocp&& rhs) noexcept
        {
            ptp_io = rhs.ptp_io;
            // null out the moved-from object's TP ptr since this object now has ownership
            rhs.ptp_io = nullptr;
            return *this;
        }

        //
        // new_request is expected to be called before each call to a Win32 function taking an OVLERAPPED*
        // - which the caller expects to have their std::function invoked with the following signature:
        //     void callback_function(OVERLAPPED* _overlapped)
        //
        // The OVERLAPPED* returned is always owned by the object - never by the caller
        // - the caller is expected to pass it directly to a Win32 API
        // - after the callback completes, the OVERLAPPED* is no longer valid
        //
        // If the API which is given the associated HANDLE/SOCKET + OVERLAPPED* succeeds or returns ERROR_IO_PENDING,
        // - the OVERLAPPED* is now in-use and should not be touched until it's passed to the user's callback function
        //   [unless the user needs to cancel the IO request with another Win32 API call like CancelIoEx or closesocket]
        //
        // If the API which is given the associated HANDLE/SOCKET + OVERLAPPED* fails with an error other than ERROR_IO_PENDING
        // - the caller should immediately call cancel_request() with the corresponding OVERLAPPED*
        //
        // Callers can invoke new_request to issue separate and concurrent IO over the same HANDLE/SOCKET
        // - each call will return a unique OVERLAPPED*
        // - the callback will be given the OVERLAPPED* matching the IO that completed
        //
        OVERLAPPED* new_request(std::function<void(OVERLAPPED*)> _callback) const
        {
            // this can fail by throwing std::bad_alloc
            // ReSharper disable CppNonReclaimedResourceAcquisition
            auto* new_callback = new ctThreadIocpCallbackInfo(std::move(_callback));
            // ReSharper restore CppNonReclaimedResourceAcquisition

            // once creating a new request succeeds, start the IO
            // - all below calls are no-fail calls
            StartThreadpoolIo(ptp_io);
            ::ZeroMemory(&new_callback->ov, sizeof OVERLAPPED);
            return &new_callback->ov;
        }

        //
        // This function should be called only if the Win32 API call which was given the OVERLAPPED* from new_request
        // - failed with an error other than ERROR_IO_PENDING
        //
        // *Note*
        // This function does *not* cancel the IO call (e.g. does not cancel the ReadFile or WSARecv request)
        // - it is only to notify the threadpool that there will not be any IO over the OVERLAPPED*
        //
        void cancel_request(OVERLAPPED* pOverlapped) const noexcept
        {
            CancelThreadpoolIo(ptp_io);
            const auto* const old_request = reinterpret_cast<ctThreadIocpCallbackInfo*>(pOverlapped);
            delete old_request;
        }

        //
        // No default c'tor - preventing zombie objects
        // No copy c'tors
        //
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
            __try
            {
                auto* _request = static_cast<ctThreadIocpCallbackInfo*>(_overlapped);
                _request->callback(static_cast<OVERLAPPED*>(_overlapped));
                delete _request;
            }
            // ReSharper disable once CppAssignedValueIsNeverUsed (exr is used in the except handler)
            __except (exr = GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER)
            {
                __try
                {
                    RaiseFailFastException(exr->ExceptionRecord, exr->ContextRecord, 0);
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
