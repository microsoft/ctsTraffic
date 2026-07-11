/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <array>
#include <memory>
// os headers
#include <Windows.h>
// ctl headers
#include <ctThreadIocp_base.hpp>
// project headers
#include "ctsConfig.h"
// wil headers always included last
#include <wil/stl.h>
#include <wil/network.h>

namespace ctsTraffic
{
class ctsMediaStreamServerListeningSocket
{
private:
    static constexpr size_t c_recvBufferSize = 1024;

    std::shared_ptr<ctl::ctThreadIocp_base> m_threadIocp;

    mutable wil::critical_section m_listeningSocketLock{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
    _Requires_lock_held_(m_listeningSocketLock) wil::unique_socket m_listeningSocket;

    // number of established connections accepted on this listening socket
    std::atomic<uint32_t> m_connectionCount{ 0 };

	const wil::network::socket_address m_listeningAddr;

    std::array<char, c_recvBufferSize> m_recvBuffer{};
    DWORD m_recvFlags{};

    wil::network::socket_address m_remoteAddr;
    int m_remoteAddrLen{};

    bool m_priorFailureWasConnectionReset = false;

    void RecvCompletion(OVERLAPPED* pOverlapped) noexcept;

public:
    // increment the per-listener accepted connection count
    void IncrementConnectionCount() noexcept { m_connectionCount.fetch_add(1, std::memory_order_relaxed); }

    // query the accepted connection count
    uint32_t GetConnectionCount() const noexcept { return m_connectionCount.load(std::memory_order_relaxed); }
    ctsMediaStreamServerListeningSocket(
        wil::unique_socket&& listeningSocket,
        const wil::network::socket_address& listeningAddr,
        std::shared_ptr<ctl::ctThreadIocp_base> threadIocp) noexcept;

    ~ctsMediaStreamServerListeningSocket() noexcept;

    SOCKET GetSocket() const noexcept;

    wil::network::socket_address GetListeningAddress() const noexcept;

    void InitiateRecv() noexcept;

    // non-copyable
    ctsMediaStreamServerListeningSocket(const ctsMediaStreamServerListeningSocket&) = delete;
    ctsMediaStreamServerListeningSocket& operator=(const ctsMediaStreamServerListeningSocket&) = delete;
    ctsMediaStreamServerListeningSocket(ctsMediaStreamServerListeningSocket&&) = delete;
    ctsMediaStreamServerListeningSocket& operator=(ctsMediaStreamServerListeningSocket&&) = delete;
};
}
