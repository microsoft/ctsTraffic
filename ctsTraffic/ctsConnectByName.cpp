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

// using wil::network to pull in all necessary networking headers
#include <wil/network.h>

// project headers
#include "ctsSocket.h"
#include "ctsConfig.h"

static int64_t g_targetCounter = 0LL;

namespace ctsTraffic
{
    //
    // ctsConnectByName makes *blocking* calls to connect
    // - callers should be careful to ensure that this is really what they want
    // - since it will not scale out well
    //
    // Its intended use is either for UDP sockets, or for very few concurrent connections
    //
    void ctsConnectByName(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
    {
        // attempt to get a reference to the socket
        const auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        auto error = 0ul;
        const auto socketReference(sharedSocket->AcquireSocketLock());
        const auto socket = socketReference.GetSocket();
        if (socket != INVALID_SOCKET)
        {
            try
            {
                error = ctsConfig::SetPreConnectOptions(socket);
                if (error != NO_ERROR)
                {
                    ctsConfig::PrintErrorIfFailed("SetPreConnectOptions", error);
                    THROW_WIN32(error);
                }

                const auto targetSize = ctsConfig::g_configSettings->TargetAddressStrings.size();
                const auto connectCounter = ctl::ctMemoryGuardIncrement(&g_targetCounter);
                const auto& targetAddr = ctsConfig::g_configSettings->TargetAddressStrings[connectCounter % targetSize];

                // read the local sockaddr - e.g. if we needed to bind locally
                socket_address localAddr(sharedSocket->GetLocalSockaddr());
                socket_address remoteAddr;
                DWORD localAddrLength = localAddr.size();
                DWORD remoteAddrLength = remoteAddr.size();

                PRINT_DEBUG_INFO(L"\t\tWSAConnectByName to %ws : %u\n",
                                 targetAddr.c_str(),
                                 ctsConfig::g_configSettings->Port);
                if (!WSAConnectByNameW(
                    socket,
                    const_cast<LPWSTR>(targetAddr.c_str()),
                    const_cast<LPWSTR>(std::to_wstring(ctsConfig::g_configSettings->Port).c_str()),
                    &localAddrLength,
                    localAddr.sockaddr(),
                    &remoteAddrLength,
                    remoteAddr.sockaddr(),
                    nullptr,
                    nullptr))
                {
                    error = WSAGetLastError();
                    ctsConfig::PrintErrorIfFailed("WSAConnectByName", error);
                    THROW_WIN32(error);
                }

                PRINT_DEBUG_INFO(
                    L"\t\tWSAConnectByName completed successfully - localAddress (%ws), remoteAddress (%ws)\n",
                    localAddr.format_complete_address().c_str(),
                    remoteAddr.format_complete_address().c_str());

                sharedSocket->SetLocalSockaddr(localAddr);
                sharedSocket->SetRemoteSockaddr(remoteAddr);
                ctsConfig::PrintNewConnection(localAddr, remoteAddr);
            }
            CATCH_LOG()
        }
        else
        {
            error = WSAECONNABORTED;
        }

        sharedSocket->CompleteState(error);
    }
}
