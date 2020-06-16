/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// parent header
#include "ctsMediaStreamServerConnectedSocket.h"
// cpp headers
#include <memory>
#include <functional>
#include <utility>
// os headers
#include <Windows.h>
#include <WinSock2.h>
// ctl headers
#include <ctException.hpp>
#include <ctSockaddr.hpp>
#include <ctString.hpp>
#include <ctTimer.hpp>
// project headers
#include "ctsWinsockLayer.h"

using namespace ctl;

namespace ctsTraffic
{
    ctsMediaStreamServerConnectedSocket::ctsMediaStreamServerConnectedSocket(
        std::weak_ptr<ctsSocket> _weak_socket,
        SOCKET _sending_socket,
        ctSockaddr _remote_addr,
        ctsMediaStreamConnectedSocketIoFunctor _io_functor) :
        weak_socket(std::move(_weak_socket)),
        io_functor(std::move(_io_functor)),
        sending_socket(_sending_socket),
        remote_addr(std::move(_remote_addr)),
        connect_time(ctTimer::ctSnapQpcInMillis())
    {
        task_timer.reset(CreateThreadpoolTimer(ctsMediaStreamTimerCallback, this, ctsConfig::Settings->PTPEnvironment));
        THROW_LAST_ERROR_IF(!task_timer);
    }

    ctsMediaStreamServerConnectedSocket::~ctsMediaStreamServerConnectedSocket() noexcept
    {
        // stop the TP before letting the d'tor delete any member objects
        task_timer.reset();
    }

    void ctsMediaStreamServerConnectedSocket::schedule_task(const ctsIOTask& _task) noexcept
    {
        const auto shared_socket(weak_socket.lock());
        if (shared_socket)
        {
            const auto lock = object_guard.lock();
            _Analysis_assume_lock_acquired_(object_guard);
            if (_task.time_offset_milliseconds < 2)
            {
                // in this case, immediately schedule the WSASendTo
                next_task = _task;
                ctsMediaStreamTimerCallback(nullptr, this, nullptr);

            }
            else
            {
                FILETIME ftDueTime(ctTimer::ctConvertMillisToRelativeFiletime(_task.time_offset_milliseconds));
                // assign the next task *and* schedule the timer while in *this object lock
                next_task = _task;
                SetThreadpoolTimer(task_timer.get(), &ftDueTime, 0, 0);
            }
            _Analysis_assume_lock_released_(object_guard);
        }
    }

    void ctsMediaStreamServerConnectedSocket::complete_state(unsigned long _error_code) const noexcept
    {
        std::shared_ptr<ctsSocket> shared_socket(weak_socket);
        if (shared_socket)
        {
            shared_socket->complete_state(_error_code);
        }
    }

    VOID CALLBACK ctsMediaStreamServerConnectedSocket::ctsMediaStreamTimerCallback(PTP_CALLBACK_INSTANCE, PVOID _context, PTP_TIMER) noexcept
    {
        auto* this_ptr = static_cast<ctsMediaStreamServerConnectedSocket*>(_context);

        // take a lock on the ctsSocket for this 'connection'
        const auto shared_socket = this_ptr->weak_socket.lock();
        if (!shared_socket)
        {
            return;
        }

        // hold a reference on the iopattern
        auto shared_pattern = shared_socket->io_pattern();

        const auto lock = this_ptr->object_guard.lock();
        _Analysis_assume_lock_acquired_(this_ptr->object_guard);

        // post the queued IO, then loop sending/scheduling as necessary
        auto send_results = this_ptr->io_functor(this_ptr);
        auto status = shared_pattern->complete_io(
            this_ptr->next_task,
            send_results.bytes_transferred,
            send_results.error_code);

        ctsIOTask current_task = this_ptr->next_task;
        while (ctsIOStatus::ContinueIo == status && current_task.ioAction != IOTaskAction::None)
        {
            current_task = shared_pattern->initiate_io();

            switch (current_task.ioAction)
            {
                case IOTaskAction::Send:
                    this_ptr->next_task = current_task;
                    // if the time is less than two ms., we need to catch up on sends
                    // - post the sendto immediately instead of scheduling for later
                    if (this_ptr->next_task.time_offset_milliseconds < 2)
                    {
                        send_results = this_ptr->io_functor(this_ptr);
                        status = shared_pattern->complete_io(
                            this_ptr->next_task,
                            send_results.bytes_transferred,
                            send_results.error_code);
                    }
                    else
                    {
                        this_ptr->schedule_task(this_ptr->next_task);
                    }
                    break;

                case IOTaskAction::None:
                    // done until the next send completes
                    break;

                default:
                    FAIL_FAST_MSG(
                        "Unexpected task action returned from initiate_io - %u (dt %p ctsTraffic::ctsIOTask)",
                        static_cast<unsigned long>(current_task.ioAction),
                        &current_task);
            }
        }

        if (ctsIOStatus::FailedIo == status)
        {
            // if IO has failed, we won't have anymore scheduled in the future
            // - deliberately stop processing now
            // must guarantee a failed error code is returned
            unsigned long returned_status = send_results.error_code;
            if (0 == returned_status)
            {
                returned_status = WSAECONNABORTED;
            }

            try
            {
                ctsConfig::PrintErrorInfo(
                    ctl::ctString::ctFormatString("MediaStream Server socket (%ws) was indicated Failed IO from the protocol - aborting this stream",
                        this_ptr->remote_addr.WriteCompleteAddress().c_str()).c_str());
            }
            catch (...)
            {
                // best effort
            }

            this_ptr->complete_state(returned_status);

        }
        else if (ctsIOStatus::CompletedIo == status)
        {
            try
            {
                PrintDebugInfo(
                    L"\t\tctsMediaStreamServerConnectedSocket socket (%ws) has completed its stream - closing this 'connection'\n",
                    this_ptr->remote_addr.WriteCompleteAddress().c_str());
            }
            catch (...)
            {
                // best effort
            }

            this_ptr->complete_state(send_results.error_code);
        }
        _Analysis_assume_lock_released_(this_ptr->object_guard);
    }
} // namespace