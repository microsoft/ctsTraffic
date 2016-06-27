/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

#include <array>
#include <memory>

#include <windows.h>

#include "ctSockaddr.hpp"
#include "ctThreadIocp.hpp"
#include "ctHandle.hpp"

namespace ctsTraffic {
    class ctsMediaStreamServerListeningSocket {
    private:
        static const size_t RecvBufferSize = 1024;
        mutable CRITICAL_SECTION object_guard;

        /// members must have access protected
        _Guarded_by_(object_guard)
        std::shared_ptr<ctl::ctThreadIocp> thread_iocp;
        _Guarded_by_(object_guard)
        std::array<char, RecvBufferSize> recv_buffer;
        _Guarded_by_(object_guard)
        ctl::ctScopedSocket socket;
        _Guarded_by_(object_guard)
        ctl::ctSockaddr listening_addr;

        // remote addr, length, and flags are updated on each recvfrom()
        _Guarded_by_(object_guard)
        ctl::ctSockaddr remote_addr;
        _Guarded_by_(object_guard)
        int remote_addr_len;
        _Guarded_by_(object_guard)
        DWORD recv_flags;

        void recv_completion(OVERLAPPED* _ov) NOEXCEPT;

    public:
        ctsMediaStreamServerListeningSocket(
            ctl::ctScopedSocket&& _listening_socket,
            const ctl::ctSockaddr& _listening_addr);

        ~ctsMediaStreamServerListeningSocket() NOEXCEPT;

        SOCKET get_socket() const NOEXCEPT;

        ctl::ctSockaddr get_address() const NOEXCEPT;

        void reset() NOEXCEPT;

        void initiate_recv() NOEXCEPT;

        // non-copyable
        ctsMediaStreamServerListeningSocket(const ctsMediaStreamServerListeningSocket&) = delete;
        ctsMediaStreamServerListeningSocket& operator=(const ctsMediaStreamServerListeningSocket&) = delete;
    };
}
