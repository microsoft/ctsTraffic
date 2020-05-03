/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <memory>
// os headers
#include <Windows.h>
#include <winsock2.h>
// ctl headers
#include <ctSocketExtensions.hpp>
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
#include <ctException.hpp>
// project headers
#include "ctsSocket.h"

namespace ctsTraffic
{

    static void ctsConnectExIoCompletionCallback(
        OVERLAPPED* overlapped,
        const std::weak_ptr<ctsSocket>& weak_socket,
        const ctl::ctSockaddr& target_address) noexcept
    {
        auto shared_socket(weak_socket.lock());
        if (!shared_socket)
        {
            return;
        }

        int gle = 0;
        const ctl::ctSockaddr local_addr;
        const auto socket_ref(shared_socket->socket_reference());
        const SOCKET socket = socket_ref.socket();
        if (socket == INVALID_SOCKET)
        {
            gle = WSAECONNABORTED;
        }

        if (NO_ERROR == gle)
        {
            // a null OVERLAPPED means this is called directly when completed inline
            if (overlapped)
            {
                DWORD transferred;
                DWORD flags;
                if (!WSAGetOverlappedResult(socket, overlapped, &transferred, FALSE, &flags))
                {
                    gle = WSAGetLastError();
                }
            }
        }
        // update the socket context if completed successfully - necessary with ConnectEx
        if (NO_ERROR == gle)
        {
            const int err = setsockopt(socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
            FAIL_FAST_IF_MSG(
                err != 0,
                "setsockopt(SO_UPDATE_CONNECT_CONTEXT) failed [%d], connected socket [%lld]",
                WSAGetLastError(), static_cast<long long>(socket_ref.socket()));
        }

        ctsConfig::PrintErrorIfFailed("ConnecteEx", gle);

        if (NO_ERROR == gle)
        {
            // store the local addr of the connection
            int local_addr_len = local_addr.length();
            if (0 == getsockname(socket, local_addr.sockaddr(), &local_addr_len))
            {
                shared_socket->set_local_address(local_addr);
            }
        }

        shared_socket->complete_state(gle);
        // print results after completing state
        if (NO_ERROR == gle)
        {
            ctsConfig::PrintNewConnection(local_addr, target_address);
        }
    }

    void ctsConnectEx(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket)
        {
            return;
        }

        int error = 0;
        try
        {
            const auto socket_ref(shared_socket->socket_reference());
            const SOCKET socket = socket_ref.socket();
            if (socket != INVALID_SOCKET)
            {
                const ctl::ctSockaddr& targetAddress = shared_socket->target_address();
                error = ctsConfig::SetPreConnectOptions(socket);
                if (error != NO_ERROR)
                {
                    throw ctl::ctException(error, L"ctsConfig::SetPreConnectOptions", false);
                }

                // get a new IO request from the socket's TP
                const std::shared_ptr<ctl::ctThreadIocp>& connect_iocp = shared_socket->thread_pool();
                OVERLAPPED* pov = connect_iocp->new_request(
                    [_weak_socket, targetAddress](OVERLAPPED* _ov) noexcept
                    { ctsConnectExIoCompletionCallback(_ov, _weak_socket, targetAddress); });

                if (!ctl::ctConnectEx(socket, targetAddress.sockaddr(), targetAddress.length(), nullptr, 0, nullptr, pov))
                {
                    error = WSAGetLastError();
                    if (ERROR_IO_PENDING == error)
                    {
                        // pended is not failure
                        error = NO_ERROR;
                    }
                    else
                    {
                        // must call cancel() on the IOCP TP if the IO call fails
                        connect_iocp->cancel_request(pov);
                    }

                }
                else if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP)
                {
                    // if inline completions are enabled, the IOCP won't be queued the completion
                    connect_iocp->cancel_request(pov);
                    // directly invoke the callback to complete the IO
                    // - with a nullptr OVERLAPPED to indicate it's already completed
                    ctsConnectExIoCompletionCallback(nullptr, _weak_socket, targetAddress);
                }

                ctsConfig::PrintErrorIfFailed("ConnectEx", error);
                if (NO_ERROR == error)
                {
                    PrintDebugInfo(L"\t\tConnecting to %ws\n", targetAddress.WriteCompleteAddress().c_str());
                }
            }
            else
            {
                error = WSAECONNABORTED;
            }
        }
        catch (const std::exception& e)
        {
            ctsConfig::PrintException(e);
            error = ctl::ctErrorCode(e);
        }

        // complete on failure
        // - inline completions will have completed when the callback function was called directly
        if (error != NO_ERROR)
        {
            shared_socket->complete_state(error);
        }
    }
}
