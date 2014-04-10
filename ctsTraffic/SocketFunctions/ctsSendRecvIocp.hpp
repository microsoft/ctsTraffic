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
#include <ctScopeGuard.hpp>
// local headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"


namespace ctsTraffic {

    /// forward delcaration
    inline
    void ctsSendRecvIocp(std::weak_ptr<ctsSocket> _weak_socket) throw();

    struct  ctsSendRecvStatus {
        ctsSendRecvStatus() throw() : io_errorcode(NO_ERROR), io_done(false), io_started(false)
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
    void ctsIoCompletionCallback(_In_ OVERLAPPED* _overlapped, std::weak_ptr<ctsSocket> _weak_socket, ctsIOTask _io_task) throw()
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

        // write to PrintError if the IO failed
        const wchar_t* function = (ctsIOTask::IOAction::Send == _io_task.ioAction) ? L"WSASend" : L"WSARecv";

        // see if complete_io requests more IO
        DWORD sendrecv_status = NO_ERROR;
        ctsSocket::IOStatus protocol_status = psocket->complete_io(_io_task, transferred, gle);
        switch (protocol_status) {
            case ctsSocket::IOStatus::SuccessMoreIO:
                // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                ctsConfig::PrintDebugIfFailed(function, gle, L"ctsSendRecvIocp");
                // more IO is requested from the protocol
                // - invoke the new IO call while holding a refcount to the prior IO
                ctsSendRecvIocp(_weak_socket);
                break;

            case ctsSocket::IOStatus::SuccessDone:
                // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                ctsConfig::PrintDebugIfFailed(function, gle, L"ctsSendRecvIocp");
                // protocol didn't fail this IO: no more IO is requested from the protocol
                sendrecv_status = NO_ERROR;
                break;

            case ctsSocket::IOStatus::Failure:
                // write out the error
                ctsConfig::PrintErrorIfFailed(function, gle);
                // protocol sees this as a failure - capture the error the protocol recorded
                sendrecv_status = psocket->get_last_error();
                break;

            default:
                ctl::ctAlwaysFatalCondition(L"ctsSendRecvIocp : unknown ctsSocket::IOStatus (%u)", protocol_status);
        }

        // always decrement *after* attempting new IO
        // - the prior IO is now formally "done"
        if (psocket->decrement_io() == 0) {
            // if we have no more IO pended, complete the state
            psocket->complete_state(sendrecv_status);
        }

        // unlock after done touching the SOCKET
        psocket->unlock_socket();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Attempts the IO specified in the ctsIOTask on the ctsSocket
    ///
    /// ** ctsSocket::increment_io must have been called before this function was invoked
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static inline
    ctsSendRecvStatus ctsProcessIOTask(std::shared_ptr<ctsSocket> _shared_socket, const ctsIOTask& next_io) throw()
    {
        ctsSendRecvStatus return_status;

        ctsSocket* psocket = _shared_socket.get();
        if (nullptr == psocket) {
            // the underlying socket went away - nothing to do
            return_status.io_errorcode = WSAENOTSOCK;
            return_status.io_started = false;
            return_status.io_done = true;
            return return_status;
        }

        // attempt to lock the SOCKET handle
        SOCKET s = psocket->lock_socket();
#pragma warning(suppress: 26110)   //  PREFast is getting confused with the scope guard
        ctlScopeGuard(unlockSocketAtExit, { psocket->unlock_socket(); });

        if (INVALID_SOCKET == s) {
            // the underlying socket went away - nothing to do
            return_status.io_errorcode = WSAENOTSOCK;
            return_status.io_started = false;
            // if the socket was closed IO is always done, but still must complete the IO request
            return_status.io_done = true;
            psocket->complete_io(next_io, 0, return_status.io_errorcode);
            return return_status;
        }

        // attempt to allocate an IO thread-pool object
        std::shared_ptr<ctl::ctThreadIocp> io_thread_pool;
        OVERLAPPED* pov = nullptr;
        try {
            // these are the only calls which can throw in this function
            io_thread_pool = psocket->thread_pool();
            pov = io_thread_pool->new_request(
                ctsIoCompletionCallback,
                std::weak_ptr<ctsSocket>(_shared_socket),
                next_io);
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
        if (nullptr == pov) {
            return_status.io_done = (psocket->complete_io(next_io, 0, return_status.io_errorcode) != ctsSocket::IOStatus::SuccessMoreIO);
            return_status.io_started = false;
            return return_status;
        }

        WSABUF wsabuf;
        wsabuf.buf = next_io.buffer + next_io.buffer_offset;
        wsabuf.len = next_io.buffer_length;

        wchar_t* function_name = nullptr;
        if (ctsIOTask::IOAction::Send == next_io.ioAction) {
            function_name = L"WSASend";
            if (::WSASend(s, &wsabuf, 1, NULL, 0, pov, NULL) != 0) {
                return_status.io_errorcode = ::WSAGetLastError();
            }
        } else {
            function_name = L"WSARecv";
            DWORD flags = 0;
            if (::WSARecv(s, &wsabuf, 1, NULL, &flags, pov, NULL) != 0) {
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
                if (!::WSAGetOverlappedResult(s, pov, &bytes_transferred, FALSE, &flags)) {
                    ctl::ctAlwaysFatalCondition(
                        L"WSAGetOverlappedResult failed (%d) after the IO request (%s) succeeded", ::WSAGetLastError(), function_name);
                }
            }
            // must cancel the IOCP TP since IO is not pended
            io_thread_pool->cancel_request(pov);
            // call back to the socket to see if wants more IO
            ctsSocket::IOStatus protocol_status = psocket->complete_io(next_io, bytes_transferred, return_status.io_errorcode);
            switch (protocol_status) {
                case ctsSocket::IOStatus::SuccessMoreIO:
                    // The protocol layer wants to transfer more data
                    // if failed, the protocol wants to ignore the error
                    return_status.io_errorcode = NO_ERROR;
                    return_status.io_done = false;
                    break;

                case ctsSocket::IOStatus::SuccessDone:
                    // The protocol layer has successfully complete all IO on this connection
                    // if failed, the protocol wants to ignore the error
                    return_status.io_errorcode = NO_ERROR;
                    return_status.io_done = true;
                    break;

                case ctsSocket::IOStatus::Failure:
                    // write out the error
                    ctsConfig::PrintErrorIfFailed(function_name, psocket->get_last_error());
                    // the protocol acknoledged the failure - socket is done with IO
                    return_status.io_errorcode = psocket->get_last_error();
                    return_status.io_done = true;
                    break;

                default:
                    ctl::ctAlwaysFatalCondition(L"ctsSendRecvIocp: unknown ctsSocket::IOStatus - %u\n", protocol_status);
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
    void ctsProcessIOTaskCallback(std::weak_ptr<ctsSocket> _weak_socket, const ctsIOTask& next_io) throw()
    {
        // attempt to get a reference to the socket
        auto shared_socket_lock(_weak_socket.lock());
        ctsSocket* psocket = shared_socket_lock.get();
        if (psocket == nullptr) {
            // the underlying socket went away - nothing to do
            return;
        }
        // increment IO for this IO request
        psocket->increment_io();
        // run the ctsIOTask (next_io) that was scheduled through the TP timer
        // - not incrementing first as the refcount was incremented before scheduling this IO
        ctsSendRecvStatus status = ctsProcessIOTask(shared_socket_lock, next_io);
        // if no IO was started, decrement the IO counter
        if (!status.io_started) {
            if (0 == psocket->decrement_io()) {
                // this should never be zero since we should be holding a refcount for this callback
                ctl::ctAlwaysFatalCondition(
                    L"The refcount of the ctsSocket object (%p) fell to zero during a scheduled callback", psocket);
            }
        }
        // if this connection still isn't done with all IO after scheduling the prior IO
        // continue requesting IO
        if (!status.io_done) {
            ctsSendRecvIocp(_weak_socket);
        }
        // finally decrement the IO that was counted for this IO that was completed async
        if (psocket->decrement_io() == 0) {
            // if we have no more IO pended, complete the state
            psocket->complete_state(status.io_errorcode);
        }
    }

    ///
    /// The function registered with ctsConfig
    ///
    inline
    void ctsSendRecvIocp(std::weak_ptr<ctsSocket> _weak_socket) throw()
    {
        // attempt to get a reference to the socket
        auto shared_socket_lock(_weak_socket.lock());
        ctsSocket* psocket = shared_socket_lock.get();
        if (psocket == nullptr) {
            // the underlying socket went away - nothing to do
            return;
        }
        //
        // loop until failure or initiate_io returns None
        //
        // IO is always done in the ctsProcessIOTask function,
        // - either synchronously or scheduled through a timer object
        //
        // The IO refcount must be incremented here to hold an IO count on the socket
        // - so that we won't call complete_state() while any IO is still scheduled
        //
        psocket->increment_io();

        ctsSendRecvStatus status;
        while (!status.io_done) {
            ctsIOTask next_io = psocket->initiate_io();
            if (ctsIOTask::IOAction::None == next_io.ioAction) {
                // nothing failed, just no more IO right now
                break;
            }

            // increment IO for each individual request
            psocket->increment_io();

            if (next_io.time_offset_milliseconds > 0) {
                // set_timer can throw
                try {
                    shared_socket_lock->set_timer(next_io, ctsProcessIOTaskCallback);
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
                status = ctsProcessIOTask(shared_socket_lock, next_io);
            }

            // if no IO was started, decrement the IO counter
            if (!status.io_started) {
                // since IO is not pended, remove the refcount
                if (0 == psocket->decrement_io()) {
                    // this should never be zero as we are holding a reference outside the loop
                    ctl::ctAlwaysFatalCondition(
                        L"The ctsSocket (%p) refcount fell to zero while this function was holding a reference", psocket);
                }
            }
        }
        // decrement IO at the end to release the refcount held before the loop
        if (0 == psocket->decrement_io()) {
            psocket->complete_state(status.io_errorcode);
        }
    }

} // namespace
