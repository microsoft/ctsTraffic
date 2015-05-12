/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include "ctsMediaStreamServerConnectedSocket.h"
#include "ctsMediaStreamServer.h"
#include "ctException.hpp"
#include "ctTimer.hpp"
#include "ctLocks.hpp"
#include "ctVersionConversion.hpp"

using namespace ctl;

namespace ctsTraffic {
    ctsMediaStreamServerConnectedSocket::ctsMediaStreamServerConnectedSocket(std::weak_ptr<ctsSocket> _weak_socket, SOCKET _s, const ctSockaddr& _addr) :
        object_guard(),
        task_timer(nullptr),
        sending_socket(_s),
        weak_socket(_weak_socket),
        remote_addr(_addr),
        next_task(),
        sequence_number(0LL),
        connect_time(ctTimer::snap_qpc_as_msec())
    {
        if (!::InitializeCriticalSectionEx(&object_guard, 4000, 0)) {
            throw ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsMediaStreamServer", false);
        }

        task_timer = ::CreateThreadpoolTimer(ctsMediaStreamTimerCallback, this, ctsConfig::Settings->PTPEnvironment);
        if (nullptr == task_timer) {
            auto gle = ::GetLastError();
            ::DeleteCriticalSection(&object_guard);
            throw ctException(gle, L"CreateThreadpoolTimer", L"ctsMediaStreamServer", false);
        }
    }

    ctsMediaStreamServerConnectedSocket::~ctsMediaStreamServerConnectedSocket() NOEXCEPT
    {
        // stop the TP before deleting the CS
        ::SetThreadpoolTimer(task_timer, nullptr, 0, 0);
        ::WaitForThreadpoolTimerCallbacks(task_timer, TRUE);
        ::CloseThreadpoolTimer(task_timer);

        ::DeleteCriticalSection(&object_guard);
    }

    void ctsMediaStreamServerConnectedSocket::reset() NOEXCEPT
    {
        // this object does not "own" this socket thus we are not closing it here
        // - it's owned by the listening object
        ctAutoReleaseCriticalSection lock_object(&object_guard);
        sending_socket = INVALID_SOCKET;
    }

    _Acquires_lock_(object_guard)
    SOCKET ctsMediaStreamServerConnectedSocket::socket_lock() const NOEXCEPT
    {
        ::EnterCriticalSection(&object_guard);
        return sending_socket;
    }

    _Releases_lock_(object_guard)
    void ctsMediaStreamServerConnectedSocket::socket_release() const NOEXCEPT
    {
        ::LeaveCriticalSection(&object_guard);
    }

    ctSockaddr ctsMediaStreamServerConnectedSocket::get_address() const NOEXCEPT
    {
        return remote_addr;
    }

    long long ctsMediaStreamServerConnectedSocket::get_startTime() const NOEXCEPT
    {
        return connect_time;
    }

    long long ctsMediaStreamServerConnectedSocket::increment_sequence() NOEXCEPT
    {
        return ctMemoryGuardIncrement(&sequence_number);
    }
    
    void ctsMediaStreamServerConnectedSocket::schedule_task(const ctsIOTask _task) NOEXCEPT
    {
        auto shared_socket(this->weak_socket.lock());
        if (shared_socket) {
            if (_task.time_offset_milliseconds < 1) {
                // in this case, immediately schedule the WSASendTo
                ctAutoReleaseCriticalSection lock_object(&this->object_guard);
                this->next_task = _task;
                ctsMediaStreamServerConnectedSocket::ctsMediaStreamTimerCallback(nullptr, this, nullptr);
            } else {
                FILETIME ftDueTime(ctTimer::convert_msec_relative_filetime(_task.time_offset_milliseconds));
                // assign the next task *and* schedule the timer while in *this object lock
                ctAutoReleaseCriticalSection lock_object(&this->object_guard);
                this->next_task = _task;
                ::SetThreadpoolTimer(this->task_timer, &ftDueTime, 0, 0);
            }
        }
    }

    std::shared_ptr<ctsSocket> ctsMediaStreamServerConnectedSocket::reference_ctsSocket() NOEXCEPT
    {
        return this->weak_socket.lock();
    }
        
    VOID CALLBACK ctsMediaStreamServerConnectedSocket::ctsMediaStreamTimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER)
    {
        ctsMediaStreamServerConnectedSocket* this_ptr = reinterpret_cast<ctsMediaStreamServerConnectedSocket*>(_context);

        // pair <BytesTransferred, Error>
        typedef std::pair<unsigned long, unsigned long> SendResults;

        // stateless lambda just to capture the functionality of posting WSASendTo
        // - as this is called multiple places within this function
        auto PostSendTo = [] (ctsMediaStreamServerConnectedSocket* this_ptr) -> SendResults {
            int error = WSA_OPERATION_ABORTED;
            unsigned bytes_transferred = 0;

            SOCKET socket = this_ptr->socket_lock();
            if (socket != INVALID_SOCKET) {
                if (ctsIOTask::BufferType::UdpConnectionId == this_ptr->next_task.buffer_type) {
                    // making a synchronous call
                    DWORD bytes_sent;
                    WSABUF wsabuf;
                    wsabuf.buf = this_ptr->next_task.buffer;
                    wsabuf.len = this_ptr->next_task.buffer_length;
                    if (SOCKET_ERROR == ::WSASendTo(socket, &wsabuf, 1, &bytes_sent, 0, this_ptr->remote_addr.sockaddr(), this_ptr->remote_addr.length(), nullptr, nullptr)) {
                        error = ::WSAGetLastError();
                    } else {
                        bytes_transferred = bytes_sent;
                        error = NO_ERROR;
                    }

                } else {
                    auto seq_number = this_ptr->increment_sequence();

#ifdef TESTING_RESEND
                    if (0 == seq_number % 5) {
                        ctsConfig::PrintDebug(L"********* TESTING ***** SKIPPING EVERY 5 SEQUENCE NUMBERS\n");
                        bytes_transferred = this_ptr->next_task.buffer_length;
                        error = NO_ERROR;

                    } else {
#endif
                        ctsConfig::PrintDebug(
                            L"\t\tctsMediaStreamServer sending seq number %lld (%lu bytes)\n",
                            seq_number,
                            this_ptr->next_task.buffer_length);

                        ctsMediaStreamSendRequests sending_requests(
                            this_ptr->next_task.buffer_length, // total bytes to send
                            seq_number,
                            this_ptr->next_task.buffer);

                        for (auto& send_request : sending_requests) {
                            // making a synchronous call
                            DWORD bytes_sent;
                            if (SOCKET_ERROR == ::WSASendTo(socket, send_request.data(), static_cast<DWORD>(send_request.size()), &bytes_sent, 0, this_ptr->remote_addr.sockaddr(), this_ptr->remote_addr.length(), nullptr, nullptr)) {
                                error = ::WSAGetLastError();
                                if (WSAEMSGSIZE == error) {
                                    unsigned long bytes_requested = 0;
                                    // iterate across each WSABUF* in the array
                                    for (auto& wasbuf : send_request) {
                                        bytes_requested += wasbuf.len;
                                    }
                                    ctsConfig::PrintErrorInfo(
                                        L"[%.3f] WSASendTo(%Iu, seq %lld, %s) failed with WSAEMSGSIZE : attempted to send datagram of size %u bytes\n",
                                        ctsConfig::GetStatusTimeStamp(),
                                        socket,
                                        seq_number,
                                        this_ptr->remote_addr.writeCompleteAddress().c_str(),
                                        bytes_requested);
                                } else {
                                    ctsConfig::PrintErrorInfo(
                                        L"[%.3f] WSASendTo(%Iu, seq %lld, %s) failed [%d]\n",
                                        ctsConfig::GetStatusTimeStamp(),
                                        socket,
                                        seq_number,
                                        this_ptr->remote_addr.writeCompleteAddress().c_str(),
                                        error);
                                }
                                // break out early if send fails
                                break;

                            } else {
                                bytes_transferred += bytes_sent;
                                error = NO_ERROR;
                            }
                        }
#ifdef TESTING_RESEND
                    }
#endif
                }
            }
            this_ptr->socket_release();

            return SendResults(bytes_transferred, error);
        }; // end of lambda definition

        // take a lock on the ctsSocket for this 'connection'
        auto shared_socket = this_ptr->weak_socket.lock();
        if (!shared_socket) {
            // socket is already gone - remove it from the impl and exit
            ctsMediaStreamServerImpl::remove_socket(this_ptr->remote_addr, WSAECONNABORTED);
            return;
        }
        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());

        ctAutoReleaseCriticalSection socket_lock(&this_ptr->object_guard);

        // post a send, then loop sending/scheduling as necessary
        auto send_results = PostSendTo(this_ptr);
        auto status = shared_pattern->complete_io(
            this_ptr->next_task,
            std::get<0>(send_results),
            std::get<1>(send_results));

        ctsIOTask current_task = this_ptr->next_task;
        while (ctsIOStatus::ContinueIo == status && IOTaskAction::None != current_task.ioAction) {
            current_task = shared_pattern->initiate_io();
            if (IOTaskAction::Send == current_task.ioAction) {
                this_ptr->next_task = current_task;
                // if the time is less than one ms., we need to catch up on sends
                // - post the sendto immediately instead of scheduling for later
                if (this_ptr->next_task.time_offset_milliseconds < 1) {
                    send_results = PostSendTo(this_ptr);
                    status = shared_pattern->complete_io(
                        this_ptr->next_task,
                        std::get<0>(send_results),
                        std::get<1>(send_results));
                } else {
                    this_ptr->schedule_task(this_ptr->next_task);
                }
            }
        }
    }
} // namespace