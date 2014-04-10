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
    ///
    /// ctsSimpleConnect makes *blocking* calls to connect
    /// - callers should be careful to ensure that this is really what they want
    /// - since it will not scale out well
    ///
    /// Its intended use is either for UDP sockets, or for very few concurrent connections
    ///

    inline
    void ctsSimpleConnect(std::weak_ptr<ctsSocket> _socket) throw()
    {
        // attempt to get a reference to the socket
        auto shared_socket_lock(_socket.lock());
        ctsSocket* socket_lock = shared_socket_lock.get();
        if (socket_lock == nullptr) {
            // the underlying socket went away - nothing to do
            return;
        }

        int error = NO_ERROR;
        SOCKET s = socket_lock->lock_socket();
        if (s != INVALID_SOCKET) {
            try {
                const ctl::ctSockaddr& targetAddress = socket_lock->get_target();
                error = ctsConfig::SetPreConnectOptions(s);
                if (error != NO_ERROR) {
                    throw ctl::ctException(error, L"ctsConfig::SetPreConnectOptions", false);
                }

                if (0 != ::connect(s, targetAddress.sockaddr(), targetAddress.length())) {
                    error = ::WSAGetLastError();
                    ctsConfig::PrintErrorIfFailed(L"connect", error);
                } else {
                    // get the local addr
                    ctl::ctSockaddr local_addr;
                    int local_addr_len = local_addr.length();
                    if (0 == ::getsockname(s, local_addr.sockaddr(), &local_addr_len)) {
                        socket_lock->set_local(local_addr);
                    }
                }
            }
            catch (const ctl::ctException& e) {
                ctsConfig::PrintException(e);
                ctl::ctFatalCondition(
                    (0 == e.why()),
                    L"ctException (%p) thrown with a zero error code", &e);
                error = e.why();
            }
            catch (const std::bad_alloc& e) {
                ctsConfig::PrintException(e);
                error = WSAENOBUFS;
            }
        } else {
            error = WSAENOTSOCK;
        }

        // unlock before completing the socket state
        socket_lock->unlock_socket();
        socket_lock->complete_state(error);
    }

}