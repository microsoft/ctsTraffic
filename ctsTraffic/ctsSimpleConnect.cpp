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
#include <WinSock2.h>
// ctl headers
#include <ctSockaddr.hpp>
// project headers
#include "ctsSocket.h"
#include "ctsConfig.h"

namespace ctsTraffic
{
//
// ctsSimpleConnect makes *blocking* calls to connect
// - callers should be careful to ensure that this is really what they want
// - since it will not scale out well
//
// Its intended use is either for UDP sockets, or for very few concurrent connections
//
void ctsSimpleConnect(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
{
    // attempt to get a reference to the socket
    const auto sharedSocket(weakSocket.lock());
    if (!sharedSocket)
    {
        return;
    }

    auto error = 0;
    const auto socketReference(sharedSocket->AcquireSocketLock());
    const auto socket = socketReference.GetSocket();
    if (socket != INVALID_SOCKET)
    {
        const ctl::ctSockaddr& targetAddress = sharedSocket->GetRemoteSockaddr();

        if (0 != connect(socket, targetAddress.sockaddr(), targetAddress.length()))
        {
            error = WSAGetLastError();
            ctsConfig::PrintErrorIfFailed("connect", error);
        }
        else
        {
            // set the local address
            ctl::ctSockaddr localAddr;
            auto localAddrLen = localAddr.length();
            if (0 == getsockname(socket, localAddr.sockaddr(), &localAddrLen))
            {
                sharedSocket->SetLocalSockaddr(localAddr);
            }

            ctsConfig::SetPostConnectOptions(socket, targetAddress);

            ctsConfig::PrintNewConnection(localAddr, targetAddress);
        }
    }
    else
    {
        error = WSAECONNABORTED;
    }

    sharedSocket->CompleteState(error);
}
}
