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
#include <vector>
// os headers
#include <winsock2.h>
#include <Windows.h>
// ctl headers
#include <ctSocketExtensions.hpp>
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
#include <ctTimer.hpp>
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
        ) throw();

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
    IoImplStatus ctsMediaStreamClientIoImpl(_In_ ctsSocket* psocket, const ctsIOTask& _next_io, const std::weak_ptr<ctsSocket>& _weak_socket) throw()
    {
        switch (_next_io.ioAction) {
            // nothing failed, just no more IO right now
            case ctsIOTask::IOAction::None:
                return IoImplStatus(NO_ERROR, false);

                // it's not an error to abort any pended IO
            case ctsIOTask::IOAction::Abort:
                psocket->close_socket();
                return IoImplStatus(NO_ERROR, false);

            case ctsIOTask::IOAction::FatalAbort:
                psocket->complete_io(_next_io, 0, ctsIOPatternStatus::ErrorNotAllDataTransferred);
                psocket->close_socket();
                return IoImplStatus(ctsIOPatternStatus::ErrorNotAllDataTransferred, false);
        }

        // add-ref the IO about to start
        LONG io_count = psocket->increment_io();

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
                unsigned int bytes_transferred = 0;
                if (_completed_inline) {
                    bytes_transferred = _completed_inline_bytes;
                }

                ctsSocket::IOStatus protocol_status = psocket->complete_io(_next_io, bytes_transferred, _gle);
                switch (protocol_status) {
                    case ctsSocket::IOStatus::SuccessMoreIO:
                        ctsConfig::PrintDebug(L"\t\tctsMediaStreamClientIoImpl - complete_io returned SuccessMoreIO\n");
                        // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                        ctsConfig::PrintDebugIfFailed(function_name, _gle, L"ctsMediaStreamClient");
                        // the protocol wants to ignore the error and send more data
                        _gle = NO_ERROR;
                        more_io = true;
                        break;

                    case ctsSocket::IOStatus::SuccessDone:
                        ctsConfig::PrintDebug(L"\t\tctsMediaStreamClientIoImpl - complete_io returned SuccessDone\n");
                        // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                        ctsConfig::PrintDebugIfFailed(function_name, _gle, L"ctsMediaStreamClient");
                        // the protocol wants to ignore the error but is done with IO
                        more_io = false;
                        break;

                    case ctsSocket::IOStatus::Failure:
                        ctsConfig::PrintDebug(L"\t\tctsMediaStreamClientIoImpl - complete_io returned Failure\n");
                        // write out the error
                        ctsConfig::PrintErrorIfFailed(function_name, _gle);
                        // the protocol acknoledged the failure - socket is done with IO
                        _gle = psocket->get_last_error();
                        more_io = false;
                        break;

                    default:
                        ctl::ctAlwaysFatalCondition(L"ctsMediaStreamClientIoImpl: unknown ctsSocket::IOStatus - %u\n", protocol_status);
                }

                // decrement the IO count if failed and/or inlined-completed
                io_count = psocket->decrement_io();
                // IO count should never be zero: callers should be guaranteeing a refcount before calling Impl
                ctl::ctFatalCondition(
                    0 == io_count,
                    L"ctsMediaStreamClient : ctsSocket::io_count fell to zero while the Impl function was called (dt %p ctsTraffic::ctsSocket)",
                    psocket);
            }

            return IoImplStatus(_gle, more_io);
        };


        unsigned long gle = 0;
        unsigned long bytes_transferred = 0;
        bool completed_inline = false;

        function_name = L"ctsSocket::lock_socket";
        // take a lock on the SOCKET to use in the Winsock API
        SOCKET s = psocket->lock_socket();
        {
            // scoped for the scope guard to unlock the socket
#pragma warning(suppress: 26110)   //  PREFast is getting confused with the scope guard
            ctlScopeGuard(unlockSocketOnExit, { psocket->unlock_socket(); });

            if (INVALID_SOCKET == s) {
                return onExitFunctor(WSAENOTSOCK, false, 0);
            }

            function_name = L"ctThreadIocp::new_request";
            std::shared_ptr<ctl::ctThreadIocp> io_thread_pool;
            OVERLAPPED* pov = NULL;
            try {
                // these are the only calls which can throw in this function
                io_thread_pool = psocket->thread_pool();
                pov = io_thread_pool->new_request(
                    ctsMediaStreamClientIoCompletionCallback,
                    _weak_socket,
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

            if (ctsIOTask::IOAction::Send == _next_io.ioAction) {
                function_name = L"WSASendTo";
                const ctl::ctSockaddr target_addr(psocket->get_target());
                if (::WSASendTo(s, &wsabuf, 1, NULL, 0, target_addr.sockaddr(), target_addr.length(), pov, NULL) != 0) {
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
                if (::WSARecvFrom(s, &wsabuf, 1, NULL, &flags, NULL, NULL, pov, NULL) != 0) {
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
        ) throw()
    {
        auto shared_socket_lock(_weak_socket.lock());
        ctsSocket* psocket(shared_socket_lock.get());
        if (nullptr == psocket) {
            // underlying socket went away - nothing to do
            return;
        }

        int gle = NO_ERROR;
        DWORD transferred = 0;
        SOCKET s = psocket->lock_socket();
        if (s != INVALID_SOCKET) {
            DWORD flags;
            if (!::WSAGetOverlappedResult(s, _overlapped, &transferred, FALSE, &flags)) {
                gle = ::WSAGetLastError();
            }
            // no longer directly need the SOCKET
            psocket->unlock_socket();
            s = INVALID_SOCKET;

            if (gle != NO_ERROR) {
                ctsConfig::PrintDebug(
                    L"\t\tctsMediaStreamClientIoCompletionCallback IO completed (%s) with error %d\n",
                    (_io_task.ioAction == ctsIOTask::IOAction::Recv) ? L"WSARecvFrom" : L"WSASendTo",
                    gle);
            }

            // see if complete_io requests more IO
            ctsSocket::IOStatus protocol_status = psocket->complete_io(_io_task, transferred, gle);
            switch (protocol_status) {
                case ctsSocket::IOStatus::SuccessMoreIO: {
                    // more IO is requested from the protocol
                    IoImplStatus status = ctsMediaStreamClientIoImpl(psocket, psocket->initiate_io(), _weak_socket);
                    while (status.continue_io) {
                        // invoke the new IO call while holding a refcount to the prior IO in a tight loop
                        status = ctsMediaStreamClientIoImpl(psocket, psocket->initiate_io(), _weak_socket);
                    }
                    gle = status.error_code;
                    break;
                }

                case ctsSocket::IOStatus::SuccessDone:
                    // protocol didn't fail this IO: no more IO is requested from the protocol
                    gle = NO_ERROR;
                    break;

                case ctsSocket::IOStatus::Failure:
                    // protocol sees this as a failure - capture the error the protocol recorded
                    gle = psocket->get_last_error();
                    break;

                default:
                    ctl::ctAlwaysFatalCondition(L"ctsMediaStreamClientIoCompletionCallback: unknown ctsSocket::IOStatus - %u\n", protocol_status);
            }
        } else {
            // socket already went away - just decrement from the prior IO and exit
            // we're intentionally ignoring the error when we have closed it early
            // - doing this because that's how we shutdown the client after processing all frames
            psocket->unlock_socket();
        }

        // always decrement *after* attempting new IO
        // - the prior IO is now formally "done"
        if (psocket->decrement_io() == 0) {
            // if we have no more IO pended, complete the state
            psocket->complete_state(gle);
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
        ) throw()
    {
        auto shared_socket_lock(_weak_socket.lock());
        ctsSocket* psocket(shared_socket_lock.get());
        if (nullptr == psocket) {
            // underlying socket went away - nothing to do
            return;
        }

        int gle = NO_ERROR;
        DWORD transferred = 0;
        SOCKET s = psocket->lock_socket();
        if (s == INVALID_SOCKET) {
            gle = WSAECONNABORTED;
        } else {
            DWORD flags;
            if (!::WSAGetOverlappedResult(s, _overlapped, &transferred, FALSE, &flags)) {
                gle = ::WSAGetLastError();
            }
        }

        ctsConfig::PrintErrorIfFailed(L"\tWSASendTo (START request)", gle);

        if (NO_ERROR == gle) {
            // set the local addr
            ctl::ctSockaddr local_addr;
            int local_addr_len = local_addr.length();
            if (0 == ::getsockname(s, local_addr.sockaddr(), &local_addr_len)) {
                psocket->set_local(local_addr);
            }
            // set the remote addr
            psocket->set_target(_target_address);
        }

        // unlock before completing the socket state
        psocket->unlock_socket();
        psocket->complete_state(gle);

        // print results after completing state
        if (NO_ERROR == gle) {
            ctsConfig::PrintNewConnection(_target_address);
        }
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to run Winsock IO using IO Completion Ports
    /// - with the specified ctsSocket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamClient(std::weak_ptr<ctsSocket> _weak_socket) throw()
    {
        // attempt to get a reference to the socket
        auto shared_socket_lock(_weak_socket.lock());
        ctsSocket* psocket = shared_socket_lock.get();
        if (psocket == nullptr) {
            // the underlying socket went away - nothing to do
            return;
        }

        // always register our ctsIOPattern callback since it's necessary for this IO Pattern
        psocket->register_pattern_callback(
            [_weak_socket] (const ctsIOTask& _task) {
            // attempt to get a reference to the socket
            auto shared_socket_lock(_weak_socket.lock());
            ctsSocket* psocket = shared_socket_lock.get();
            if (psocket == nullptr) {
                // the underlying socket went away - nothing to do
                ctsConfig::PrintDebug(L"\t\tctsMediaStreamClient callback - NULL ctsSocket!!\n");
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
            if (psocket->increment_io() > 1) {
                // only running this one task in the OOB callback
                IoImplStatus status = ctsMediaStreamClientIoImpl(psocket, _task, _weak_socket);
                // always check to see if we aborted IO in the middle of an operation
                // - there's a timing window where the main path won't see this decremented to zero
                if (psocket->decrement_io() == 0) {
                    psocket->complete_state(status.error_code);
                }
            } else {
                psocket->decrement_io();
            }
        });

        // increment IO count while issuing this Impl so we hold a ref-count during this out of band callback
        psocket->increment_io();
        IoImplStatus status = ctsMediaStreamClientIoImpl(psocket, psocket->initiate_io(), _weak_socket);
        while (status.continue_io) {
            // invoke the new IO call while holding a refcount to the prior IO in a tight loop
            status = ctsMediaStreamClientIoImpl(psocket, psocket->initiate_io(), _weak_socket);
        }
        if (0 == psocket->decrement_io()) {
            psocket->complete_state(status.error_code);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to 'connect' to the target server by sending a START command
    /// using IO Completion Ports
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamClientConnect(std::weak_ptr<ctsSocket> _weak_socket) throw()
    {
        // attempt to get a reference to the socket
        auto shared_socket_lock(_weak_socket.lock());
        ctsSocket* psocket = shared_socket_lock.get();
        if (psocket == nullptr) {
            // the underlying socket went away - nothing to do
            return;
        }

        const ctl::ctSockaddr& targetAddress = psocket->get_target();

        bool completed_inline = false;
        int io_error = NO_ERROR;
        SOCKET s = psocket->lock_socket();
        if (s != INVALID_SOCKET) {
            std::shared_ptr<ctl::ctThreadIocp> io_thread_pool;
            OVERLAPPED* pov = NULL;

            try {
                io_error = ctsConfig::SetPreConnectOptions(s);
                if (io_error != NO_ERROR) {
                    throw ctl::ctException(io_error, L"ctsConfig::SetPreConnectOptions", false);
                }
                // these are the only calls which can throw in this function
                io_thread_pool = psocket->thread_pool();
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
                auto start_task = ctsMediaStreamMessage::Construct(ctsMediaStreamMessage::START);
                WSABUF wsabuf;
                wsabuf.buf = start_task.buffer + start_task.buffer_offset;
                wsabuf.len = start_task.buffer_length;

                if (::WSASendTo(s, &wsabuf, 1, NULL, 0, targetAddress.sockaddr(), targetAddress.length(), pov, NULL) != 0) {
                    io_error = ::WSAGetLastError();
                    // IO pended successfully initiating the IO
                    if (WSA_IO_PENDING == io_error) {
                        io_error = NO_ERROR;
                    } else {
                        // must cancel the IOCP TP if the IO call fails
                        io_thread_pool->cancel_request(pov);
                    }
                } else if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP) {
                    // completed inline - won't hit the completion routine
                    io_error = NO_ERROR;
                    completed_inline = true;
                    // completed inline, so the TP won't be notified
                    io_thread_pool->cancel_request(pov);
                }

                if (NO_ERROR == io_error) {
                    ctsConfig::PrintDebug(
                        L"\t\tctsMediaStreamClient sent its START message to %s\n",
                        targetAddress.writeCompleteAddress().c_str());
                }
            }
        } else {
            io_error = WSAENOTSOCK;
        }

        // unlock before completing the socket state
        psocket->unlock_socket();

        // complete only on failure (otherwise will complete in the IOCP callback)
        if (io_error != NO_ERROR) {
            psocket->complete_state(io_error);

        } else if (completed_inline) {
            // set the local addr
            ctl::ctSockaddr local_addr;
            int local_addr_len = local_addr.length();
            if (0 == ::getsockname(s, local_addr.sockaddr(), &local_addr_len)) {
                psocket->set_local(local_addr);
            }
            // set the remote addr
            psocket->set_target(targetAddress);
            psocket->complete_state(NO_ERROR);

            ctsConfig::PrintNewConnection(targetAddress);
        }
    }
} // namespace
