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
// project headers
#include "ctsSocket.h"
#include "ctsConfig.h"
// wil headers always included last
#include <wil/stl.h>
#include <wil/resource.h>

namespace ctsTraffic
{
    static std::atomic_signed_lock_free g_bindCounter{};
    static std::atomic_signed_lock_free g_targetCounter{};
    static std::atomic_signed_lock_free g_portCounter{};

    // ReSharper disable once CppInconsistentNaming
    void ctsWSASocket(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
    {
        auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        USHORT nextPort = 0;
        if (ctsConfig::g_configSettings->LocalPortHigh != 0 && ctsConfig::g_configSettings->LocalPortLow != 0)
        {
            const auto portCounter = g_portCounter.fetch_add(1) + 1;
            nextPort = static_cast<uint16_t>(portCounter % (ctsConfig::g_configSettings->LocalPortHigh - ctsConfig::g_configSettings->LocalPortLow + 1)) + ctsConfig::g_configSettings->LocalPortLow;
        }
        else
        {
            nextPort = ctsConfig::g_configSettings->LocalPortLow;
        }

        //
        // Find a bind and target address by moving to the next address in the respective vectors
        //
        ctl::ctSockaddr localAddr;
        if (ctsConfig::g_configSettings->ListenAddresses.empty() && !ctsConfig::g_configSettings->TargetAddressStrings.empty())
        {
            // if we are connecting by name, always bind to the ephemeral IPv6 address
            localAddr.reset(AF_INET6, ctl::ctSockaddr::AddressType::Any);
        }
        else
        {
            const auto bindSize = ctsConfig::g_configSettings->BindAddresses.size();
            auto socketCounter = g_bindCounter.fetch_add(1) + 1;
            localAddr = ctsConfig::g_configSettings->BindAddresses[socketCounter % bindSize];
        }

        localAddr.setPort(nextPort);

        ctl::ctSockaddr targetAddr;
        if (!ctsConfig::g_configSettings->TargetAddresses.empty())
        {
            //
            // the target address family must match the bind address family
            // - ctsConfig guarantees that at least address families will match with at least one address in bind and target vectors
            //
            const auto targetSize = ctsConfig::g_configSettings->TargetAddresses.size();
            auto socketCounter = g_targetCounter.fetch_add(1) + 1;
            targetAddr = ctsConfig::g_configSettings->TargetAddresses[socketCounter % targetSize];
            while (targetAddr.family() != localAddr.family())
            {
                socketCounter = g_targetCounter.fetch_add(1) + 1;
                targetAddr = ctsConfig::g_configSettings->TargetAddresses[socketCounter % targetSize];
            }
        }

        auto socket = INVALID_SOCKET;
        uint32_t gle = 0;
        const auto* functionName = "CreateSocket";
        try
        {
            switch (ctsConfig::g_configSettings->Protocol)
            {
            case ctsConfig::ProtocolType::TCP:
                socket = ctsConfig::CreateSocket(localAddr.family(), SOCK_STREAM, IPPROTO_TCP, ctsConfig::g_configSettings->SocketFlags);
                break;

            case ctsConfig::ProtocolType::UDP:
                socket = ctsConfig::CreateSocket(localAddr.family(), SOCK_DGRAM, IPPROTO_UDP, ctsConfig::g_configSettings->SocketFlags);
                break;

            case ctsConfig::ProtocolType::NoProtocolSet:
                [[fallthrough]];
            default: // NOLINT(clang-diagnostic-covered-switch-default)
                ctsConfig::PrintErrorInfo(
                    L"Unknown socket protocol (%u)",
                    static_cast<unsigned>(ctsConfig::g_configSettings->Protocol));
                gle = WSAEINVAL;
            }
        }
        catch (const wil::ResultException& e)
        {
            gle = ctsConfig::Win32FromHresult(e.GetErrorCode());
        }
        catch (...)
        {
            gle = WSAENOBUFS;
        }

        if (NO_ERROR == gle)
        {
            functionName = "SetPreBindOptions";
            gle = ctsConfig::SetPreBindOptions(socket, localAddr);
        }

        if (NO_ERROR == gle)
        {
            // setting the socket option to support dual-mode sockets must be done before calling bind
            // must enable dual-mode sockets before calling WSAConnectByName, so it can connect to either IPv4 or IPv6 addresses
            if (ctsConfig::g_configSettings->ListenAddresses.empty() && !ctsConfig::g_configSettings->TargetAddressStrings.empty())
            {
                PRINT_DEBUG_INFO(L"\t\tEnabling Dual-mode sockets\n");
                constexpr DWORD ipv6_only = FALSE;
                if (0 != setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&ipv6_only), sizeof ipv6_only))
                {
                    gle = WSAGetLastError();
                    ctsConfig::PrintErrorIfFailed("setsockopt(IPV6_V6ONLY)", gle);
                }
            }
        }

        if (NO_ERROR == gle)
        {
            functionName = "bind";

            if (0 == nextPort)
            {
                if (SOCKET_ERROR == bind(socket, localAddr.sockaddr(), localAddr.length()))
                {
                    gle = WSAGetLastError();
                }
            }
            else
            {
                // sleep up to 5 seconds to allow TCP to clean up its internal state
                constexpr auto bindRetryCount = 5;
                for (auto bindRetry = 0; bindRetry < bindRetryCount; ++bindRetry)
                {
                    if (SOCKET_ERROR == bind(socket, localAddr.sockaddr(), localAddr.length()))
                    {
                        gle = WSAGetLastError();
                        if (WSAEADDRINUSE == gle)
                        {
                            constexpr uint32_t bindRetrySleepMs = 1000;
                            PRINT_DEBUG_INFO(L"\t\tctsWSASocket : bind failed on attempt %d, sleeping %u ms.\n", bindRetry + 1, bindRetrySleepMs);
                            Sleep(bindRetrySleepMs);
                        }
                    }
                    else
                    {
                        // succeeded - exit the loop
                        gle = NO_ERROR;
                        PRINT_DEBUG_INFO(L"\t\tctsWSASocket : bind succeeded on attempt %d\n", bindRetry + 1);
                        break;
                    }
                }
            }
        }

        // store whatever values we have: for accurate logging
        sharedSocket->SetSocket(socket);
        sharedSocket->SetLocalSockaddr(localAddr);
        sharedSocket->SetRemoteSockaddr(targetAddr);

        if (0 == gle)
        {
            sharedSocket->CompleteState(NO_ERROR);
        }
        else
        {
            ctsConfig::PrintErrorIfFailed(functionName, gle);
            sharedSocket->CompleteState(gle);
        }
    }
} // namespace
