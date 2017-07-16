/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <memory>
#include <vector>
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
#include "ctsSocketGuard.hpp"

namespace ctsTraffic
{
    // forward delcaration
    void ctsReadWriteIocp(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT;

    //
    // IO Threadpool completion callback 
    //
    static void ctsReadWriteIocpIoCompletionCallback(
        _In_ OVERLAPPED* _overlapped,
        const std::weak_ptr<ctsSocket>& _weak_socket,
        const ctsIOTask& _io_task) NOEXCEPT
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }
        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());

        int gle = NO_ERROR;
        DWORD transferred = 0;
        // lock the socket just long enough to read the result
        {
            auto socket_lock(ctsGuardSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (INVALID_SOCKET == socket) {
                gle = WSAECONNABORTED;
            } else {
                DWORD flags;
                if (!::WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags)) {
                    gle = ::WSAGetLastError();
                }
            }
        }

        const wchar_t* Function = (IOTaskAction::Send == _io_task.ioAction) ? L"WriteFile" : L"ReadFile";
        if (gle != 0) PrintDebugInfo(L"\t\tIO Failed: %s (%d) [ctsReadWriteIocp]\n", Function, gle);
        // see if complete_io requests more IO
        DWORD readwrite_status = NO_ERROR;
        ctsIOStatus protocol_status = shared_pattern->complete_io(_io_task, transferred, gle);
        switch (protocol_status) {
            case ctsIOStatus::ContinueIo:
                // more IO is requested from the protocol
                // - invoke the new IO call while holding a refcount to the prior IO
                ctsReadWriteIocp(_weak_socket);
                break;

            case ctsIOStatus::CompletedIo:
                // protocol didn't fail this IO: no more IO is requested from the protocol
                readwrite_status = NO_ERROR;
                break;

            case ctsIOStatus::FailedIo:
                // write out the error
                ctsConfig::PrintErrorIfFailed(Function, gle);
                // protocol sees this as a failure - capture the error the protocol recorded
                readwrite_status = shared_pattern->get_last_error();
                break;

            default:
                ctl::ctAlwaysFatalCondition(L"ctsReadWriteIocp: unknown ctsSocket::IOStatus - %u\n", static_cast<unsigned>(protocol_status));
        }

        // always decrement *after* attempting new IO
        // - the prior IO is now formally "done"
        if (shared_socket->decrement_io() == 0) {
            // if we have no more IO pended, complete the state
            shared_socket->complete_state(readwrite_status);
        }
    }

    //
    // The registered function with ctsConfig
    //
    void ctsReadWriteIocp(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT
    {
        // must get a reference to the socket and the IO pattern
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }
        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());

        // can't initialize to zero - zero indicates to complete_state()
        long io_count = -1;
        bool io_done = false;
        int io_error = NO_ERROR;

        // lock the socket while doing IO
        auto socket_lock(ctsGuardSocket(shared_socket));
        SOCKET socket = socket_lock.get();
        if (socket != INVALID_SOCKET) {
            // loop until failure or initiate_io returns None
            while (!io_done && (NO_ERROR == io_error)) {
                // each loop requests the next task
                ctsIOTask next_io = shared_pattern->initiate_io();
                if (IOTaskAction::None == next_io.ioAction) {
                    // nothing failed, just no more IO right now
                    io_done = true;
                    continue;
                }

                if (IOTaskAction::GracefulShutdown == next_io.ioAction) {
                    if (0 != ::shutdown(socket, SD_SEND)) {
                        io_error = ::WSAGetLastError();
                    }
                    io_done = (shared_pattern->complete_io(next_io, 0, io_error) != ctsIOStatus::ContinueIo);

                } else if (IOTaskAction::HardShutdown == next_io.ioAction) {
                    // pass through -1 to force an RST with the closesocket
                    io_error = shared_socket->close_socket(-1);
                    socket = INVALID_SOCKET;

                    io_done = (shared_pattern->complete_io(next_io, 0, io_error) != ctsIOStatus::ContinueIo);

                } else {
                    // add-ref the IO about to start
                    io_count = shared_socket->increment_io();

                    std::shared_ptr<ctl::ctThreadIocp> io_thread_pool;
                    OVERLAPPED* pov = nullptr;
                    try {
                        // these are the only calls which can throw in this function
                        io_thread_pool = shared_socket->thread_pool();
                        pov = io_thread_pool->new_request(
                            [_weak_socket, next_io] (OVERLAPPED* _ov) { ctsReadWriteIocpIoCompletionCallback(_ov, _weak_socket, next_io); });
                    }
                    catch (const ctl::ctException& e) {
                        ctsConfig::PrintException(e);
                        io_error = (0 == e.why()) ? WSAENOBUFS : e.why();
                    }
                    catch (const std::bad_alloc& e) {
                        ctsConfig::PrintException(e);
                        io_error = WSAENOBUFS;
                    }
                    // if an exception prevented this IO from initiating,
                    if (io_error != NO_ERROR) {
                        io_count = shared_socket->decrement_io();
                        io_done = (shared_pattern->complete_io(next_io, 0, io_error) != ctsIOStatus::ContinueIo);
                        continue;
                    }

                    /////////////////////////////////////////////////////////////
                    // No-Throw operations from here until end of try {} block
                    /////////////////////////////////////////////////////////////
                    char* io_buffer = next_io.buffer + next_io.buffer_offset;
                    if (IOTaskAction::Send == next_io.ioAction) {
                        if (!::WriteFile(reinterpret_cast<HANDLE>(socket), io_buffer, next_io.buffer_length, nullptr, pov)) {
                            io_error = ::GetLastError();
                        }
                    } else {
                        if (!::ReadFile(reinterpret_cast<HANDLE>(socket), io_buffer, next_io.buffer_length, nullptr, pov)) {
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
                        // decrement the IO count since it was not pended
                        io_count = shared_socket->decrement_io();
                        // call back to the socket that it failed to see if wants more IO
                        const wchar_t* Function = (IOTaskAction::Send == next_io.ioAction) ? L"WriteFile" : L"ReadFile";
                        PrintDebugInfo(L"\t\tIO Failed: %s (%d) [ctsReadWriteIocp]\n", Function, io_error);

                        ctsIOStatus protocol_status = shared_pattern->complete_io(next_io, 0, io_error);
                        io_done = (protocol_status != ctsIOStatus::ContinueIo);
                        switch (protocol_status) {
                            case ctsIOStatus::ContinueIo:
                                // the protocol wants to ignore the error and send more data
                                io_error = NO_ERROR;
                                io_done = false;
                                break;

                            case ctsIOStatus::CompletedIo:
                                // the protocol wants to ignore the error but is done with IO
                                io_error = NO_ERROR;
                                io_done = true;
                                break;

                            case ctsIOStatus::FailedIo:
                                // print the error on failure
                                ctsConfig::PrintErrorIfFailed(Function, io_error);
                                // the protocol acknoledged the failure - socket is done with IO
                                io_error = shared_pattern->get_last_error();
                                io_done = true;
                                break;

                            default:
                                ctl::ctAlwaysFatalCondition(L"ctsReadWriteIocp: unknown ctsSocket::IOStatus - %u\n", static_cast<unsigned>(protocol_status));
                        }
                    }
                }
            }
        } else {
            io_error = WSAECONNABORTED;
        }

        if (0 == io_count) {
            // complete the ctsSocket if we have no IO pended
            shared_socket->complete_state(io_error);
        }
    }
} // namespace
