/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <memory>
#include <vector>
#include <algorithm>
// os headers
#include <Windows.h>
#include <WinSock2.h>
// ctl headers
#include <ctLocks.hpp>
#include <ctException.hpp>
#include <ctScopeGuard.hpp>
#include <ctHandle.hpp>
#include <ctSockaddr.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"
#include "ctsWinsockLayer.h"
#include "ctsMediaStreamServer.h"
#include "ctsMediaStreamServerConnectedSocket.h"
#include "ctsMediaStreamServerListeningSocket.h"
#include "ctsMediaStreamProtocol.hpp"


namespace ctsTraffic {

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called to 'accept' incoming connections
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamServerListener(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT
    {
        try {
            ctsMediaStreamServerImpl::init_once();
            // ctsMediaStreamServerImpl will complete the ctsSocket object
            // when a client request comes in to be 'accepted'
            ctsMediaStreamServerImpl::accept_socket(_weak_socket);
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            auto shared_socket(_weak_socket.lock());
            if (shared_socket) {
                shared_socket->complete_state(ERROR_OUTOFMEMORY);
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called initiate IO on a datagram socket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamServerIo(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT
    {
        ctsIOTask next_task;
        try {
            ctsMediaStreamServerImpl::init_once();

            auto shared_socket(_weak_socket.lock());
            if (shared_socket) {
                // hold a reference on the iopattern
                auto shared_pattern(shared_socket->io_pattern());
                do {
                    next_task = shared_pattern->initiate_io();
                    if (next_task.ioAction != IOTaskAction::None) {
                        ctsMediaStreamServerImpl::schedule_io(_weak_socket, next_task);
                    }
                } while (next_task.ioAction != IOTaskAction::None);
            }
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            if (next_task.ioAction != IOTaskAction::None) {
                auto exception_shared_socket(_weak_socket.lock());
                if (exception_shared_socket) {
                    // hold a reference on the iopattern
                    auto exception_shared_pattern(exception_shared_socket->io_pattern());
                    // must complete any IO that was requested but not scheduled
                    exception_shared_pattern->complete_io(next_task, 0, WSAENOBUFS);
                    if (0 == exception_shared_socket->pended_io()) {
                        exception_shared_socket->complete_state(ERROR_OUTOFMEMORY);
                    }
                }
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called to remove that socket from the tracked vector of connected sockets
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamServerClose(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT
    {
        try {
            ctsMediaStreamServerImpl::init_once();

            auto shared_socket(_weak_socket.lock());
            if (shared_socket) {
                ctsMediaStreamServerImpl::remove_socket(shared_socket->local_address());
            }
        }
        catch (const std::exception&) {
        }
    }


    namespace ctsMediaStreamServerImpl {
        std::vector<std::unique_ptr<ctsMediaStreamServerListeningSocket>> listening_sockets;
        
        // function for doing the actual IO for a UDP media stream datagram connection
        wsIOResult ConnectedSocketIo(_In_ ctsMediaStreamServerConnectedSocket* this_ptr);

        CRITICAL_SECTION connected_object_guard;
        _Guarded_by_(connected_object_guard)
            std::vector<std::shared_ptr<ctsMediaStreamServerConnectedSocket>> connected_sockets;

        CRITICAL_SECTION awaiting_object_guard;
        // weak_ptr<> to ctsSocket objects ready to accept a connection
        _Guarded_by_(awaiting_object_guard)
            std::vector<std::weak_ptr<ctsSocket>> accepting_sockets;

        // endpoints that have been received from clients not yet matched to ctsSockets
        _Guarded_by_(awaiting_object_guard)
            std::vector<std::pair<SOCKET, ctl::ctSockaddr>> awaiting_endpoints;


        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Singleton values used as the actual implementation for every 'connection'
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        static INIT_ONCE InitImpl = INIT_ONCE_STATIC_INIT;
        static BOOL CALLBACK InitOnceImpl(PINIT_ONCE, PVOID, PVOID *)
        {
            try {
                if (!::InitializeCriticalSectionEx(&ctsMediaStreamServerImpl::connected_object_guard, 4000, 0)) {
                    throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsMediaStreamServer", false);
                }
                ctlScopeGuard(
                    deleteConnectedObjectguardOnError,
                    {::DeleteCriticalSection(&ctsMediaStreamServerImpl::connected_object_guard);});

                if (!::InitializeCriticalSectionEx(&ctsMediaStreamServerImpl::awaiting_object_guard, 4000, 0)) {
                    throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsMediaStreamServer", false);
                }
                ctlScopeGuard(
                    deleteAwaitingObjectguardOnError,
                    {::DeleteCriticalSection(&ctsMediaStreamServerImpl::awaiting_object_guard);});

                // 'listen' to each address
                for (const auto& addr : ctsConfig::Settings->ListenAddresses) {
                    ctl::ctScopedSocket listening(ctsConfig::CreateSocket(addr.family(), SOCK_DGRAM, IPPROTO_UDP, ctsConfig::Settings->SocketFlags));

                    auto error = ctsConfig::SetPreBindOptions(listening.get(), addr);
                    if (error != NO_ERROR) {
                        throw ctl::ctException(error, L"SetPreBindOptions", L"ctsMediaStreamServer", false);
                    }

                    if (SOCKET_ERROR == ::bind(listening.get(), addr.sockaddr(), addr.length())) {
                        throw ctl::ctException(::WSAGetLastError(), L"bind", L"ctsMediaStreamServer", false);
                    }

                    // capture the socket value before moved into the vector
                    SOCKET listening_socket_to_print(listening.get());
                    ctsMediaStreamServerImpl::listening_sockets.emplace_back(
                        new ctsMediaStreamServerListeningSocket(std::move(listening), addr));

                    PrintDebugInfo(
                        L"\t\tctsMediaStreamServer - Receiving datagrams on %s (%Iu)\n",
                        addr.writeCompleteAddress().c_str(),
                        listening_socket_to_print);
                }

                if (ctsMediaStreamServerImpl::listening_sockets.empty()) {
                    throw std::exception("ctsMediaStreamServer invoked with no listening addresses specified");
                }

                // initiate the recv's in the 'listening' sockets
                for (auto& listener : ctsMediaStreamServerImpl::listening_sockets) {
                    listener->initiate_recv();
                }

                // dismiss scope guards as there were no errors
                deleteConnectedObjectguardOnError.dismiss();
                deleteAwaitingObjectguardOnError.dismiss();
            }
            catch (const std::exception& e) {
                ctsConfig::PrintException(e);
                return FALSE;
            }
            return TRUE;
        }

        void ctsMediaStreamServerImpl::init_once()
        {
            if (!::InitOnceExecuteOnce(&InitImpl, InitOnceImpl, nullptr, nullptr)) {
                throw std::runtime_error("ctsMediaStreamServerListener could not be instantiated");
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Schedule the first IO on the specified ctsSocket
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void ctsMediaStreamServerImpl::schedule_io(const std::weak_ptr<ctsSocket>& _weak_socket, const ctsIOTask& _task)
        {
            auto shared_socket = _weak_socket.lock();
            if (!shared_socket) {
                throw ctl::ctException(WSAECONNABORTED, L"ctsSocket already freed", L"ctsMediaStreamServer", false);
            }

            std::shared_ptr<ctsMediaStreamServerConnectedSocket> shared_connected_socket;
            {
                // must guard connected_sockets since we need to add it
                ctl::ctAutoReleaseCriticalSection lock_connected_object(&ctsMediaStreamServerImpl::connected_object_guard);

                // find the matching connected_socket
                auto found_socket = std::find_if(
                    std::begin(ctsMediaStreamServerImpl::connected_sockets),
                    std::end(ctsMediaStreamServerImpl::connected_sockets),
                    [&shared_socket] (const std::shared_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) {
                        return (shared_socket->target_address() == _connected_socket->get_address());
                    }
                );

                if (found_socket == std::end(ctsMediaStreamServerImpl::connected_sockets)) {
                    PrintDebugInfo(
                        L"\t\tctsMediaStreamServer - failed to find the socket with remote address %s in our connected socket list\n",
                        shared_socket->target_address().writeCompleteAddress().c_str());
                    throw ctl::ctException(ERROR_INVALID_DATA, L"ctsSocket was not found in the Connected Sockets", L"ctsMediaStreamServer", false);
                }

                shared_connected_socket = *found_socket;
            }
            // must call into connected socket without holding a lock
            // and without maintaining an iterator into the list
            // since the call to schedule_io could end up asking to remove this object from the list
            shared_connected_socket->schedule_task(_task);
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Process a new ctsSocket from the ctsSocketBroker
        /// - accept_socket takes the ctsSocket to create a new entry
        ///   which will create a corresponding ctsMediaStreamServerConnectedSocket in the process
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void ctsMediaStreamServerImpl::accept_socket(const std::weak_ptr<ctsSocket>& _weak_socket)
        {
            auto shared_socket(_weak_socket.lock());
            if (shared_socket) {
                ctl::ctAutoReleaseCriticalSection lock_awaiting_object(&ctsMediaStreamServerImpl::awaiting_object_guard);

                if (0 == ctsMediaStreamServerImpl::awaiting_endpoints.size()) {
                    // just add it to our accepting sockets vector under the writer lock
                    ctsMediaStreamServerImpl::accepting_sockets.push_back(_weak_socket);

                } else {
                    auto waiting_endpoint = ctsMediaStreamServerImpl::awaiting_endpoints.rbegin();

                    // must guard connected_sockets since we need to add it to the vector
                    // - scope to the lock
                    {
                        ctl::ctAutoReleaseCriticalSection lock_connected_object(&ctsMediaStreamServerImpl::connected_object_guard);
                        ctsMediaStreamServerImpl::connected_sockets.emplace_back(
                            std::make_shared<ctsMediaStreamServerConnectedSocket>(
                            _weak_socket, 
                            waiting_endpoint->first, 
                            waiting_endpoint->second,
                            ctsMediaStreamServerImpl::ConnectedSocketIo));
                    }

                    // now complete the ctsSocket 'Create' request
                    // find the local address
                    auto found_socket = std::find_if(
                        ctsMediaStreamServerImpl::listening_sockets.begin(),
                        ctsMediaStreamServerImpl::listening_sockets.end(),
                        [&waiting_endpoint] (const std::unique_ptr<ctsMediaStreamServerListeningSocket>& _listener) {
                        return (_listener->get_socket() == waiting_endpoint->first);
                    });

                    ctl::ctFatalCondition(
                        (found_socket == ctsMediaStreamServerImpl::listening_sockets.end()),
                        L"Could not find the socket (%Iu) in the waiting_endpoint from our listening sockets (%p)\n",
                        waiting_endpoint->first, &ctsMediaStreamServerImpl::listening_sockets);

                    shared_socket->set_local_address((*found_socket)->get_address());
                    shared_socket->set_target_address(waiting_endpoint->second);
                    shared_socket->complete_state(NO_ERROR);

                    // if added to connected_sockets, can then safely remove it from the waiting endpoint
                    // - no longer touching the iterator waiting_endpoint
                    ctsMediaStreamServerImpl::awaiting_endpoints.pop_back();
                }
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Process the removal of a connected socket once it is completed
        /// - remove_socket takes the remote address to find the socket
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void ctsMediaStreamServerImpl::remove_socket(const ctl::ctSockaddr& _target_addr)
        {
            ctl::ctAutoReleaseCriticalSection lock_connected_object(&ctsMediaStreamServerImpl::connected_object_guard);

            auto found_socket = std::find_if(
                std::begin(ctsMediaStreamServerImpl::connected_sockets),
                std::end(ctsMediaStreamServerImpl::connected_sockets),
                [&_target_addr] (const std::shared_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) {
                return _target_addr == _connected_socket->get_address();
            });

            if (found_socket != std::end(ctsMediaStreamServerImpl::connected_sockets)) {
                ctsMediaStreamServerImpl::connected_sockets.erase(found_socket);
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Processes the incoming START request from the client
        /// - if we have a waiting ctsSocket to accept it, will add it to connected_sockets
        /// - else we'll queue it to awaiting_endpoints
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void ctsMediaStreamServerImpl::start(const ctl::ctScopedSocket& _socket, const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _target_addr)
        {
            // before starting a socket, verify there is not already a connected socket with this same socket address
            // scope the lock
            {
                ctl::ctAutoReleaseCriticalSection lock_connected_object(&ctsMediaStreamServerImpl::connected_object_guard);

                auto found_socket = std::find_if(
                    std::begin(ctsMediaStreamServerImpl::connected_sockets),
                    std::end(ctsMediaStreamServerImpl::connected_sockets),
                    [&_target_addr] (const std::shared_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) {
                    return _target_addr == _connected_socket->get_address();
                });

                if (found_socket != std::end(ctsMediaStreamServerImpl::connected_sockets)) {
                    PrintDebugInfo(
                        L"\t\tctsMediaStreamServer - socket with remote address %s asked to be Started but was already established\n",
                        _target_addr.writeCompleteAddress().c_str());
                    // return early if this was a duplicate request: this can happen if there is latency or drops
                    // between the client and server as they attempt to negotiating starting a new stream
                    return;
                }
            }

            // find a ctsSocket waiting to 'accept' a connection and complete it
            ctl::ctAutoReleaseCriticalSection lock_awaiting_object(&ctsMediaStreamServerImpl::awaiting_object_guard);

            // walk through the list to find a socket that is still alive to take this connection
            bool added_connection = false;
            while (!ctsMediaStreamServerImpl::accepting_sockets.empty()) {
                auto weak_instance = *ctsMediaStreamServerImpl::accepting_sockets.rbegin();
                auto shared_instance = weak_instance.lock();
                if (shared_instance) {
                    // 'move' the accepting socket to connected
                    // - scope the lock
                    {
                        ctl::ctAutoReleaseCriticalSection lock_connected_object(&ctsMediaStreamServerImpl::connected_object_guard);
                        ctsMediaStreamServerImpl::connected_sockets.emplace_back(
                            std::make_shared<ctsMediaStreamServerConnectedSocket>(
                            weak_instance,
                            _socket.get(),
                            _target_addr,
                            ctsMediaStreamServerImpl::ConnectedSocketIo));
                    }

                    // verify is successfully added to connected_sockets before popping off accepting_sockets
                    added_connection = true;
                    ctsMediaStreamServerImpl::accepting_sockets.pop_back();

                    // now complete the accepted ctsSocket back to the ctsSocketState
                    shared_instance->set_local_address(_local_addr);
                    shared_instance->set_target_address(_target_addr);
                    shared_instance->complete_state(NO_ERROR);

                    ctsConfig::PrintNewConnection(_local_addr, _target_addr);
                    break;
                }
            }

            // if we didn't find a waiting connection to accept it, queue it for when one arrives later
            if (!added_connection) {
                // only queue it if we aren't already waiting on this address
                ctsMediaStreamServerImpl::awaiting_endpoints.emplace_back(_socket.get(), _target_addr);
            }
        }


        wsIOResult ctsMediaStreamServerImpl::ConnectedSocketIo(_In_ ctsMediaStreamServerConnectedSocket* this_ptr)
        {
            auto this_socket_lock(ctsGuardSocket(this_ptr));
            SOCKET socket = this_socket_lock.get();
            if (INVALID_SOCKET == socket) {
                return wsIOResult(WSA_OPERATION_ABORTED);
            }

            const ctl::ctSockaddr& remote_addr(this_ptr->get_address());
            ctsIOTask next_task = this_ptr->get_nextTask();

            wsIOResult return_results;
            if (ctsIOTask::BufferType::UdpConnectionId == next_task.buffer_type) {
                // making a synchronous call
                WSABUF wsabuf;
                wsabuf.buf = next_task.buffer;
                wsabuf.len = next_task.buffer_length;

                auto send_result = ::WSASendTo(
                    socket,
                    &wsabuf,
                    1,
                    &return_results.bytes_transferred,
                    0,
                    remote_addr.sockaddr(),
                    remote_addr.length(),
                    nullptr,
                    nullptr);

                if (SOCKET_ERROR == send_result) {
                    auto error = ::WSAGetLastError();
                    try {
                        ctsConfig::PrintErrorInfo(
                            L"WSASendTo(%Iu, %s) for the Connection-ID failed [%d]",
                            socket,
                            remote_addr.writeCompleteAddress().c_str(),
                            error);
                    }
                    catch (const std::exception&) {
                        // best effort
                    }
                    return wsIOResult(error);
                }

            } else {
                auto seq_number = this_ptr->increment_sequence();

                PrintDebugInfo(
                    L"\t\tctsMediaStreamServer sending seq number %lld (%lu bytes)\n",
                    seq_number,
                    next_task.buffer_length);

                ctsMediaStreamSendRequests sending_requests(
                    next_task.buffer_length, // total bytes to send
                    seq_number,
                    next_task.buffer);

                for (auto& send_request : sending_requests) {
                    // making a synchronous call
                    DWORD bytes_sent;
                    auto send_result = ::WSASendTo(
                        socket,
                        send_request.data(),
                        static_cast<DWORD>(send_request.size()),
                        &bytes_sent,
                        0,
                        remote_addr.sockaddr(),
                        remote_addr.length(),
                        nullptr,
                        nullptr);

                    if (SOCKET_ERROR == send_result) {
                        auto error = ::WSAGetLastError();
                        try {
                            if (WSAEMSGSIZE == error) {
                                unsigned long bytes_requested = 0;
                                // iterate across each WSABUF* in the array
                                for (auto& wasbuf : send_request) {
                                    bytes_requested += wasbuf.len;
                                }
                                ctsConfig::PrintErrorInfo(
                                    L"WSASendTo(%Iu, seq %lld, %s) failed with WSAEMSGSIZE : attempted to send datagram of size %u bytes",
                                    socket,
                                    seq_number,
                                    remote_addr.writeCompleteAddress().c_str(),
                                    bytes_requested);
                            } else {
                                ctsConfig::PrintErrorInfo(
                                    L"WSASendTo(%Iu, seq %lld, %s) failed [%d]",
                                    socket,
                                    seq_number,
                                    remote_addr.writeCompleteAddress().c_str(),
                                    error);
                            }
                        }
                        catch (const std::exception&) {
                            // best effort
                        }
                        return wsIOResult(error);
                    }
                    
                    // successfully completed synchronously
                    return_results.bytes_transferred += bytes_sent;
                }
            }

            return return_results;
        }
    }
}
