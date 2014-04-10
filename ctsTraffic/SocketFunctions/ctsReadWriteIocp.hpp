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
// local headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"


namespace ctsTraffic {

    /// forward delcaration
    inline
    void ctsReadWriteIocp(std::weak_ptr<ctsSocket> _weak_socket) throw();

    ///
    /// IO Threadpool completion callback 
    ///
    static
    inline
    void ctsReadWriteIocpIoCompletionCallback(
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
        if (s == INVALID_SOCKET) {
            gle = WSAECONNABORTED;
        } else {
            DWORD flags;
            if (!::WSAGetOverlappedResult(s, _overlapped, &transferred, FALSE, &flags)) {
                gle = ::WSAGetLastError();
            }
        }

        const wchar_t* Function = (ctsIOTask::IOAction::Send == _io_task.ioAction) ? L"WriteFile" : L"ReadFile";
        // see if complete_io requests more IO
        DWORD readwrite_status = NO_ERROR;
        ctsSocket::IOStatus protocol_status = psocket->complete_io(_io_task, transferred, gle);
        switch (protocol_status) {
            case ctsSocket::IOStatus::SuccessMoreIO:
                // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                ctsConfig::PrintDebugIfFailed(Function, gle, L"ctsReadWriteIocp");
                // more IO is requested from the protocol
                // - invoke the new IO call while holding a refcount to the prior IO
                ctsReadWriteIocp(_weak_socket);
                break;

            case ctsSocket::IOStatus::SuccessDone:
                // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                ctsConfig::PrintDebugIfFailed(Function, gle, L"ctsReadWriteIocp");
                // protocol didn't fail this IO: no more IO is requested from the protocol
                readwrite_status = NO_ERROR;
                break;

            case ctsSocket::IOStatus::Failure:
                // write out the error
                ctsConfig::PrintErrorIfFailed(Function, gle);
                // protocol sees this as a failure - capture the error the protocol recorded
                readwrite_status = psocket->get_last_error();
                break;

            default:
                ctl::ctAlwaysFatalCondition(L"ctsReadWriteIocp: unknown ctsSocket::IOStatus - %u\n", protocol_status);
        }

        // always decrement *after* attempting new IO
        // - the prior IO is now formally "done"
        if (psocket->decrement_io() == 0) {
            // if we have no more IO pended, complete the state
            psocket->complete_state(readwrite_status);
        }

        // unlock after done touching the SOCKET
        psocket->unlock_socket();
    }

    ///
    /// The registered function with ctsConfig
    ///
    inline
    void ctsReadWriteIocp(std::weak_ptr<ctsSocket> _weak_socket) throw()
    {
        // attempt to get a reference to the socket
        auto shared_socket_lock(_weak_socket.lock());
        ctsSocket* psocket = shared_socket_lock.get();
        if (psocket == nullptr) {
            // the underlying socket went away - nothing to do
            return;
        }

        // can't initialize to zero - zero indicates to complete_state()
        long io_count = -1;
        bool io_done = false;
        int io_error = NO_ERROR;

        SOCKET s = psocket->lock_socket();
        if (s != INVALID_SOCKET) {
            // loop until failure or initiate_io returns None
            while (!io_done && (NO_ERROR == io_error)) {
                ctsIOTask next_io = psocket->initiate_io();
                if (ctsIOTask::IOAction::None == next_io.ioAction) {
                    // nothing failed, just no more IO right now
                    break;
                }

                // add-ref the IO about to start
                io_count = psocket->increment_io();

                std::shared_ptr<ctl::ctThreadIocp> io_thread_pool;
                OVERLAPPED* pov = nullptr;
                try {
                    // these are the only calls which can throw in this function
                    io_thread_pool = psocket->thread_pool();
                    pov = io_thread_pool->new_request(
                        ctsReadWriteIocpIoCompletionCallback,
                        _weak_socket,
                        next_io);
                }
                catch (const ctl::ctException& e) {
                    ctsConfig::PrintException(e);
                    io_error = (0 == e.why()) ? WSAENOBUFS : e.why();
                    io_done = (psocket->complete_io(next_io, 0, io_error) != ctsSocket::IOStatus::SuccessMoreIO);
                }
                catch (const std::bad_alloc& e) {
                    ctsConfig::PrintException(e);
                    io_error = WSAENOBUFS;
                    io_done = (psocket->complete_io(next_io, 0, io_error) != ctsSocket::IOStatus::SuccessMoreIO);
                }
                // if an exception prevented this IO from initiating,
                // return back to the IO Pattern that it failed
                if (nullptr == pov) {
                    io_count = psocket->decrement_io();
                    continue;
                }

                /////////////////////////////////////////////////////////////
                // No-Throw operations from here until end of try {} block
                /////////////////////////////////////////////////////////////

                if (ctsIOTask::IOAction::Send == next_io.ioAction) {
                    if (!::WriteFile(reinterpret_cast<HANDLE>(s), next_io.buffer + next_io.buffer_offset, next_io.buffer_length, NULL, pov)) {
                        io_error = ::GetLastError();
                    }
                } else {
                    if (!::ReadFile(reinterpret_cast<HANDLE>(s), next_io.buffer + next_io.buffer_offset, next_io.buffer_length, NULL, pov)) {
                        io_error = ::GetLastError();
                    }
                }
                //
                // not calling complete_io on success, since the IO completion will handle that in the callback
                //
                if (ERROR_IO_PENDING == io_error) {
                    io_error = NO_ERROR;
                }

                if (io_error != NO_ERROR) {
                    // must cancel the IOCP TP if the IO call fails
                    io_thread_pool->cancel_request(pov);
                    // call back to the socket that it failed to see if wants more IO
                    const wchar_t* Function = (ctsIOTask::IOAction::Send == next_io.ioAction) ? L"WriteFile" : L"ReadFile";
                    ctsSocket::IOStatus protocol_status = psocket->complete_io(next_io, 0, io_error);
                    io_done = (protocol_status != ctsSocket::IOStatus::SuccessMoreIO);
                    switch (protocol_status) {
                        case ctsSocket::IOStatus::SuccessMoreIO:
                            // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                            ctsConfig::PrintDebugIfFailed(Function, io_error, L"ctsReadWriteIocp");
                            // the protocol wants to ignore the error and send more data
                            io_error = NO_ERROR;
                            break;

                        case ctsSocket::IOStatus::SuccessDone:
                            // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                            ctsConfig::PrintDebugIfFailed(Function, io_error, L"ctsReadWriteIocp");
                            // the protocol wants to ignore the error but is done with IO
                            io_error = NO_ERROR;
                            break;

                        case ctsSocket::IOStatus::Failure:
                            // print the error on failure
                            ctsConfig::PrintErrorIfFailed(Function, io_error);
                            // the protocol acknoledged the failure - socket is done with IO
                            io_error = psocket->get_last_error();
                            break;

                        default:
                            ctl::ctAlwaysFatalCondition(L"ctsReadWriteIocp: unknown ctsSocket::IOStatus - %u\n", protocol_status);
                    }
                    // decrement the IO count since it was not pended
                    io_count = psocket->decrement_io();
                }
            }
        } else {
            io_error = WSAENOTSOCK;
        }

        if (0 == io_count) {
            // complete the ctsSocket if we have no IO pended
            psocket->complete_state(io_error);
        }

        // unlock before completing the socket state
        psocket->unlock_socket();
    }

} // namespace
