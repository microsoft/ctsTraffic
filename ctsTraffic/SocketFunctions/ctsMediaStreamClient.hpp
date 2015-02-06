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
// os headers
#include <Windows.h>
#include <winsock2.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
// local headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"

#include "ctsMediaStreamProtocol.hpp"


namespace ctsTraffic {

    ///
    /// Forward-declaring the callback
    ///
    inline
    void ctsMediaStreamClientIoCompletionCallback(
        OVERLAPPED* _overlapped,
        std::weak_ptr<ctsSocket> _weak_socket,
        ctsIOTask _io_task
        ) NOEXCEPT;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// implementation of processing a ctsIOTask
    ///
    /// The 1st argument (ctsSocket*) should be acquired from locking the ctsSocket into a shared_ptr
    /// The 3rd argument (weak_ptr,ctsSocket>) is required only to pass through to the io_thread_pool
    ///
    /// *Must* always complete_io() for the [in] ctsIOTask given even on failure
    ///  (except for IOAction::None or IOAction::Abort)
    ///
    /// Return integer value for a Win32 error code
    /// - or zero for success
    /// - or -1 for no failure, but not needing more IO
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    struct IoImplStatus {
        IoImplStatus(unsigned long _error, bool _continue)
        : error_code(_error), continue_io(_continue)
        {
        }

        unsigned long error_code;
        bool continue_io;
    };

    static inline
    IoImplStatus ctsMediaStreamClientIoImpl(_In_ std::shared_ptr<ctsSocket> _shared_socket, const ctsIOTask& _next_io) NOEXCEPT
    {
            switch (_next_io.ioAction) {
                case IOTaskAction::None:
                    // nothing failed, just no more IO right now
                    return IoImplStatus(NO_ERROR, false);

                case IOTaskAction::Abort: {
                    // the protocol will signal abort when it's done
                    auto shared_pattern(_shared_socket->io_pattern());
                    shared_pattern->complete_io(_next_io, 0, 0);
                    _shared_socket->close_socket();
                    return IoImplStatus(NO_ERROR, false);
                }

                case IOTaskAction::FatalAbort: {
                    // the protocol indicated to rudely abort the connection
                    auto shared_pattern(_shared_socket->io_pattern());
                    shared_pattern->complete_io(_next_io, 0, 0);
                    _shared_socket->close_socket();
                    return IoImplStatus(shared_pattern->get_last_error(), false);
                }
            }

        // add-ref the IO about to start
        LONG io_count = _shared_socket->increment_io();

        //
        // Using a functor to guarantee that regardless of error path, the required ctsSocket interactions occur
        //
        LPCWSTR function_name = nullptr;
        auto onExitFunctor = [&] (DWORD _gle, bool _completed_inline, unsigned long _completed_inline_bytes) -> IoImplStatus {
            bool more_io = false;
            if (!_completed_inline && NO_ERROR == _gle) {
                more_io = true;

            } else {
                // IO successfully completed inline or failed
                unsigned long bytes_transferred = 0;
                if (_completed_inline) {
                    bytes_transferred = _completed_inline_bytes;
                }
                // hold a reference on the iopattern
                auto shared_pattern(_shared_socket->io_pattern());
                ctsIOStatus protocol_status = shared_pattern->complete_io(_next_io, bytes_transferred, _gle);
                switch (protocol_status) {
                    case ctsIOStatus::ContinueIo:
                        // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                        ctsConfig::PrintDebugIfFailed(function_name, _gle, L"ctsMediaStreamClient");
                        // the protocol wants to ignore the error and send more data
                        _gle = NO_ERROR;
                        more_io = true;
                        break;

                    case ctsIOStatus::CompletedIo:
                        // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                        ctsConfig::PrintDebugIfFailed(function_name, _gle, L"ctsMediaStreamClient");
                        // the protocol wants to ignore the error but is done with IO
                        _shared_socket->close_socket();
                        more_io = false;
                        break;

                    case ctsIOStatus::FailedIo:
                        // write out the error
                        ctsConfig::PrintErrorIfFailed(function_name, _gle);
                        // the protocol acknoledged the failure - socket is done with IO
                        _shared_socket->close_socket();
                        _gle = shared_pattern->get_last_error();
                        more_io = false;
                        break;

                    default:
                        ctl::ctAlwaysFatalCondition(L"ctsMediaStreamClientIoImpl: unknown ctsSocket::IOStatus - %u\n", protocol_status);
                }

                // decrement the IO count if failed and/or inlined-completed
                io_count = _shared_socket->decrement_io();
                // IO count should never be zero: callers should be guaranteeing a refcount before calling Impl
                ctl::ctFatalCondition(
                    0 == io_count,
                    L"ctsMediaStreamClient : ctsSocket::io_count fell to zero while the Impl function was called (dt %p ctsTraffic::ctsSocket)",
                    _shared_socket.get());
            }

            return IoImplStatus(_gle, more_io);
        };


        unsigned long gle = 0;
        unsigned long bytes_transferred = 0;
        bool completed_inline = false;

        // scope to the socket lock
        {
            function_name = L"ctsSocket was closed";
            auto socket_lock(ctsSocket::LockSocket(_shared_socket));
            SOCKET socket = socket_lock.get();
            if (INVALID_SOCKET == socket) {
                return onExitFunctor(WSAECONNABORTED, false, 0);
            }

            std::shared_ptr<ctl::ctThreadIocp> io_thread_pool;
            OVERLAPPED* pov = NULL;
            try {
                // these are the only calls which can throw in this function
                function_name = L"ctsSocket::thread_pool";
                io_thread_pool = _shared_socket->thread_pool();

                function_name = L"ctThreadIocp::new_request";
                pov = io_thread_pool->new_request(
                    ctsMediaStreamClientIoCompletionCallback,
                    std::weak_ptr<ctsSocket>(_shared_socket),
                    _next_io);
            }
            catch (const ctl::ctException& e) {
                ctsConfig::PrintException(e);
                return onExitFunctor((0 == e.why()) ? WSAENOBUFS : e.why(), false, 0);
            }
            catch (const std::exception& e) {
                ctsConfig::PrintException(e);
                return onExitFunctor(WSAENOBUFS, false, 0);
            }

            WSABUF wsabuf;
            wsabuf.buf = _next_io.buffer + _next_io.buffer_offset;
            wsabuf.len = _next_io.buffer_length;

            if (IOTaskAction::Send == _next_io.ioAction) {
                function_name = L"WSASendTo";
                const ctl::ctSockaddr target_addr(_shared_socket->target_address());
                if (::WSASendTo(socket, &wsabuf, 1, NULL, 0, target_addr.sockaddr(), target_addr.length(), pov, NULL) != 0) {
                    gle = ::WSAGetLastError();
                    // IO pending is considered to be successful since the completion routine will handle it
                    if (WSA_IO_PENDING == gle) {
                        gle = NO_ERROR;
                    } else {
                        // IO failed - cancel the TP request
                        io_thread_pool->cancel_request(pov);
                    }

                } else if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP) {
                    /// succeeded inline - must check to see if handling inline completions
                    completed_inline = true;
                    // OVERLAPPED.InternalHigh == the number of bytes transferred for the I/O request. 
                    // - The system sets this member if the request is completed without errors. 
                    bytes_transferred = static_cast<unsigned long>(pov->InternalHigh);
                    // completed inline, so the TP won't be notified
                    io_thread_pool->cancel_request(pov);
                }

            } else {
                function_name = L"WSARecvFrom";
                DWORD flags = 0;
                if (::WSARecvFrom(socket, &wsabuf, 1, NULL, &flags, NULL, NULL, pov, NULL) != 0) {
                    gle = ::WSAGetLastError();
                    // IO pending is considered to be successful since the completion routine will handle it
                    if (WSA_IO_PENDING == gle) {
                        gle = NO_ERROR;
                    } else {
                        // IO failed - cancel the TP request
                        io_thread_pool->cancel_request(pov);
                    }

                } else if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP) {
                    /// succeeded inline - must check to see if handling inline completions
                    completed_inline = true;
                    // OVERLAPPED.InternalHigh == the number of bytes transferred for the I/O request. 
                    // - The system sets this member if the request is completed without errors. 
                    bytes_transferred = static_cast<unsigned long>(pov->InternalHigh);
                    // completed inline, so the TP won't be notified
                    io_thread_pool->cancel_request(pov);
                }
            }
        }

        return onExitFunctor(gle, completed_inline, bytes_transferred);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// IO Threadpool completion callback 
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamClientIoCompletionCallback(
        OVERLAPPED* _overlapped,
        std::weak_ptr<ctsSocket> _weak_socket,
        ctsIOTask _io_task
        ) NOEXCEPT
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        int gle = NO_ERROR;
        DWORD transferred = 0;
        // scope to the socket lock
        {
            auto socket_lock(ctsSocket::LockSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (socket != INVALID_SOCKET) {
                DWORD flags;
                if (!::WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags)) {
                    gle = ::WSAGetLastError();
                }
            } else {
                // we're intentionally ignoring the error when we have closed it early
                // - doing this because that's how we shutdown the client after processing all frames
                gle = NO_ERROR;
            }
        }

        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());
        // see if complete_io requests more IO
        ctsIOStatus protocol_status = shared_pattern->complete_io(_io_task, transferred, gle);
        switch (protocol_status) {
            case ctsIOStatus::ContinueIo: {
                // more IO is requested from the protocol
                IoImplStatus status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
                while (status.continue_io) {
                    // invoke the new IO call while holding a refcount to the prior IO in a tight loop
                    status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
                }
                gle = status.error_code;
                break;
            }

            case ctsIOStatus::CompletedIo:
                shared_socket->close_socket();
                gle = NO_ERROR;
                break;

            case ctsIOStatus::FailedIo:
                if (gle != 0) {
                    // the failure may have been a protocol error - in which case gle would just be NO_ERROR
                    ctsConfig::PrintErrorInfo(
                        L"ctsMediaStreamClientIoCompletionCallback IO failed (%s) with error %d\n",
                        (_io_task.ioAction == IOTaskAction::Recv) ? L"WSARecvFrom" : L"WSASendTo",
                        gle);
                }
                shared_socket->close_socket();
                gle = shared_pattern->get_last_error();
                break;

            default:
                ctl::ctAlwaysFatalCondition(L"ctsMediaStreamClientIoCompletionCallback: unknown ctsSocket::IOStatus - %u\n", protocol_status);
        }

        // always decrement *after* attempting new IO - the prior IO is now formally "done"
        if (shared_socket->decrement_io() == 0) {
            // if we have no more IO pended, complete the state
            shared_socket->complete_state(gle);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// IO Threadpool completion callback for the 'connect' request
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static inline
    void ctsMediaStreamClientConnectionCompletionCallback(
        OVERLAPPED* _overlapped,
        std::weak_ptr<ctsSocket> _weak_socket,
        ctl::ctSockaddr _target_address
        ) NOEXCEPT
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        int gle = NO_ERROR;
        DWORD transferred = 0;
        // scope to the socket lock
        {
            auto socket_lock(ctsSocket::LockSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (INVALID_SOCKET == socket) {
                gle = WSAECONNABORTED;
            } else {
                DWORD flags;
                if (!::WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags)) {
                    gle = ::WSAGetLastError();
                }
            }

            ctsConfig::PrintErrorIfFailed(L"\tWSASendTo (START request)", gle);

            if (NO_ERROR == gle) {
                // set the local and remote addr's
                ctl::ctSockaddr local_addr;
                int local_addr_len = local_addr.length();
                if (0 == ::getsockname(socket, local_addr.sockaddr(), &local_addr_len)) {
                    shared_socket->set_local_address(local_addr);
                }
                shared_socket->set_target_address(_target_address);
                ctsConfig::PrintNewConnection(local_addr, _target_address);
            }
        }

        shared_socket->complete_state(gle);
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to run Winsock IO using IO Completion Ports
    /// - with the specified ctsSocket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamClient(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }
        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());

        // always register our ctsIOPattern callback since it's necessary for this IO Pattern
        shared_pattern->register_callback(
            [_weak_socket] (const ctsIOTask& _task) {
            // attempt to get a reference to the socket
            auto lambda_shared_socket(_weak_socket.lock());
            if (!lambda_shared_socket) {
                return;
            }

            //
            // the below check with increment_io avoids a possible race-condition: 
            // - if increment_io() returns 1, it means our IO count in the main loop
            //   hit an io_count of 0 : which means that main thread will be completing this socket
            // - if this OOB callback ever returns 1, we cannot use this socket, since this socket
            //   will either be completed soon, or will have already been completed
            //
            // this special scenario exists because the callback doesn't hold a ref-count
            // - so this callback could be invoked after the mainline completed
            // this is still 'safe' due to the above socket locks
            //

            // increment IO count while issuing this Impl so we hold a ref-count during this out of band callback
            if (lambda_shared_socket->increment_io() > 1) {
                // only running this one task in the OOB callback
                IoImplStatus status = ctsMediaStreamClientIoImpl(lambda_shared_socket, _task);
                // decrement the IO count that we added before calling the Impl
                // - complete_state if this happened to be the final IO refcount
                if (lambda_shared_socket->decrement_io() == 0) {
                    lambda_shared_socket->complete_state(status.error_code);
                }
            } else {
                // just decrement the IO count that we added before calling the Impl (no IO attempted)
                lambda_shared_socket->decrement_io();
            }
        });

        // increment IO count while issuing this Impl so we hold a ref-count during this out of band callback
        shared_socket->increment_io();
        IoImplStatus status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
        while (status.continue_io) {
            // invoke the new IO call while holding a refcount to the prior IO in a tight loop
            status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
        }
        if (0 == shared_socket->decrement_io()) {
            shared_socket->complete_state(status.error_code);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to 'connect' to the target server by sending a START command
    /// using IO Completion Ports
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamClientConnect(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        const ctl::ctSockaddr& targetAddress = shared_socket->target_address();

        bool completed_inline = false;
        int io_error = NO_ERROR;

        // scope to the socket lock
        {
            auto socket_lock(ctsSocket::LockSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (socket != INVALID_SOCKET) {
                std::shared_ptr<ctl::ctThreadIocp> io_thread_pool;
                OVERLAPPED* pov = NULL;

                try {
                    io_error = ctsConfig::SetPreConnectOptions(socket);
                    if (io_error != NO_ERROR) {
                        throw ctl::ctException(io_error, L"ctsConfig::SetPreConnectOptions", false);
                    }
                    // these are the only calls which can throw in this function
                    io_thread_pool = shared_socket->thread_pool();
                    // also passing the buffer through to ensure it remains allocated for the lifetime of the IO
                    pov = io_thread_pool->new_request(
                        ctsMediaStreamClientConnectionCompletionCallback,
                        _weak_socket,
                        targetAddress);
                }
                catch (const ctl::ctException& e) {
                    ctsConfig::PrintException(e);
                    io_error = e.why();
                }
                catch (const std::bad_alloc& e) {
                    ctsConfig::PrintException(e);
                    io_error = WSAENOBUFS;
                }

                if (NO_ERROR == io_error) {
                    auto start_task = ctsMediaStreamMessage::Construct(MediaStreamAction::START);
                    WSABUF wsabuf;
                    wsabuf.buf = start_task.buffer + start_task.buffer_offset;
                    wsabuf.len = start_task.buffer_length;

                    if (::WSASendTo(socket, &wsabuf, 1, NULL, 0, targetAddress.sockaddr(), targetAddress.length(), pov, NULL) != 0) {
                        io_error = ::WSAGetLastError();
                        // IO pended successfully initiating the IO
                        if (WSA_IO_PENDING == io_error) {
                            io_error = NO_ERROR;
                        } else {
                            // must cancel the IOCP TP if the IO call fails
                            io_thread_pool->cancel_request(pov);
                        }
                    } else if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP) {
                        // completed inline, so the TP won't be notified
                        io_thread_pool->cancel_request(pov);
                        io_error = NO_ERROR;
                        completed_inline = true;

                        // set the local and remote addresses on the socket object
                        ctl::ctSockaddr local_addr;
                        int local_addr_len = local_addr.length();
                        if (0 == ::getsockname(socket, local_addr.sockaddr(), &local_addr_len)) {
                            shared_socket->set_local_address(local_addr);
                        }
                        shared_socket->set_target_address(targetAddress);
                        ctsConfig::PrintNewConnection(local_addr, targetAddress);
                    }
                }

                if (NO_ERROR == io_error) {
                    ctsConfig::PrintDebug(
                        L"\t\tctsMediaStreamClient sent its START message to %s\n",
                        targetAddress.writeCompleteAddress().c_str());
                }

            } else {
                io_error = WSAECONNABORTED;
            }
        }

        // complete only on failure or successfully completed inline (otherwise will complete in the IOCP callback)
        if (completed_inline || io_error != NO_ERROR) {
            shared_socket->complete_state(io_error);
        }
    }
} // namespace
