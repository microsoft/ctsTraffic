/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <memory>
#include <string>

// using wil::networking to pull in all necessary networking headers
#include <wil/networking.h>

// ctl headers
#include <ctThreadIocp.hpp>
// project headers
#include "ctsSocket.h"

namespace ctsTraffic
{
    static void ctsConnectExIoCompletionCallback(
        OVERLAPPED* overlapped,
        const std::weak_ptr<ctsSocket>& weakSocket,
        const socket_address& targetAddress) noexcept
    {
        const auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        auto gle = 0;
        const auto socketReference(sharedSocket->AcquireSocketLock());
        const auto socket = socketReference.GetSocket();
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
            const auto err = setsockopt(socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
            FAIL_FAST_IF_MSG(
                err != 0,
                "setsockopt(SO_UPDATE_CONNECT_CONTEXT) failed [%d], connected socket [%lld]",
                WSAGetLastError(), static_cast<int64_t>(socketReference.GetSocket()));
        }

        ctsConfig::PrintErrorIfFailed("ConnectEx", gle);

        socket_address localAddr;
        if (NO_ERROR == gle)
        {
            // store the local addr of the connection
            int localAddrLen = socket_address::length;
            if (0 == getsockname(socket, localAddr.sockaddr(), &localAddrLen))
            {
                sharedSocket->SetLocalSockaddr(localAddr);
            }
        }

        sharedSocket->CompleteState(gle);
        // print results after completing state
        if (NO_ERROR == gle)
        {
            ctsConfig::PrintNewConnection(localAddr, targetAddress);
        }
    }

    void ctsConnectEx(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
    {
        const auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        uint32_t error = 0;
        try
        {
            const auto socketReference(sharedSocket->AcquireSocketLock());
            const auto socket = socketReference.GetSocket();
            if (socket != INVALID_SOCKET)
            {
                const socket_address& targetAddress = sharedSocket->GetRemoteSockaddr();
                error = ctsConfig::SetPreConnectOptions(socket);
                THROW_IF_WIN32_ERROR_MSG(error, "ctsConfig::SetPreConnectOptions");

                // get a new IO request from the socket's TP
                const std::shared_ptr<ctl::ctThreadIocp>& connectIocp = sharedSocket->GetIocpThreadpool();

                OVERLAPPED* pOverlapped = connectIocp->new_request(
                    [weakSocket, targetAddress](OVERLAPPED* pCallbackOverlapped) noexcept
                    {
                        ctsConnectExIoCompletionCallback(pCallbackOverlapped, weakSocket, targetAddress);
                    });

                if (!ctsConfig::SocketFunctions().ConnectEx(
                    socket,
                    targetAddress.sockaddr(),
                    socket_address::length,
                    nullptr,
                    0,
                    nullptr,
                    pOverlapped))
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
                        connectIocp->cancel_request(pOverlapped);
                    }
                }
                else if (ctsConfig::g_configSettings->Options & ctsConfig::OptionType::HandleInlineIocp)
                {
                    // if inline completions are enabled, the IOCP won't be queued the completion
                    connectIocp->cancel_request(pOverlapped);
                    // directly invoke the callback to complete the IO
                    // - with a nullptr OVERLAPPED to indicate it's already completed
                    ctsConnectExIoCompletionCallback(nullptr, weakSocket, targetAddress);
                }

                ctsConfig::PrintErrorIfFailed("ConnectEx", error);
                if (NO_ERROR == error)
                {
                    PRINT_DEBUG_INFO(L"\t\tConnecting to %ws\n", targetAddress.write_complete_address().c_str());
                }
            }
            else
            {
                error = WSAECONNABORTED;
            }
        }
        catch (...)
        {
            error = ctsConfig::PrintThrownException();
        }

        // complete on failure
        // - inline completions will have completed when the callback function was called directly
        if (error != NO_ERROR)
        {
            sharedSocket->CompleteState(error);
        }
    }
}
