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
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
// ctl headers
#include <ctString.hpp>
// project headers
#include "ctsSocket.h"
#include "ctsConfig.h"

namespace ctsTraffic
{
    static long long s_BindCounter = 0LL;
    static long long s_TargetCounter = 0LL;
    static long long s_PortCounter = 0LL;

    void ctsWSASocket(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket)
        {
            return;
        }

        USHORT next_port = 0;
        if (ctsConfig::Settings->LocalPortHigh != 0 && ctsConfig::Settings->LocalPortLow != 0)
        {
            const auto port_counter = ctl::ctMemoryGuardIncrement(&s_PortCounter);
            next_port = static_cast<USHORT>(port_counter % (ctsConfig::Settings->LocalPortHigh - ctsConfig::Settings->LocalPortLow + 1)) + ctsConfig::Settings->LocalPortLow;
        }
        else
        {
            next_port = ctsConfig::Settings->LocalPortLow;
        }

        //
        // Find a bind and target address by moving to the next address in the respective vectors
        //
        const auto bind_size = ctsConfig::Settings->BindAddresses.size();
        auto socket_counter = ctl::ctMemoryGuardIncrement(&s_BindCounter);
        ctl::ctSockaddr local_addr(ctsConfig::Settings->BindAddresses[socket_counter % bind_size]);
        local_addr.SetPort(next_port);

        ctl::ctSockaddr target_addr;
        if (!ctsConfig::Settings->TargetAddresses.empty())
        {
            //
            // the target address family must match the bind address family
            // - ctsConfig guarantees that at least address families will match with at least one address in bind and target vectors
            //
            const auto target_size = ctsConfig::Settings->TargetAddresses.size();
            socket_counter = ctl::ctMemoryGuardIncrement(&s_TargetCounter);
            target_addr = ctsConfig::Settings->TargetAddresses[socket_counter % target_size];
            while (target_addr.family() != local_addr.family())
            {
                socket_counter = ctl::ctMemoryGuardIncrement(&s_TargetCounter);
                target_addr = ctsConfig::Settings->TargetAddresses[socket_counter % target_size];
            }
        }

        auto socket = INVALID_SOCKET;
        int gle = 0;
        PCSTR function_name = "CreateWSASocket";
        try
        {
            switch (ctsConfig::Settings->Protocol)
            {
                case ctsConfig::ProtocolType::TCP:
                    socket = ctsConfig::CreateSocket(local_addr.family(), SOCK_STREAM, IPPROTO_TCP, ctsConfig::Settings->SocketFlags);
                    break;

                case ctsConfig::ProtocolType::UDP:
                    socket = ctsConfig::CreateSocket(local_addr.family(), SOCK_DGRAM, IPPROTO_UDP, ctsConfig::Settings->SocketFlags);
                    break;

                case ctsConfig::ProtocolType::NoProtocolSet: // fall-through
                default:  // NOLINT(clang-diagnostic-covered-switch-default)
                    ctsConfig::PrintErrorInfo(
                        wil::str_printf<std::wstring>(L"Unknown socket protocol (%u)",
                            static_cast<unsigned>(ctsConfig::Settings->Protocol)).c_str());
                    gle = WSAEINVAL;
            }
        }
        catch (const wil::ResultException& e)
        {
            gle = ctsConfig::Win32FromHRESULT(e.GetErrorCode());
        }
        catch (...)
        {
            gle = WSAENOBUFS;
        }

        if (NO_ERROR == gle)
        {
            function_name = "SetPreBindOptions";
            gle = ctsConfig::SetPreBindOptions(socket, local_addr);
        }

        if (NO_ERROR == gle)
        {
            function_name = "bind";

            if (0 == next_port)
            {
                if (SOCKET_ERROR == bind(socket, local_addr.sockaddr(), local_addr.length()))
                {
                    gle = WSAGetLastError();
                }
            }
            else
            {
                // sleep up to 5 seconds to allow TCP to cleanup its internal state
                constexpr unsigned long BindRetryCount = 5;
                constexpr unsigned long BindRetrySleepMs = 1000;

                for (unsigned long bind_retry = 0; bind_retry < BindRetryCount; ++bind_retry)
                {
                    if (SOCKET_ERROR == bind(socket, local_addr.sockaddr(), local_addr.length()))
                    {
                        gle = WSAGetLastError();
                        if (WSAEADDRINUSE == gle)
                        {
                            PRINT_DEBUG_INFO(L"\t\tctsWSASocket : bind failed on attempt %lu, sleeping %lu ms.\n", bind_retry + 1, BindRetrySleepMs)
                            Sleep(BindRetrySleepMs);
                        }
                    }
                    else
                    {
                        // succeeded - exit the loop
                        gle = NO_ERROR;
                        PRINT_DEBUG_INFO(L"\t\tctsWSASocket : bind succeeded on attempt %lu\n", bind_retry + 1)
                        break;
                    }
                }
            }
        }

        // store whatever values we have: for accurate logging
        shared_socket->set_socket(socket);
        shared_socket->set_local_address(local_addr);
        shared_socket->set_target_address(target_addr);

        if (0 == gle)
        {
            shared_socket->complete_state(NO_ERROR);
        }
        else
        {
            ctsConfig::PrintErrorIfFailed(function_name, gle);
            shared_socket->complete_state(gle);
        }
    }

} // namespace
