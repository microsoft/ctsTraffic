/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <memory>
#include <exception>
// os headers
#include <windows.h>
#include <winsock2.h>
// ctl headers
#include <ctSockaddr.hpp>
#include <ctException.hpp>
// project headers
#include "ctsSocket.h"
#include "ctsConfig.h"

namespace ctsTraffic {
    //
    // ctsSimpleConnect makes *blocking* calls to connect
    // - callers should be careful to ensure that this is really what they want
    // - since it will not scale out well
    //
    // Its intended use is either for UDP sockets, or for very few concurrent connections
    //
    void ctsSimpleConnect(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        int error;
        try {
            const auto socket_lock(ctsGuardSocket(shared_socket));
            const auto socket = socket_lock.get();
            if (socket != INVALID_SOCKET) {
                const ctl::ctSockaddr& targetAddress = shared_socket->target_address();
                const ctl::ctSockaddr local_addr;

                error = ctsConfig::SetPreConnectOptions(socket);
                if (error != NO_ERROR) {
                    throw ctl::ctException(error, L"ctsConfig::SetPreConnectOptions", false);
                }

                if (0 != ::connect(socket, targetAddress.sockaddr(), targetAddress.length())) {
                    error = ::WSAGetLastError();
                    ctsConfig::PrintErrorIfFailed(L"connect", error);
                } else {
                    // set the local address
                    auto local_addr_len = local_addr.length();
                    if (0 == ::getsockname(socket, local_addr.sockaddr(), &local_addr_len)) {
                        shared_socket->set_local_address(local_addr);
                    }
                }

                ctsConfig::PrintNewConnection(local_addr, targetAddress);
            } else {
                error = WSAECONNABORTED;
            }
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            error = ctl::ctErrorCode(e);
        }

        shared_socket->complete_state(error);
    }

}
