/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <algorithm>
#include <vector>
#include <memory>
#include <string>
#include <exception>
// os headers
#include <winsock2.h>
#include <windows.h>
// ctl headers
#include <ctSockaddr.hpp>
#include <ctException.hpp>
#include <ctLocks.hpp>
#include <ctTimer.hpp>
#include <ctString.hpp>
#include <ctScopeGuard.hpp>
#include <ctThreadIocp.hpp>
#include <ctHandle.hpp>
// project headers
#include "ctsSocket.h"
#include "ctsConfig.h"
#include "ctsMediaStreamProtocol.hpp"

///
/// We register both of these functions with ctsConfig:
/// - ctsMediaStreamServerListener is the "Accepting" function
///   - it will complete 'Create' ctsSocket requests as clients send in START requests
///     it will be assumed that a client is unique when its IP:PORT are unique
///
/// - ctsMediaStreamServerIo is the 'IO' function
///   - it queues up IO to a central prioritized queue of work
///     since all IO is triggered to occur at a future point, the queue is sorted by work that comes soonest
///


namespace ctsTraffic {
    ///
    /// Function to pass to the cts 'accept' functor
    ///
    void ctsMediaStreamServerListener(std::weak_ptr<ctsSocket> _socket) throw();
    ///
    /// Function to pass to the cts 'IO' functor
    ///
    void ctsMediaStreamServerIo(std::weak_ptr<ctsSocket> _socket) throw();

    class ctsMediaStreamListeningSocket {
    private:
        mutable CRITICAL_SECTION object_guard;
        
        /// members must have access protected
        _Guarded_by_(object_guard)
        std::shared_ptr<ctl::ctThreadIocp> thread_iocp;
        _Guarded_by_(object_guard)
        std::array<char, 1024> recv_buffer;
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

    public:
        ctsMediaStreamListeningSocket(ctl::ctScopedSocket&& _listening_socket, const ctl::ctSockaddr& _listening_addr)
        : object_guard(),
          thread_iocp(std::make_shared<ctl::ctThreadIocp>(_listening_socket.get(), ctsConfig::Settings->PTPEnvironment)),
          recv_buffer(),
          socket(std::move(_listening_socket)),
          listening_addr(_listening_addr),
          remote_addr(),
          remote_addr_len(0),
          recv_flags(0)
        {
            ctl::ctFatalCondition(
                !!(ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP),
                L"ctsMediaStream sockets must not have HANDLE_INLINE_IOCP set on its datagram sockets");

            if (!::InitializeCriticalSectionEx(&object_guard, 4000, 0)) {
                throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsMediaStreamServer", false);
            }
        }

        ~ctsMediaStreamListeningSocket() throw()
        {
            // close the socket, then end the TP
            this->reset();
            this->thread_iocp.reset();
            ::DeleteCriticalSection(&object_guard);
        }

        /// initiates and OVERLAPPED recv to be completed in the thread pool thread_iocp
        void initiate_recv();

        SOCKET get_socket() const throw()
        {
            ctl::ctAutoReleaseCriticalSection object_lock(&this->object_guard);
            return this->socket.get();
        }

        ctl::ctSockaddr get_address() const throw()
        {
            ctl::ctAutoReleaseCriticalSection object_lock(&this->object_guard);
            return this->listening_addr;
        }

        void reset() throw()
        {
            ctl::ctAutoReleaseCriticalSection object_lock(&this->object_guard);
            this->socket.reset();
        }

        // non-copyable, no default c'tor
        ctsMediaStreamListeningSocket() = delete;
        ctsMediaStreamListeningSocket(const ctsMediaStreamListeningSocket&) = delete;
        ctsMediaStreamListeningSocket& operator=(const ctsMediaStreamListeningSocket&) = delete;
    };

    class ctsMediaStreamConnectedSocket {
    private:
        // the CS is mutable so we can take a lock / release a lock in const methods
        mutable CRITICAL_SECTION object_guard;
        PTP_TIMER task_timer;

        _Guarded_by_(object_guard)
        SOCKET sending_socket;

        _Guarded_by_(object_guard)
        std::weak_ptr<ctsSocket> cts_socket;
        _Guarded_by_(object_guard)
        ctl::ctSockaddr remote_addr;
        _Guarded_by_(object_guard)
        ctsIOTask next_task;

        _Interlocked_
        long long sequence_number;

        const long long connect_time;

    public:
        ctsMediaStreamConnectedSocket(std::weak_ptr<ctsSocket> _cts_socket, SOCKET _s, const ctl::ctSockaddr& _addr)
        : object_guard(),
          task_timer(nullptr),
          sending_socket(_s),
          cts_socket(_cts_socket),
          remote_addr(_addr),
          next_task(),
          sequence_number(0LL),
          connect_time(ctl::ctTimer::snap_qpc_msec())
        {
            if (!::InitializeCriticalSectionEx(&object_guard, 4000, 0)) {
                throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsMediaStreamServer", false);
            }

            task_timer = ::CreateThreadpoolTimer(ctsMediaStreamTimerCallback, this, ctsConfig::Settings->PTPEnvironment);
            if (nullptr == task_timer) {
                auto gle = ::GetLastError();
                ::DeleteCriticalSection(&object_guard);
                throw ctl::ctException(gle, L"CreateThreadpoolTimer", L"ctsMediaStreamServer", false);
            }
        }

        ~ctsMediaStreamConnectedSocket() throw()
        {
            // stop the TP before deleting the CS
            ::SetThreadpoolTimer(task_timer, nullptr, 0, 0);
            ::WaitForThreadpoolTimerCallbacks(task_timer, TRUE);
            ::CloseThreadpoolTimer(task_timer);

            ::DeleteCriticalSection(&object_guard);
        }

        void reset() throw()
        {
            // this object does not "own" this socket thus we are not closing it here
            // - it's owned by the listening object that is listening on it
            ctl::ctAutoReleaseCriticalSection lock_object(&object_guard);
            sending_socket = INVALID_SOCKET;
        }

        _Acquires_lock_(object_guard)
        SOCKET socket_lock() const throw()
        {
            ::EnterCriticalSection(&object_guard);
            return sending_socket;
        }

        _Releases_lock_(object_guard)
        void socket_release() const throw()
        {
            ::LeaveCriticalSection(&object_guard);
        }

        ctl::ctSockaddr get_address() const throw()
        {
            return remote_addr;
        }

        long long get_startTime() const throw()
        {
            return connect_time;
        }

        long long increment_sequence() throw()
        {
            return ctl::ctMemoryGuardIncrement(&sequence_number);
        }
        void schedule_task(const ctsIOTask _task) throw()
        {
            auto shared_socket(this->cts_socket.lock());
            if (shared_socket) {
                if (_task.time_offset_milliseconds < 1) {
                    // in this case, immediately schedule the WSASendTo
                    ctl::ctAutoReleaseCriticalSection lock_object(&this->object_guard);
                    this->next_task = _task;
                    ctsMediaStreamConnectedSocket::ctsMediaStreamTimerCallback(nullptr, this, nullptr);
                } else {
                    FILETIME ftDueTime(ctl::ctTimer::convert_msec_relative_filetime(_task.time_offset_milliseconds));
                    // assign the next task *and* schedule the timer while in *this object lock
                    ctl::ctAutoReleaseCriticalSection lock_object(&this->object_guard);
                    this->next_task = _task;
                    ::SetThreadpoolTimer(this->task_timer, &ftDueTime, 0, 0);
                }
            }
        }

        std::shared_ptr<ctsSocket> reference_ctsSocket() throw()
        {
            return this->cts_socket.lock();
        }

        // non-copyable, no default c'tor
        ctsMediaStreamConnectedSocket() = delete;
        ctsMediaStreamConnectedSocket(const ctsMediaStreamConnectedSocket&) = delete;
        ctsMediaStreamConnectedSocket& operator=(const ctsMediaStreamConnectedSocket&) = delete;

    private:
        static
        VOID CALLBACK ctsMediaStreamTimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER);
    };

    class ctsMediaStreamServerImpl {
    private:
        // ctsMediaStreamListeningSocket doesn't allow copies so using unique_ptr's to move them around
        std::vector<std::unique_ptr<ctsMediaStreamListeningSocket>> listening_sockets;

        CRITICAL_SECTION connected_object_guard;
        // ctsMediaStreamConnectedSocket doesn't allow copies so using unique_ptr's to move them around
        _Guarded_by_(connected_object_guard)
        std::vector<std::unique_ptr<ctsMediaStreamConnectedSocket>> connected_sockets;

        CRITICAL_SECTION awaiting_object_guard;
        // weak_ptr<> to ctsSocket objects ready to accept a connection
        _Guarded_by_(awaiting_object_guard)
        std::vector<std::weak_ptr<ctsSocket>> accepting_sockets;

        // endpoints that have been received from clients not yet matched to ctsSockets
        _Guarded_by_(awaiting_object_guard)
        std::vector<std::pair<SOCKET, ctl::ctSockaddr>> awaiting_endpoints;

    public:
        // non-copyable
        ctsMediaStreamServerImpl(const ctsMediaStreamServerImpl&) = delete;
        ctsMediaStreamServerImpl& operator=(const ctsMediaStreamServerImpl&) = delete;

    public:
        ctsMediaStreamServerImpl()
        : listening_sockets(),
          connected_object_guard(),
          connected_sockets(),
          awaiting_object_guard(),
          accepting_sockets(),
          awaiting_endpoints()
        {
            if (!::InitializeCriticalSectionEx(&connected_object_guard, 4000, 0)) {
                throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsMediaStreamServer", false);
            }
            if (!::InitializeCriticalSectionEx(&awaiting_object_guard, 4000, 0)) {
                auto gle = ::GetLastError();
                ::DeleteCriticalSection(&connected_object_guard);
                throw ctl::ctException(gle, L"InitializeCriticalSectionEx", L"ctsMediaStreamServer", false);
            }

            // 'listen' to each address
            for (const auto& addr : ctsConfig::Settings->ListenAddresses) {
                ctl::ctScopedSocket listening(::WSASocket(addr.family(), SOCK_DGRAM, IPPROTO_UDP, NULL, 0, ctsConfig::Settings->SocketFlags));
                if (INVALID_SOCKET == listening.get()) {
                    throw ctl::ctException(::WSAGetLastError(), L"socket", L"ctsMediaStreamServer", false);
                }

                int gle = ctsConfig::SetPreBindOptions(listening.get(), addr);
                if (gle != NO_ERROR) {
                    throw ctl::ctException(gle, L"SetPreBindOptions", L"ctsMediaStreamServer", false);
                }

                if (SOCKET_ERROR == ::bind(listening.get(), addr.sockaddr(), addr.length())) {
                    throw ctl::ctException(::WSAGetLastError(), L"bind", L"ctsMediaStreamServer", false);
                }

                SOCKET listening_socket_to_print(listening.get());
                this->listening_sockets.emplace_back(std::make_unique<ctsMediaStreamListeningSocket>(std::move(listening), addr));

                ctsConfig::PrintDebug(
                    L"\t\tctsMediaStreamServer - Receiving datagrams on %s (%Iu)\n",
                    addr.writeCompleteAddress().c_str(),
                    listening_socket_to_print);
            }

            if (this->listening_sockets.empty()) {
                throw std::exception("ctsMediaStreamServer invoked with no listening addresses specified");
            }

            // initiate the recv's in the 'listening' sockets
            for (auto& listener : this->listening_sockets) {
                listener->initiate_recv();
            }
        }

        ~ctsMediaStreamServerImpl()
        {
            /// first, safely set all sockets in the connected list to INVALID_SOCKET so they won't be used
            /// - which will allow us to subsequently closesocket() the underlying listening sockets
            ::EnterCriticalSection(&this->connected_object_guard);
            for (auto& socket : this->connected_sockets) {
                socket->reset();
            }
            ::LeaveCriticalSection(&this->connected_object_guard);

            /// close all listening sockets to release any pended Winsock calls
            for (auto& listener : this->listening_sockets) {
                listener.reset();
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Schedule the first IO on the specified ctsSocket
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void schedule_io(const std::weak_ptr<ctsSocket>& _socket, const ctsIOTask& _task)
        {
            auto shared_socket = _socket.lock();
            if (!shared_socket) {
                throw ctl::ctException(WSAENOTSOCK, L"ctsSocket already freed", L"ctsMediaStreamServer", false);
            }

            // must guard connected_sockets since we need to add it
            ctl::ctAutoReleaseCriticalSection lock_connected_object(&this->connected_object_guard);

            // find the matching connected_socket
            auto found_socket = std::find_if(
                std::begin(this->connected_sockets),
                std::end(this->connected_sockets),
                [&] (const std::unique_ptr<ctsMediaStreamConnectedSocket>& _connected_socket) {
                return (shared_socket->get_target() == _connected_socket->get_address());
            });
            if (found_socket == std::end(this->connected_sockets)) {
                ctsConfig::PrintDebug(
                    L"\t\tctsMediaStreamServer - failed to find the socket with remote address %s in our connected socket list",
                    shared_socket->get_target().writeCompleteAddress().c_str());
                throw ctl::ctException(ERROR_INVALID_DATA, L"ctsSocket was not found in the Connected Sockets", L"ctsMediaStreamServer", false);
            }

            (*found_socket)->schedule_task(_task);
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Process a new ctsSocket from the ctsSocketBroker
        /// - accept_socket takes the ctsSocket to create a new entry
        ///   which will create a corresponding ctsMediaStreamConnectedSocket in the process
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void accept_socket(const std::weak_ptr<ctsSocket>& _socket)
        {
            auto shared_socket(_socket.lock());
            if (shared_socket) {
                // need a writer lock to allow modifying accepting_sockets and awaiting_endpoints
                ctl::ctAutoReleaseCriticalSection lock_awaiting_object(&this->awaiting_object_guard);

                if (0 == this->awaiting_endpoints.size()) {
                    // just add it to our accepting sockets vector under the writer lock
                    this->accepting_sockets.push_back(_socket);

                } else {
                    auto waiting_endpoint = this->awaiting_endpoints.rbegin();

                    // must guard connected_sockets since we need to add it to the vector
                    // - scope to the lock
                    {
                        ctl::ctAutoReleaseCriticalSection lock_connected_object(&this->connected_object_guard);
                        this->connected_sockets.emplace_back(std::make_unique<ctsMediaStreamConnectedSocket>(
                            _socket, waiting_endpoint->first, waiting_endpoint->second));
                    }

                    // now complete the ctsSocket 'Create' request
                    // find the local address
                    auto found_socket = std::find_if(
                        this->listening_sockets.begin(),
                        this->listening_sockets.end(),
                        [&waiting_endpoint] (const std::unique_ptr<ctsMediaStreamListeningSocket>& _listener) {
                        return (_listener->get_socket() == waiting_endpoint->first);
                    });

                    ctl::ctFatalCondition(
                        (found_socket == this->listening_sockets.end()),
                        L"Could not find the socket (%Iu) in the waiting_endpoint from our listening sockets (%p)\n",
                        waiting_endpoint->first, &this->listening_sockets);

                    shared_socket->set_local((*found_socket)->get_address());
                    shared_socket->set_target(waiting_endpoint->second);
                    shared_socket->complete_state(NO_ERROR);

                    // if added to connected_sockets, can then safely remove it from the waiting endpoint
                    // - no longer touching the iterator waiting_endpoint
                    this->awaiting_endpoints.pop_back();
                }
            }
        }
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Process the removal of a connected socket once it is completed
        /// - remove_socket takes the remote address to find the socket
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void remove_socket(const ctl::ctSockaddr& _target_addr)
        {
            std::unique_ptr<ctsMediaStreamConnectedSocket> removed_socket;
            // scoping to the lock
            {
                ctl::ctAutoReleaseCriticalSection lock_connected_object(&this->connected_object_guard);

                auto found_socket = std::find_if(
                    std::begin(this->connected_sockets),
                    std::end(this->connected_sockets),
                    [&_target_addr] (const std::unique_ptr<ctsMediaStreamConnectedSocket>& _connected_socket) {
                    return _target_addr == _connected_socket->get_address();
                });
                // complete its ctsSocket and remove it from the connected socket list
                if (found_socket != std::end(this->connected_sockets)) {
                    // can't erase it while holding a lock, so move it out of the vector
                    // then erase that moved-from slot from the vector
                    removed_socket = std::move(*found_socket);
                    this->connected_sockets.erase(found_socket);

                } else {
                    ctsConfig::PrintErrorInfo(
                        L"[%.3f] ctsMediaStreamServer - no connected socket with remote address %s to process the Done request\n",
                        ctsConfig::GetStatusTimeStamp(),
                        _target_addr.writeCompleteAddress().c_str());
                }
            }

            // update the socket outside of the connected socket lock since we have finished modifying that vector
            if (removed_socket) {
                std::shared_ptr<ctsSocket> shared_socket(removed_socket->reference_ctsSocket());
                if (shared_socket) {
                    shared_socket->complete_state(0);
                }
            }
            // only after releasing the lock can we delete the removed ctsMediaStreamConnectedSocket
            // - calling reset isn't technically required since this object is about to go out of scope
            //   but calling it to reflect that the object is being deleted after leaving the connected_object_guard
            removed_socket.reset();
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Processes the incoming START request from the client
        /// - if we have a waiting ctsSocket to accept it, will add it to connected_sockets
        /// - else we'll queue it to awaiting_endpoints
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void start(const ctl::ctScopedSocket& _socket, const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _target_addr)
        {
            // before starting a socket, verify there is not already a connected socket with this same socket address
            // - scope the lock
            {
                ctl::ctAutoReleaseCriticalSection lock_connected_object(&this->connected_object_guard);

                auto found_socket = std::find_if(
                    std::begin(this->connected_sockets),
                    std::end(this->connected_sockets),
                    [&_target_addr] (const std::unique_ptr<ctsMediaStreamConnectedSocket>& _connected_socket) {
                    return _target_addr == _connected_socket->get_address();
                });

                if (found_socket != std::end(this->connected_sockets)) {
                    ctsConfig::PrintDebug(
                        L"ctsMediaStreamServer - socket with remote address %s asked to be Started but was already established\n",
                        _target_addr.writeCompleteAddress().c_str());
                    // return early if this was a duplicate request: this can happen if there is latency or drops
                    // between the client and server as they attempt to negotiating starting a new stream
                    return;
                }
            }

            // find a ctsSocket waiting to 'accept' a connection and complete it
            ctl::ctAutoReleaseCriticalSection lock_awaiting_object(&this->awaiting_object_guard);

            // walk through the list to find a socket that is still alive to take this connection
            bool added_connection = false;
            while (!this->accepting_sockets.empty()) {
                auto weak_instance = *this->accepting_sockets.rbegin();
                auto shared_instance = weak_instance.lock();
                if (shared_instance.get() != nullptr) {
                    // 'move' the accepting socket to connected
                    // - scope the lock
                    {
                        ctl::ctAutoReleaseCriticalSection lock_connected_object(&this->connected_object_guard);
                        this->connected_sockets.emplace_back(std::make_unique<ctsMediaStreamConnectedSocket>(
                            weak_instance, _socket.get(), _target_addr));
                    }

                    // verify is successfully added to connected_sockets before popping off accepting_sockets
                    added_connection = true;
                    this->accepting_sockets.pop_back();

                    // now complete the accepted ctsSocket back to the ctsSocketState
                    shared_instance->set_local(_local_addr);
                    shared_instance->set_target(_target_addr);
                    shared_instance->complete_state(0);

                    ctsConfig::PrintNewConnection(_target_addr);
                    break;
                }
            }

            // if we didn't find a waiting connection to accept it, queue it for when one arrives later
            if (!added_connection) {
                // only queue it if we aren't already waiting on this address
                this->awaiting_endpoints.emplace_back(_socket.get(), _target_addr);
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Process an incoming RESEND request
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void resend(const ctsMediaStreamMessage& _message, const ctl::ctSockaddr& _target_addr)
        {
            ctl::ctAutoReleaseCriticalSection lock_connected_object(&this->connected_object_guard);

            // find the connected socket to resend a datagram
            auto found_socket = std::find_if(
                std::begin(this->connected_sockets),
                std::end(this->connected_sockets),
                [&_target_addr] (const std::unique_ptr<ctsMediaStreamConnectedSocket>& _connected_socket) {
                return _target_addr == _connected_socket->get_address();
            });
            if (found_socket == std::end(this->connected_sockets)) {
                throw ctl::ctException(
                    ERROR_INVALID_DATA,
                    ctl::ctString::format_string(
                        L"ctsMediaStreamServer - socket with remote address %s asked to be Resend but was not found\n",
                        _target_addr.writeCompleteAddress().c_str()).c_str(),
                    L"ctsMediaStreamServer::resend",
                    true);

            }

            const std::unique_ptr<ctsMediaStreamConnectedSocket>& found_protected_socket = *found_socket;
            ctl::ctSockaddr target_addr(found_protected_socket->get_address());

            SOCKET s = found_protected_socket->socket_lock();
#pragma warning(suppress: 26110)   //  PREFast is getting confused with the scope guard
            ctlScopeGuard(releaseSocketLockOnExit, { found_protected_socket->socket_release(); });

            if (s != INVALID_SOCKET) {
                long long seq_number(ctl::ctMemoryGuardRead(&_message.sequence_number));

                ctsMediaStreamSendRequests sending_requests(
                    ctsConfig::GetMediaStream().FrameSizeBytes, // bytes to send
                    seq_number,
                    ctsIOPattern::AccessSharedBuffer());

                for (auto& send_request : sending_requests) {
                    DWORD bytes_sent;
                    if (SOCKET_ERROR == ::WSASendTo(s, send_request.data(), static_cast<DWORD>(send_request.size()), &bytes_sent, 0, target_addr.sockaddr(), target_addr.length(), nullptr, nullptr)) {
                        auto error = ::WSAGetLastError();
                        ctsConfig::PrintErrorInfo(
                            L"[%.3f] WSASendTo(%Iu, seq %lld, %s) for a RESEND request failed [%d]\n",
                            ctsConfig::GetStatusTimeStamp(),
                            s,
                            seq_number,
                            target_addr.writeCompleteAddress().c_str(),
                            error);
                        // break out early if send fails
                        break;
                    } else {
                        ctsConfig::PrintDebug(
                            L"\t\tctsMediaStreamServer RESEND sent %s seq number %lld (%lu bytes)\n",
                            target_addr.writeCompleteAddress().c_str(), _message.sequence_number, bytes_sent);
                    }
                }
            }
        }
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Singleton values used as the actual implementation for every 'connection'
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static ctsMediaStreamServerImpl* pimpl = nullptr;
    static INIT_ONCE InitImpl = INIT_ONCE_STATIC_INIT;
    static inline
    BOOL CALLBACK InitOnceImpl(PINIT_ONCE, PVOID, PVOID *)
    {
        try {
            pimpl = new ctsMediaStreamServerImpl();
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            return FALSE;
        }
        return TRUE;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called to 'accept' incoming connections
    /// - adds them to accepting_sockets
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamServerListener(std::weak_ptr<ctsSocket> _socket) throw()
    {
        try {
            if (!::InitOnceExecuteOnce(&InitImpl, InitOnceImpl, NULL, NULL)) {
                throw std::runtime_error("ctsMediaStreamServerListener could not be instantiated");
            }

            // ctsMediaStreamServerImpl will complete the ctsSocket object
            // when a client request comes in to be 'accepted'
            pimpl->accept_socket(_socket);
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            auto shared_socket(_socket.lock());
            if (shared_socket) {
                shared_socket->complete_state(ERROR_OUTOFMEMORY);
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called initiate IO on a datagram socket
    /// - the original ctsSocket is already in the connected_sockets vector
    /// - adding the next_io request to the IO queue
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamServerIo(std::weak_ptr<ctsSocket> _socket) throw()
    {
        ctsIOTask next_task;
        try {
            if (!::InitOnceExecuteOnce(&InitImpl, InitOnceImpl, NULL, NULL)) {
                throw std::runtime_error("ctsMediaStreamServerIo could not be instantiated");
            }

            auto shared_socket(_socket.lock());
            if (shared_socket) {
                do {
                    next_task = shared_socket->initiate_io();
                    if (next_task.ioAction != ctsIOTask::IOAction::None) {
                        pimpl->schedule_io(_socket, next_task);
                        // reset the IoAction after successfully scheduling
                        next_task.ioAction = ctsIOTask::IOAction::None;
                    }
                } while (next_task.ioAction != ctsIOTask::IOAction::None);
            }
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            auto exception_shared_socket(_socket.lock());
            if (exception_shared_socket) {
                if (next_task.ioAction != ctsIOTask::IOAction::None) {
                    // must complete any IO that was requested but not scheduled
                    exception_shared_socket->complete_io(next_task, 0, WSAENOBUFS);
                }
                exception_shared_socket->complete_state(ERROR_OUTOFMEMORY);
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Definitions for methods declared above
    /// - definitions are separated out as there are references between classes being defined which need to be deferred
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamListeningSocket::initiate_recv()
    {
        auto recv_iocp_lambda = [this] (OVERLAPPED* _ov) {
            // Cannot be holding the object_guard when calling into any pimpl-> methods
            // - will risk deadlocking the server
            // Will store the pimpl call to be made in this std function to be exeucted outside the lock
            std::function<void(void)> pimpl_operation;

            try {
                // scope to the object lock
                {
                    // must take the object lock before touching this->socket
                    ctl::ctAutoReleaseCriticalSection lock_object(&this->object_guard);

                    if (INVALID_SOCKET == this->socket.get()) {
                        // the listening socket was closed - just exit
                        return;
                    }

                    DWORD bytes_received;
                    if (!::WSAGetOverlappedResult(this->socket.get(), _ov, &bytes_received, FALSE, &this->recv_flags)) {
                        // recvfrom failed
                        auto gle = ::WSAGetLastError();
                        if (WSAECONNRESET == gle) {
                            // the remote endpoint is down - just remove this socket
                            ctsConfig::PrintErrorInfo(
                                L"[%.3f] ctsMediaStreamServer - WSARecvFrom failed as the prior WSASendTo(%s) failed with port unreachable\n",
                                ctsConfig::GetStatusTimeStamp(),
                                this->remote_addr.writeCompleteAddress().c_str());

                            // cannot hold the object lock when remove this object through the pimpl
                            pimpl_operation = ([&] () { pimpl->remove_socket(this->remote_addr); });

                        } else {
                            ctsConfig::PrintErrorInfo(
                                L"[%.3f] ctsMediaStreamServer - WSARecvFrom failed [%d]\n",
                                ctsConfig::GetStatusTimeStamp(),
                                ::WSAGetLastError());
                        }

                    } else {
                        ctsMediaStreamMessage message(ctsMediaStreamMessage::Extract(this->recv_buffer.data(), bytes_received));
                        switch (message.action) {
                            case ctsMediaStreamMessage::Action::START:
                                ctsConfig::PrintDebug(
                                    L"\t\tctsMediaStreamServer - processing START from %s\n",
                                    this->remote_addr.writeCompleteAddress().c_str());
#ifndef TESTING_IGNORE_START
                                // cannot hold the object lock when remove this object through the pimpl
                                // - all values must be passed to the lambda by value not by reference since they will be accessed outside the lock
                                pimpl_operation = ([&] () { pimpl->start(this->socket, this->listening_addr, this->remote_addr); });
#endif
                                break;

                            case ctsMediaStreamMessage::Action::RESEND:
                                ctsConfig::PrintDebug(
                                    L"\t\tctsMediaStreamServer - processing RESEND from %s - sending sequence number %lld\n",
                                    this->remote_addr.writeCompleteAddress().c_str(),
                                    ctl::ctMemoryGuardRead(&message.sequence_number));

                                // cannot hold the object lock when remove this object through the pimpl
                                pimpl_operation = ([&] () { pimpl->resend(message, this->remote_addr); });
                                break;

                            case ctsMediaStreamMessage::Action::DONE:
                                ctsConfig::PrintDebug(
                                    L"\t\tctsMediaStreamServer - processing DONE from %s\n",
                                    this->remote_addr.writeCompleteAddress().c_str());

                                // cannot hold the object lock when remove this object through the pimpl
                                pimpl_operation = ([&] () {pimpl->remove_socket(this->remote_addr); });
                                break;

                            default:
                                ctl::ctAlwaysFatalCondition(L"ctsMediaStreamServer - received an unexpected Action: %d (%p)\n", message.action, this->recv_buffer.data());
                        }
                    }
                }

                // now execute the stored call outside the lock but inside the try/catch
                pimpl_operation();
            }
            catch (const std::exception& e) {
                ctsConfig::PrintException(e);
            }

            this->initiate_recv();
        };

        // continue to try to post a recv if the call fails
        int error = SOCKET_ERROR;
        while (error != NO_ERROR) {
            ctl::ctAutoReleaseCriticalSection lock_socket(&this->object_guard);
            if (this->socket.get() != INVALID_SOCKET) {
                WSABUF wsabuf;
                wsabuf.buf = this->recv_buffer.data();
                wsabuf.len = static_cast<ULONG>(this->recv_buffer.size());
                ::ZeroMemory(this->recv_buffer.data(), this->recv_buffer.size());

                this->recv_flags = 0;
                this->remote_addr.reset();
                this->remote_addr_len = this->remote_addr.length();
                OVERLAPPED* pov = this->thread_iocp->new_request(recv_iocp_lambda);

                if (SOCKET_ERROR == ::WSARecvFrom(this->socket.get(), &wsabuf, 1, nullptr, &this->recv_flags, this->remote_addr.sockaddr(), &this->remote_addr_len, pov, nullptr)) {
                    error = ::WSAGetLastError();
                    if (WSA_IO_PENDING == error) {
                        error = NO_ERROR; // pending is not an error

                    } else {
                        // TODO: how to handle failure
                        // - currently will just retry until succeeds - we might get into a bad state and infinitely loop
                        // - for WSAECONNRESET, definately just need to retry
                        // - for any other error, potentially close the socket, recreate it, and start over
                        this->thread_iocp->cancel_request(pov);
                        if (WSAECONNRESET == error) {
                            ctsConfig::PrintErrorInfo(
                                L"[%.3f] ctsMediaStreamServer - WSARecvFrom failed as the prior WSASendTo failed with port unreachable\n",
                                ctsConfig::GetStatusTimeStamp());
                        } else {
                            ctsConfig::PrintErrorInfo(
                                L"[%.3f] WSARecvFrom failed (SOCKET %Iu) with error (%d)\n",
                                ctsConfig::GetStatusTimeStamp(),
                                this->socket.get(),
                                error);
                        }
                    }
                } else {
                    error = NO_ERROR;
                }
            } else {
                // if we no longer have a socket exit the loop
                break;
            }
        }
    }


    inline
    VOID CALLBACK ctsMediaStreamConnectedSocket::ctsMediaStreamTimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER)
    {
        ctsMediaStreamConnectedSocket* this_ptr = reinterpret_cast<ctsMediaStreamConnectedSocket*>(_context);

        // pair <BytesTransferred, Error>
        typedef std::pair<unsigned long, unsigned long> SendResults;

        // stateless lambda just to capture the functionality of posting WSASendTo
        // - as this is called multiple places within this function
        auto PostSendTo = [] (ctsMediaStreamConnectedSocket* this_ptr) -> SendResults {
            int error = WSA_OPERATION_ABORTED;
            unsigned bytes_transferred = 0;

            SOCKET s = this_ptr->socket_lock();
            if (s != INVALID_SOCKET) {
                long long seq_number = this_ptr->increment_sequence();

#ifdef TESTING_RESEND
                if (0 == seq_number % 5) {
                    ctsConfig::PrintDebug(L"********* TESTING ***** SKIPPING EVERY 5 SEQUENCE NUMBERS\n");
                    bytes_transferred = this_ptr->next_task.buffer_length;
                    error = NO_ERROR;

                } else {
#endif
                    ctsMediaStreamSendRequests sending_requests(
                        this_ptr->next_task.buffer_length, // total bytes to send
                        seq_number,
                        this_ptr->next_task.buffer);

                    for (auto& send_request : sending_requests) {
                        DWORD bytes_sent;
                        // making a synchronous call
                        if (SOCKET_ERROR == ::WSASendTo(s, send_request.data(), static_cast<DWORD>(send_request.size()), &bytes_sent, 0, this_ptr->remote_addr.sockaddr(), this_ptr->remote_addr.length(), nullptr, nullptr)) {
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
                                    s,
                                    seq_number,
                                    this_ptr->remote_addr.writeCompleteAddress().c_str(),
                                    bytes_requested);
                            } else {
                                ctsConfig::PrintErrorInfo(
                                    L"[%.3f] WSASendTo(%Iu, seq %lld, %s) failed [%d]\n",
                                    ctsConfig::GetStatusTimeStamp(),
                                    s,
                                    seq_number,
                                    this_ptr->remote_addr.writeCompleteAddress().c_str(),
                                    error);
                            }
                            // break out early if send fails
                            break;
                        } else {
                            ctsConfig::PrintDebug(
                                L"\t\tctsMediaStreamServer SendThreadProc sent %s seq number %lld (%lu bytes)\n",
                                this_ptr->remote_addr.writeCompleteAddress().c_str(),
                                seq_number,
                                bytes_sent);
                            bytes_transferred += bytes_sent;
                            error = NO_ERROR;
                        }
                    }
#ifdef TESTING_RESEND
                }
#endif
            }
            this_ptr->socket_release();

            return SendResults(bytes_transferred, error);
        }; // end of lambda definition

        // take a lock on the ctsSocket for this 'connection'
        std::shared_ptr<ctsSocket> shared_ctsSocket = this_ptr->cts_socket.lock();
        if (shared_ctsSocket.get() == nullptr) {
            // socket is already gone - remove it from the impl and exit
            pimpl->remove_socket(this_ptr->remote_addr);
            return;
        }

        ctl::ctAutoReleaseCriticalSection socket_lock(&this_ptr->object_guard);

        // post a send, then loop sending/scheduling as necessary
        auto send_results = PostSendTo(this_ptr);
        auto status = shared_ctsSocket->complete_io(
            this_ptr->next_task,
            std::get<0>(send_results),
            std::get<1>(send_results));

        ctsIOTask current_task = this_ptr->next_task;
        while (ctsSocket::IOStatus::SuccessMoreIO == status && ctsIOTask::IOAction::None != current_task.ioAction) {
            current_task = shared_ctsSocket->initiate_io();
            if (ctsIOTask::IOAction::Send == current_task.ioAction) {
                this_ptr->next_task = current_task;
                // if the time is less than one ms., we need to catch up on sends
                // - post the sendto immediately instead of scheduling for later
                if (this_ptr->next_task.time_offset_milliseconds < 1) {
                    send_results = PostSendTo(this_ptr);
                    status = shared_ctsSocket->complete_io(
                        this_ptr->next_task,
                        std::get<0>(send_results),
                        std::get<1>(send_results));
                } else {
                    this_ptr->schedule_task(this_ptr->next_task);
                }
            }
        }
    }

} // ctsTraffic namespace
