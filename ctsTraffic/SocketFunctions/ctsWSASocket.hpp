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
#include <cassert>
// os headers
#include <winsock2.h>
#include <mstcpip.h> // for the fast-path ioctl
#include <windows.h>
// ctl headers
#include <ctLocks.hpp>
// project headers
#include "ctsSocket.h"
#include "ctsConfig.h"


namespace ctsTraffic {

    static long long s_BindCounter = 0LL;
    static long long s_TargetCounter = 0LL;
    static long long s_PortCounter = 0LL;

    inline
    void ctsWSASocket(std::weak_ptr<ctsSocket> _socket) throw()
    {
        auto shared_socket_lock(_socket.lock());
        ctsSocket* socket_lock = shared_socket_lock.get();
        if (socket_lock == nullptr) {
            // underlying socket went away - nothing to do now
            return;
        }

        USHORT next_port = 0;
        if (ctsConfig::Settings->LocalPortHigh != 0 && ctsConfig::Settings->LocalPortLow != 0) {
            auto port_counter = ctl::ctMemoryGuardIncrement(&s_PortCounter);
            next_port = static_cast<USHORT>((port_counter % (ctsConfig::Settings->LocalPortHigh - ctsConfig::Settings->LocalPortLow + 1)) + ctsConfig::Settings->LocalPortLow);
        } else {
            next_port = ctsConfig::Settings->LocalPortLow;
        }

        //
        // Find a bind and target address by moving to the next address in the respective vectors
        //
        auto bind_size = ctsConfig::Settings->BindAddresses.size();
        auto socket_counter = ctl::ctMemoryGuardIncrement(&s_BindCounter);
        ctl::ctSockaddr local_addr(ctsConfig::Settings->BindAddresses[socket_counter % bind_size]);
        local_addr.setPort(next_port);

        ctl::ctSockaddr target_addr;
        if (!ctsConfig::Settings->TargetAddresses.empty()) {
            //
            // the target address family must match the bind address family
            // - ctsConfig guarantees that at least address families will match with at least one address in bind and target vectors
            //
            auto target_size = ctsConfig::Settings->TargetAddresses.size();
            socket_counter = ctl::ctMemoryGuardIncrement(&s_TargetCounter);
            target_addr = ctsConfig::Settings->TargetAddresses[socket_counter % target_size];
            while (target_addr.family() != local_addr.family()) {
                socket_counter = ctl::ctMemoryGuardIncrement(&s_TargetCounter);
                target_addr = ctsConfig::Settings->TargetAddresses[socket_counter % target_size];
            }
        }

        wchar_t* function = nullptr;
        SOCKET s = INVALID_SOCKET;
        int gle = 0;
        switch (ctsConfig::Settings->Protocol) {
            case ctsConfig::ProtocolType::TCP: {
                function = L"WSASocket";
                s = ::WSASocket(local_addr.family(), SOCK_STREAM, IPPROTO_TCP, NULL, 0, ctsConfig::Settings->SocketFlags);
                if (INVALID_SOCKET == s) {
                    gle = ::WSAGetLastError();
                }

                break;
            }

            case ctsConfig::ProtocolType::UDP: {
                function = L"WSASocket";
                s = ::WSASocket(local_addr.family(), SOCK_DGRAM, IPPROTO_UDP, NULL, 0, ctsConfig::Settings->SocketFlags);
                if (INVALID_SOCKET == s) {
                    gle = ::WSAGetLastError();
                }

                break;
            }

            default: {
                function = L"WSASocket";
                ctsConfig::PrintErrorIfFailed(L"Unknown socket protocol", ctsConfig::Settings->Protocol);
                gle = WSAEINVAL;
            }
        }

        if (NO_ERROR == gle) {
            function = L"SetPreBindOptions";
            gle = ctsConfig::SetPreBindOptions(s, local_addr);
        }

        if (NO_ERROR == gle) {
            function = L"bind";
            if (SOCKET_ERROR == ::bind(s, local_addr.sockaddr(), local_addr.length())) {
                gle = ::WSAGetLastError();
            }
        }

        if (0 == gle) {
            socket_lock->set_socket(s);
            socket_lock->set_local(local_addr);
            socket_lock->set_target(target_addr);
            socket_lock->complete_state(NO_ERROR);
        } else {
            ctsConfig::PrintErrorIfFailed(function, gle);
            socket_lock->complete_state(gle);
            if (s != INVALID_SOCKET) {
                ::closesocket(s);
            }
        }
    }

} // namespace

