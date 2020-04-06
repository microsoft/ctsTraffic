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
#include <windows.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include <ctSockaddr.hpp>
#include <ctThreadIocp.hpp>

namespace ctsTraffic {
    class ctsMediaStreamServerListeningSocket {
    private:
        static const size_t RecvBufferSize = 1024;

        std::shared_ptr<ctl::ctThreadIocp> thread_iocp;

        mutable wil::critical_section socket_lock;
        _Requires_lock_held_(socket_lock) wil::unique_socket listening_socket;

        const ctl::ctSockaddr listening_addr;
        std::array<char, RecvBufferSize> recv_buffer{};
        DWORD recv_flags{};
        ctl::ctSockaddr remote_addr;
        int remote_addr_len{};

        void recv_completion(OVERLAPPED* _ov) noexcept;

    public:
        ctsMediaStreamServerListeningSocket(
            wil::unique_socket&& _listening_socket,
            ctl::ctSockaddr _listening_addr);

        ~ctsMediaStreamServerListeningSocket() noexcept;

        SOCKET get_socket() const noexcept;

        ctl::ctSockaddr get_address() const noexcept;

        void initiate_recv() noexcept;

        // non-copyable
        ctsMediaStreamServerListeningSocket(const ctsMediaStreamServerListeningSocket&) = delete;
        ctsMediaStreamServerListeningSocket& operator=(const ctsMediaStreamServerListeningSocket&) = delete;
        ctsMediaStreamServerListeningSocket(ctsMediaStreamServerListeningSocket&&) = delete;
        ctsMediaStreamServerListeningSocket& operator=(ctsMediaStreamServerListeningSocket&&) = delete;
    };
}
