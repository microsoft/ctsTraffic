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
#include <ctScopeGuard.hpp>
// local headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"
#include "ctsSocketGuard.hpp"

namespace ctsTraffic {

    /// forward delcaration
    inline
    void ctsSendRecvIocp(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT;

    struct  ctsSendRecvStatus {
        ctsSendRecvStatus() NOEXCEPT : io_errorcode(NO_ERROR), io_done(false), io_started(false)
        {
        }

        // Winsock error code
        unsigned long io_errorcode;
        // flag if to request another ctsIOTask
        bool io_done;
        // returns if IO was started (since can return !io_done, but I/O wasn't started yet)
        bool io_started;
    };

    ///
    /// IO Threadpool completion callback 
    ///
    static inline
    void ctsIoCompletionCallback(_In_ OVERLAPPED* _overlapped, std::weak_ptr<ctsSocket> _weak_socket, ctsIOTask _io_task) NOEXCEPT
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        // hold a reference on the iopattern
        auto shared_pattern = shared_socket->io_pattern();

        // try to get the success/error code and bytes transferred (under the socket lock)
        int gle = NO_ERROR;
        DWORD transferred = 0;
        // scoping the socket lock
        {
            auto socketlock(ctsGuardSocket(shared_socket));
            SOCKET socket = socketlock.get();
            // if we no longer have a valid socket or the pattern was destroyed, return early
            if (!shared_pattern || INVALID_SOCKET == socket) {
                gle = WSAECONNABORTED;
            } else {
                DWORD flags;
                if (!::WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags)) {
                    gle = ::WSAGetLastError();
                }
            }
        }

        // write to PrintError if the IO failed
        const wchar_t* function = (IOTaskAction::Send == _io_task.ioAction) ? L"WSASend" : L"WSARecv";
        // see if complete_io requests more IO
        ctsIOStatus protocol_status = shared_pattern->complete_io(_io_task, transferred, gle);
        switch (protocol_status) {
            case ctsIOStatus::ContinueIo:
                // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                ctsConfig::PrintDebugIfFailed(function, gle, L"ctsSendRecvIocp");
                // more IO is requested from the protocol : invoke the new IO call while holding a refcount to the prior IO
                ctsSendRecvIocp(_weak_socket);
                break;

            case ctsIOStatus::CompletedIo:
                // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                ctsConfig::PrintDebugIfFailed(function, gle, L"ctsSendRecvIocp");
                // no more IO is requested from the protocol : indicate success
                gle = NO_ERROR;
                break;

            case ctsIOStatus::FailedIo:
                // write out the error to the error log since the protocol sees this as a hard error
                ctsConfig::PrintErrorIfFailed(function, gle);
                // protocol sees this as a failure : capture the error the protocol recorded
                gle = shared_pattern->get_last_error();
                break;

            default:
                ctl::ctAlwaysFatalCondition(L"ctsSendRecvIocp : unknown ctsSocket::IOStatus (%u)", protocol_status);
        }

        // always decrement *after* attempting new IO : the prior IO is now formally "done"
        if (shared_socket->decrement_io() == 0) {
            // if we have no more IO pended, complete the state
            shared_socket->complete_state(gle);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Attempts the IO specified in the ctsIOTask on the ctsSocket
    ///
    /// ** ctsSocket::increment_io must have been called before this function was invoked
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static inline
    ctsSendRecvStatus ctsProcessIOTask(std::shared_ptr<ctsSocket> _shared_socket, const ctsIOTask& next_io) NOEXCEPT
    {
        ctsSendRecvStatus return_status;

        // hold a reference on the iopattern
        auto shared_pattern(_shared_socket->io_pattern());
        // take a lock on the socket before accessing it
        auto socketlock(ctsGuardSocket(_shared_socket));
        SOCKET socket = socketlock.get();
        // if we no longer have a valid socket return early
        if (INVALID_SOCKET == socket) {
            return_status.io_errorcode = WSAECONNABORTED;
            return_status.io_started = false;
            return_status.io_done = true;
            // even if the socket was closed we still must complete the IO request
            shared_pattern->complete_io(next_io, 0, return_status.io_errorcode);
            return return_status;
        }

        if (IOTaskAction::GracefulShutdown == next_io.ioAction) {
            if (0 != ::shutdown(socket, SD_SEND)) {
                return_status.io_errorcode = ::WSAGetLastError();
            }
            return_status.io_done = (shared_pattern->complete_io(next_io, 0, return_status.io_errorcode) != ctsIOStatus::ContinueIo);
            return_status.io_started = false;

        } else if (IOTaskAction::HardShutdown == next_io.ioAction) {
            ::linger linger_option;
            linger_option.l_onoff = 1;
            linger_option.l_linger = 0;
            if (0 != ::setsockopt(socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&linger_option), static_cast<int>(sizeof(linger_option)))) {
                return_status.io_errorcode = ::WSAGetLastError();
            }
            _shared_socket->close_socket();
            socket = INVALID_SOCKET;

            return_status.io_done = (shared_pattern->complete_io(next_io, 0, return_status.io_errorcode) != ctsIOStatus::ContinueIo);
            return_status.io_started = false;

        } else {
            // attempt to allocate an IO thread-pool object
            std::shared_ptr<ctl::ctThreadIocp> io_thread_pool;
            OVERLAPPED* pov = nullptr;
            try {
                // these are the only calls which can throw in this function
                io_thread_pool = _shared_socket->thread_pool();
                std::weak_ptr<ctsSocket> weak_reference(_shared_socket);
                pov = io_thread_pool->new_request(
                    [weak_reference, next_io] (OVERLAPPED* _ov) 
                    { ctsIoCompletionCallback(_ov, weak_reference, next_io); });
            }
            catch (const ctl::ctException& e) {
                ctsConfig::PrintException(e);
                return_status.io_errorcode = (0 == e.why()) ? WSAENOBUFS : e.why();
            }
            catch (const std::bad_alloc& e) {
                ctsConfig::PrintException(e);
                return_status.io_errorcode = WSAENOBUFS;
            }

            // if an exception prevented this IO from initiating, return back to the IO Pattern that it failed
            if (return_status.io_errorcode != NO_ERROR) {
                return_status.io_done = (shared_pattern->complete_io(next_io, 0, return_status.io_errorcode) != ctsIOStatus::ContinueIo);
                return_status.io_started = false;
                return return_status;
            }

            WSABUF wsabuf;
            wsabuf.buf = next_io.buffer + next_io.buffer_offset;
            wsabuf.len = next_io.buffer_length;

            const wchar_t* function_name = nullptr;
            if (IOTaskAction::Send == next_io.ioAction) {
                function_name = L"WSASend";
                if (::WSASend(socket, &wsabuf, 1, NULL, 0, pov, NULL) != 0) {
                    return_status.io_errorcode = ::WSAGetLastError();
                }
            } else {
                function_name = L"WSARecv";
                DWORD flags = 0;
                if (::WSARecv(socket, &wsabuf, 1, NULL, &flags, pov, NULL) != 0) {
                    return_status.io_errorcode = ::WSAGetLastError();
                }
            }
            //
            // not calling complete_io if returned IO pended 
            // not calling complete_io if returned success but not handling inline completions
            //
            if ((WSA_IO_PENDING == return_status.io_errorcode) ||
                (NO_ERROR == return_status.io_errorcode && !(ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP))) {
                return_status.io_errorcode = NO_ERROR;
                return_status.io_started = true;
                return_status.io_done = false;

            } else {
                // process the completion if the API call failed, or if it succeeded and we're handling the completion inline, 
                return_status.io_started = false;
                // determine # of bytes transferred, if any
                DWORD bytes_transferred = 0;
                if (NO_ERROR == return_status.io_errorcode) {
                    DWORD flags;
                    if (!::WSAGetOverlappedResult(socket, pov, &bytes_transferred, FALSE, &flags)) {
                        ctl::ctAlwaysFatalCondition(
                            L"WSAGetOverlappedResult failed (%d) after the IO request (%s) succeeded", ::WSAGetLastError(), function_name);
                    }
                }
                // must cancel the IOCP TP since IO is not pended
                io_thread_pool->cancel_request(pov);
                // call back to the socket to see if wants more IO
                ctsIOStatus protocol_status = shared_pattern->complete_io(next_io, bytes_transferred, return_status.io_errorcode);
                switch (protocol_status) {
                    case ctsIOStatus::ContinueIo:
                        // The protocol layer wants to transfer more data
                        // if prior IO failed, the protocol wants to ignore the error
                        return_status.io_errorcode = NO_ERROR;
                        return_status.io_done = false;
                        break;

                    case ctsIOStatus::CompletedIo:
                        // The protocol layer has successfully complete all IO on this connection
                        // if prior IO failed, the protocol wants to ignore the error
                        return_status.io_errorcode = NO_ERROR;
                        return_status.io_done = true;
                        break;

                    case ctsIOStatus::FailedIo:
                        // write out the error
                        ctsConfig::PrintErrorIfFailed(function_name, shared_pattern->get_last_error());
                        // the protocol acknoledged the failure - socket is done with IO
                        return_status.io_errorcode = shared_pattern->get_last_error();
                        return_status.io_done = true;
                        break;

                    default:
                        ctl::ctAlwaysFatalCondition(L"ctsSendRecvIocp: unknown ctsSocket::IOStatus - %u\n", protocol_status);
                }
            }
        }

        return return_status;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// This is the callback for the threadpool timer.
    /// Processes the given task and then calls ctsSendRecvIocp function to deal with any additional tasks
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static inline
    void ctsProcessIOTaskCallback(std::weak_ptr<ctsSocket> _weak_socket, const ctsIOTask& next_io) NOEXCEPT
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }
        // increment IO for this IO request
        shared_socket->increment_io();
        // run the ctsIOTask (next_io) that was scheduled through the TP timer
        // - not incrementing first as the refcount was incremented before scheduling this IO
        ctsSendRecvStatus status = ctsProcessIOTask(shared_socket, next_io);
        // if no IO was started, decrement the IO counter
        if (!status.io_started) {
            if (0 == shared_socket->decrement_io()) {
                // this should never be zero since we should be holding a refcount for this callback
                ctl::ctAlwaysFatalCondition(
                    L"The refcount of the ctsSocket object (%p) fell to zero during a scheduled callback", shared_socket.get());
            }
        }
        // continue requesting IO if this connection still isn't done with all IO after scheduling the prior IO
        if (!status.io_done) {
            ctsSendRecvIocp(_weak_socket);
        }
        // finally decrement the IO that was counted for this IO that was completed async
        if (shared_socket->decrement_io() == 0) {
            // if we have no more IO pended, complete the state
            shared_socket->complete_state(status.io_errorcode);
        }
    }

    ///
    /// The function registered with ctsConfig
    ///
    inline
    void ctsSendRecvIocp(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }
        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());
        //
        // loop until failure or initiate_io returns None
        //
        // IO is always done in the ctsProcessIOTask function,
        // - either synchronously or scheduled through a timer object
        //
        // The IO refcount must be incremented here to hold an IO count on the socket
        // - so that we won't inadvertently call complete_state() while IO is still being scheduled
        //
        shared_socket->increment_io();

        ctsSendRecvStatus status;
        while (!status.io_done) {
            ctsIOTask next_io = shared_pattern->initiate_io();
            if (IOTaskAction::None == next_io.ioAction) {
                // nothing failed, just no more IO right now
                break;
            }

            // increment IO for each individual request
            shared_socket->increment_io();

            if (next_io.time_offset_milliseconds > 0) {
                // set_timer can throw
                try {
                    shared_socket->set_timer(next_io, ctsProcessIOTaskCallback);
                    status.io_started = true; // IO started in the context of keeping the count incremented
                }
                catch (const ctl::ctException& e) {
                    ctsConfig::PrintException(e);
                    status.io_started = false;
                    status.io_errorcode = e.why();
                }
                catch (const std::exception& e) {
                    ctsConfig::PrintException(e);
                    status.io_started = false;
                    status.io_errorcode = WSAENOBUFS;
                }

            } else {
                status = ctsProcessIOTask(shared_socket, next_io);
            }

            // if no IO was started, decrement the IO counter
            if (!status.io_started) {
                // since IO is not pended, remove the refcount
                if (0 == shared_socket->decrement_io()) {
                    // this should never be zero as we are holding a reference outside the loop
                    ctl::ctAlwaysFatalCondition(
                        L"The ctsSocket (%p) refcount fell to zero while this function was holding a reference", shared_socket.get());
                }
            }
        }
        // decrement IO at the end to release the refcount held before the loop
        if (0 == shared_socket->decrement_io()) {
            shared_socket->complete_state(status.io_errorcode);
        }
    }

} // namespace
