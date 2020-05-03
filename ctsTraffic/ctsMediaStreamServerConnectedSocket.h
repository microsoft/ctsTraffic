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
// os headers
#include <Windows.h>
#include <WinSock2.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include <ctSockaddr.hpp>
// project headers
#include "ctsIOTask.hpp"
#include "ctsSocket.h"
#include "ctsWinsockLayer.h"

namespace ctsTraffic
{
    class ctsMediaStreamServerConnectedSocket;
    typedef std::function<wsIOResult(ctsMediaStreamServerConnectedSocket*)> ctsMediaStreamConnectedSocketIoFunctor;

    class ctsMediaStreamServerConnectedSocket
    {
    private:
        // the CS is mutable so we can take a lock / release a lock in const methods
        mutable wil::critical_section object_guard;
        _Guarded_by_(object_guard) ctsIOTask next_task;

        wil::unique_threadpool_timer task_timer;

        // this weak_socket is the weak reference to the ctsSocket tracked by ctsSocketState & ctsSocketBroker
        // used to complete the state when finished and take a shared_ptr when needing to take a reference
        const std::weak_ptr<ctsSocket> weak_socket;

        // invoked to do actual IO on the socket
        const ctsMediaStreamConnectedSocketIoFunctor io_functor;

        // sending_socket is a shared socket from the datagram server
        // that (potentially) many connected datagram sockets will send from
        // thus it's not owned by this class
        const SOCKET sending_socket;
        const ctl::ctSockaddr remote_addr;

        long long sequence_number = 0LL;
        const long long connect_time = 0LL;

    public:
        ctsMediaStreamServerConnectedSocket(
            std::weak_ptr<ctsSocket> _weak_socket,
            SOCKET _sending_socket,
            ctl::ctSockaddr _remote_addr,
            ctsMediaStreamConnectedSocketIoFunctor _io_functor);

        ~ctsMediaStreamServerConnectedSocket() noexcept;

        const ctl::ctSockaddr& get_remote_address() const noexcept
        {
            return remote_addr;
        }

        SOCKET get_sending_socket() const noexcept
        {
            return sending_socket;
        }
        long long get_startTime() const noexcept
        {
            return connect_time;
        }

        ctsIOTask get_nextTask() const noexcept
        {
            const auto lock = object_guard.lock();
            return next_task;
        }

        long long increment_sequence() noexcept
        {
            return InterlockedIncrement64(&sequence_number);
        }

        void schedule_task(const ctsIOTask& _task) noexcept;

        void complete_state(unsigned long _error_code) const noexcept;

        // non-copyable
        ctsMediaStreamServerConnectedSocket(const ctsMediaStreamServerConnectedSocket&) = delete;
        ctsMediaStreamServerConnectedSocket& operator=(const ctsMediaStreamServerConnectedSocket&) = delete;
        ctsMediaStreamServerConnectedSocket(ctsMediaStreamServerConnectedSocket&&) = delete;
        ctsMediaStreamServerConnectedSocket& operator=(ctsMediaStreamServerConnectedSocket&&) = delete;

    private:
        static VOID CALLBACK ctsMediaStreamTimerCallback(PTP_CALLBACK_INSTANCE, PVOID _context, PTP_TIMER) noexcept;
    };
}