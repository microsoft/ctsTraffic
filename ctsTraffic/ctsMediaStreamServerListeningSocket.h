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
// using wil::network to pull in all necessary networking headers
#include <wil/network.h>
// ctl headers
#include <ctThreadIocp.hpp>
// project headers
#include "ctsConfig.h"

namespace ctsTraffic
{
    class ctsMediaStreamServerListeningSocket
    {
    private:
        static constexpr size_t c_recvBufferSize = 1024;

        std::shared_ptr<ctl::ctThreadIocp> m_threadIocp;

        mutable wil::critical_section m_listeningsocketLock{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
        _Requires_lock_held_(m_listeningsocketLock) wil::unique_socket m_listeningSocket;

        const socket_address m_listeningAddr;
        std::array<char, c_recvBufferSize> m_recvBuffer{};
        DWORD m_recvFlags{};
        socket_address m_remoteAddr;
        int m_remoteAddrLen{};
        bool m_priorFailureWasConectionReset = false;

        void RecvCompletion(OVERLAPPED* pOverlapped) noexcept;

    public:
        ctsMediaStreamServerListeningSocket(
            wil::unique_socket&& listeningSocket,
            const socket_address& listeningAddr);

        ~ctsMediaStreamServerListeningSocket() noexcept;

        SOCKET GetSocket() const noexcept;

        socket_address GetListeningAddress() const noexcept;

        void InitiateRecv() noexcept;

        // non-copyable
        ctsMediaStreamServerListeningSocket(const ctsMediaStreamServerListeningSocket&) = delete;
        ctsMediaStreamServerListeningSocket& operator=(const ctsMediaStreamServerListeningSocket&) = delete;
        ctsMediaStreamServerListeningSocket(ctsMediaStreamServerListeningSocket&&) = delete;
        ctsMediaStreamServerListeningSocket& operator=(ctsMediaStreamServerListeningSocket&&) = delete;
    };
}
