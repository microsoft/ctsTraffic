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
#include <Windows.h>
#include <winsock2.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctSocketExtensions.hpp>
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
#include <ctException.hpp>
// project headers
#include "ctsSocket.h"
#include "ctsSocketGuard.hpp"

namespace ctsTraffic {

    static inline
    void ctsConnectExIoCompletionCallback(
        OVERLAPPED* _overlapped,
        const std::weak_ptr<ctsSocket>& _weak_socket,
        const ctl::ctSockaddr& _targetAddress
        ) NOEXCEPT
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        int gle = 0;
        ctl::ctSockaddr local_addr;
        // scope to the socket lock
        {
            auto socket_lock(ctsGuardSocket(shared_socket));
            SOCKET socket = socket_lock.get();
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
                if (NO_ERROR == gle) {
                    int err = ::setsockopt(socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
                    ctl::ctFatalCondition(
                        (err != 0),
                        L"setsockopt(SO_UPDATE_CONNECT_CONTEXT) failed [%d], connected socket [%lld]",
                        ::WSAGetLastError(),
                        static_cast<long long>(socket));
                }
            }

            ctsConfig::PrintErrorIfFailed(L"ConnecteEx", gle);
            if (NO_ERROR == gle) {
                // store the local addr of the connection
                int local_addr_len = local_addr.length();
                if (0 == ::getsockname(socket, local_addr.sockaddr(), &local_addr_len)) {
                    shared_socket->set_local_address(local_addr);
                }
            }
        }

        shared_socket->complete_state(gle);
        // print results after completing state
        if (NO_ERROR == gle) {
            ctsConfig::PrintNewConnection(local_addr, _targetAddress);
        }
    }

    inline void ctsConnectEx(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        int error = NO_ERROR;
        // scope to the socket lock
        {
            auto socket_lock(ctsGuardSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (socket != INVALID_SOCKET) {
                try {
                    const ctl::ctSockaddr& targetAddress = shared_socket->target_address();
                    error = ctsConfig::SetPreConnectOptions(socket);
                    if (error != NO_ERROR) {
                        throw ctl::ctException(error, L"ctsConfig::SetPreConnectOptions", false);
                    }

                    // get a new IO request from the socket's TP
                    const std::shared_ptr<ctl::ctThreadIocp>& connect_iocp = shared_socket->thread_pool();
                    OVERLAPPED* pov = connect_iocp->new_request(
                        [_weak_socket, targetAddress] (OVERLAPPED* _ov) 
                        { ctsConnectExIoCompletionCallback(_ov, _weak_socket, targetAddress); });

                    if (!ctl::ctConnectEx(socket, targetAddress.sockaddr(), targetAddress.length(), nullptr, 0, nullptr, pov)) {
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
                        ctsConnectExIoCompletionCallback(nullptr, _weak_socket, targetAddress);
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
                error = WSAECONNABORTED;
            }
        }

        // complete on failure
        // - inline completions will have completed when the callback function was called directly
        if (error != NO_ERROR) {
            shared_socket->complete_state(error);
        }
    }
}
