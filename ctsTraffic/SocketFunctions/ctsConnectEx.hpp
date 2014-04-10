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
#include <string>
// os headers
#include <winsock2.h>
#include <Windows.h>
// ctl headers
#include <ctSocketExtensions.hpp>
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
#include <ctException.hpp>
#include <ctTimer.hpp>
// project headers
#include "ctsSocket.h"


namespace ctsTraffic {

    static inline
    void ctsConnectExIoCompletionCallback(
        OVERLAPPED* _overlapped,
        std::weak_ptr<ctsSocket> _socket,
        const ctl::ctSockaddr _targetAddress
        ) throw()
    {
        auto shared_socket_lock(_socket.lock());
        ctsSocket* socket_lock = shared_socket_lock.get();
        if (socket_lock == nullptr) {
            // underlying socket went away - nothing to do now
            return;
        }

        int gle = 0;
        SOCKET socket = socket_lock->lock_socket();
        if (socket == INVALID_SOCKET) {
            gle = WSAECONNABORTED;
        } else {
            // a null OVERLAPPED means this is called directly when completed inline
            if (_overlapped) {
                DWORD transferred, flags;
                if (!::WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags)) {
                    gle = ::WSAGetLastError();
                }
            }

            // update the socket context if completed successfully - necessary with ConnectEx
            if (0 == gle) {
                int err = ::setsockopt(socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
                ctl::ctFatalCondition(
                    (err != 0),
                    L"setsockopt(SO_UPDATE_CONNECT_CONTEXT) failed [%d], connected socket [%lld]",
                    ::WSAGetLastError(),
                    static_cast<long long>(socket));
            }
        }

        ctsConfig::PrintErrorIfFailed(L"ConnecteEx", gle);

        if (NO_ERROR == gle) {
            // get the local addr
            ctl::ctSockaddr local_addr;
            int local_addr_len = local_addr.length();
            if (0 == ::getsockname(socket, local_addr.sockaddr(), &local_addr_len)) {
                socket_lock->set_local(local_addr);
            }
        }

        // unlock before completing the socket state
        socket_lock->unlock_socket();
        socket_lock->complete_state(gle);

        // print results after completing state
        if (NO_ERROR == gle) {
            ctsConfig::PrintNewConnection(_targetAddress);
        }
    }

    inline
    void ctsConnectEx(std::weak_ptr<ctsSocket> _socket) throw()
    {
        auto shared_socket_lock(_socket.lock());
        ctsSocket* socket_lock = shared_socket_lock.get();
        if (socket_lock == nullptr) {
            // underlying socket went away - nothing to do now
            return;
        }

        // attempt to get a reference to the socket
        int error = NO_ERROR;
        SOCKET socket = socket_lock->lock_socket();
        if (socket != INVALID_SOCKET) {
            try {
                const ctl::ctSockaddr& targetAddress = socket_lock->get_target();
                error = ctsConfig::SetPreConnectOptions(socket);
                if (error != 0) {
                    throw ctl::ctException(error, L"ctsConfig::SetPreConnectOptions", false);
                }

                // get a new IO request from the socket's TP
                std::shared_ptr<ctl::ctThreadIocp> connect_iocp = socket_lock->thread_pool();
                OVERLAPPED* pov = connect_iocp->new_request(ctsConnectExIoCompletionCallback, _socket, targetAddress);

                if (!ctl::ctConnectEx(
                        socket,
                        targetAddress.sockaddr(),
                        targetAddress.length(),
                        NULL,
                        0,
                        NULL,
                        pov)) {
                    error = ::WSAGetLastError();
                    if (ERROR_IO_PENDING == error) {
                        // pended is not failure
                        error = NO_ERROR;
                    } else {
                        // must call cancel() on the IOCP TP if the IO call fails
                        connect_iocp->cancel_request(pov);
                    }

                } else if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP) {
                    // if inline completions are enabled, the IOCP won't be queued the completion
                    connect_iocp->cancel_request(pov);
                    // directly invoke the callback to complete the IO
                    // - with a nullptr OVERLAPPED to indicate it's already completed
                    ctsConnectExIoCompletionCallback(nullptr, _socket, targetAddress);
                }

                ctsConfig::PrintErrorIfFailed(L"ConnectEx", error);
                if (NO_ERROR == error) {
                    ctsConfig::PrintDebug(L"\t\tConnecting to %s\n", targetAddress.writeCompleteAddress().c_str());
                }
            }
            catch (const ctl::ctException& e) {
                ctsConfig::PrintException(e);
                error = e.why();
            }
            catch (const std::bad_alloc& e) {
                ctsConfig::PrintException(e);
                error = ERROR_NOT_ENOUGH_MEMORY;
            }
        } else {
            error = WSAENOTSOCK;
        }

        // unlock before completing the socket state
        socket_lock->unlock_socket();

        // complete on failure
        // - inline completions will have completed when the callback function was called directly
        if (error != NO_ERROR) {
            socket_lock->complete_state(error);
        }
    }
}
