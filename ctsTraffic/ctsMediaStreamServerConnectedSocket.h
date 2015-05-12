/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

#include <memory>

#include <Windows.h>
#include <WinSock2.h>

#include "ctsIOTask.hpp"
#include "ctsSocket.h"

#include "ctSockaddr.hpp"

namespace ctsTraffic {
    class ctsMediaStreamServerConnectedSocket {
    private:
        // the CS is mutable so we can take a lock / release a lock in const methods
        mutable CRITICAL_SECTION object_guard;
        PTP_TIMER task_timer;

        _Guarded_by_(object_guard)
        SOCKET sending_socket;

        _Guarded_by_(object_guard)
        std::weak_ptr<ctsSocket> weak_socket;
        _Guarded_by_(object_guard)
        ctl::ctSockaddr remote_addr;
        _Guarded_by_(object_guard)
        ctsIOTask next_task;

        _Interlocked_
        long long sequence_number;

        const long long connect_time;

    public:
        ctsMediaStreamServerConnectedSocket(std::weak_ptr<ctsSocket> _weak_socket, SOCKET _s, const ctl::ctSockaddr& _addr);

        ~ctsMediaStreamServerConnectedSocket() NOEXCEPT;

        void reset() NOEXCEPT;

        _Acquires_lock_(object_guard)
        SOCKET socket_lock() const NOEXCEPT;

        _Releases_lock_(object_guard)
        void socket_release() const NOEXCEPT;

        ctl::ctSockaddr get_address() const NOEXCEPT;

        long long get_startTime() const NOEXCEPT;

        long long increment_sequence() NOEXCEPT;

        void schedule_task(const ctsIOTask _task) NOEXCEPT;

        std::shared_ptr<ctsSocket> reference_ctsSocket() NOEXCEPT;

        // non-copyable, no default c'tor
        ctsMediaStreamServerConnectedSocket() = delete;
        ctsMediaStreamServerConnectedSocket(const ctsMediaStreamServerConnectedSocket&) = delete;
        ctsMediaStreamServerConnectedSocket& operator=(const ctsMediaStreamServerConnectedSocket&) = delete;

    private:
        static VOID CALLBACK ctsMediaStreamTimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER);
    };
}