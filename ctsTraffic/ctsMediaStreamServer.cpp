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
// wil headers
#include <wil/resource.h>
// ctl headers
#include <ctException.hpp>
#include <ctSockaddr.hpp>
#include <ctString.hpp>
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
    void ctsMediaStreamServerListener(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
    {
        try
        {
            ctsMediaStreamServerImpl::init_once();
            // ctsMediaStreamServerImpl will complete the ctsSocket object
            // when a client request comes in to be 'accepted'
            ctsMediaStreamServerImpl::accept_socket(_weak_socket);
        }
        catch (const std::exception& e)
        {
            ctsConfig::PrintException(e);
            auto shared_socket(_weak_socket.lock());
            if (shared_socket)
            {
                shared_socket->complete_state(ERROR_OUTOFMEMORY);
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called initiate IO on a datagram socket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamServerIo(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
    {
        ctsIOTask next_task;
        try
        {
            ctsMediaStreamServerImpl::init_once();

            const auto shared_socket(_weak_socket.lock());
            if (shared_socket)
            {
                // hold a reference on the iopattern
                auto shared_pattern(shared_socket->io_pattern());
                do
                {
                    next_task = shared_pattern->initiate_io();
                    if (next_task.ioAction != IOTaskAction::None)
                    {
                        ctsMediaStreamServerImpl::schedule_io(_weak_socket, next_task);
                    }
                } while (next_task.ioAction != IOTaskAction::None);
            }
        }
        catch (const std::exception& e)
        {
            ctsConfig::PrintException(e);
            if (next_task.ioAction != IOTaskAction::None)
            {
                auto exception_shared_socket(_weak_socket.lock());
                if (exception_shared_socket)
                {
                    // hold a reference on the iopattern
                    auto exception_shared_pattern(exception_shared_socket->io_pattern());
                    // must complete any IO that was requested but not scheduled
                    exception_shared_pattern->complete_io(next_task, 0, WSAENOBUFS);
                    if (0 == exception_shared_socket->pended_io())
                    {
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
    void ctsMediaStreamServerClose(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
        try
    {
        ctsMediaStreamServerImpl::init_once();

        const auto shared_socket(_weak_socket.lock());
        if (shared_socket)
        {
            ctsMediaStreamServerImpl::remove_socket(shared_socket->target_address());
        }
    }
    catch (...)
    {
    }


    namespace ctsMediaStreamServerImpl
    {
        std::vector<std::unique_ptr<ctsMediaStreamServerListeningSocket>> listening_sockets;

        // function for doing the actual IO for a UDP media stream datagram connection
        wsIOResult ConnectedSocketIo(_In_ ctsMediaStreamServerConnectedSocket* connected_socket) noexcept;

        wil::critical_section socket_vector_guard;
        _Guarded_by_(socket_vector_guard) std::vector<std::shared_ptr<ctsMediaStreamServerConnectedSocket>> connected_sockets;
        // weak_ptr<> to ctsSocket objects ready to accept a connection
        _Guarded_by_(socket_vector_guard) std::vector<std::weak_ptr<ctsSocket>> accepting_sockets;
        // endpoints that have been received from clients not yet matched to ctsSockets
        _Guarded_by_(socket_vector_guard) std::vector<std::pair<SOCKET, ctl::ctSockaddr>> awaiting_endpoints;


        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Singleton values used as the actual implementation for every 'connection'
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
        static INIT_ONCE InitImpl = INIT_ONCE_STATIC_INIT;
        static BOOL CALLBACK InitOnceImpl(PINIT_ONCE, PVOID, PVOID*)
        {
            try
            {
                // 'listen' to each address
                for (const auto& addr : ctsConfig::Settings->ListenAddresses)
                {
                    wil::unique_socket listening(ctsConfig::CreateSocket(addr.family(), SOCK_DGRAM, IPPROTO_UDP, ctsConfig::Settings->SocketFlags));

                    const auto error = ctsConfig::SetPreBindOptions(listening.get(), addr);
                    if (error != NO_ERROR)
                    {
                        throw ctl::ctException(error, L"SetPreBindOptions", L"ctsMediaStreamServer", false);
                    }

                    if (SOCKET_ERROR == bind(listening.get(), addr.sockaddr(), addr.length()))
                    {
                        throw ctl::ctException(WSAGetLastError(), L"bind", L"ctsMediaStreamServer", false);
                    }

                    // capture the socket value before moved into the vector
                    const SOCKET listening_socket_to_print(listening.get());
                    listening_sockets.emplace_back(
                        std::make_unique<ctsMediaStreamServerListeningSocket>(std::move(listening), addr));
                    PrintDebugInfo(
                        L"\t\tctsMediaStreamServer - Receiving datagrams on %ws (%Iu)\n",
                        addr.WriteCompleteAddress().c_str(),
                        listening_socket_to_print);
                }

                if (listening_sockets.empty())
                {
                    throw std::exception("ctsMediaStreamServer invoked with no listening addresses specified");
                }

                // initiate the recv's in the 'listening' sockets
                for (auto& listener : listening_sockets)
                {
                    listener->initiate_recv();
                }
            }
            catch (const std::exception& e)
            {
                ctsConfig::PrintException(e);
                return FALSE;
            }
            return TRUE;
        }

        void init_once()
        {
            if (!InitOnceExecuteOnce(&InitImpl, InitOnceImpl, nullptr, nullptr))
            {
                throw std::runtime_error("ctsMediaStreamServerListener could not be instantiated");
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Schedule the first IO on the specified ctsSocket
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void schedule_io(const std::weak_ptr<ctsSocket>& _weak_socket, const ctsIOTask& _task)
        {
            auto shared_socket = _weak_socket.lock();
            if (!shared_socket)
            {
                throw ctl::ctException(WSAECONNABORTED, L"ctsSocket already freed", L"ctsMediaStreamServer", false);
            }

            std::shared_ptr<ctsMediaStreamServerConnectedSocket> shared_connected_socket;
            {
                // must guard connected_sockets since we need to add it
                const auto lock_connected_object = socket_vector_guard.lock();

                // find the matching connected_socket
                const auto found_socket = std::find_if(
                    std::begin(connected_sockets),
                    std::end(connected_sockets),
                    [&shared_socket](const std::shared_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) noexcept {
                        return (shared_socket->target_address() == _connected_socket->get_remote_address());
                    }
                );

                if (found_socket == std::end(connected_sockets))
                {
                    ctsConfig::PrintErrorInfo(
                        ctl::ctString::ctFormatString("ctsMediaStreamServer - failed to find the socket with remote address %ws in our connected socket list to continue sending datagrams",
                            shared_socket->target_address().WriteCompleteAddress().c_str()).c_str());
                    throw ctl::ctException(ERROR_INVALID_DATA, L"ctsSocket was not found in the connected sockets to continue sending datagrams", L"ctsMediaStreamServer", false);
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
        void accept_socket(const std::weak_ptr<ctsSocket>& _weak_socket)
        {
            auto shared_socket(_weak_socket.lock());
            if (shared_socket)
            {
                const auto lock_awaiting_object = socket_vector_guard.lock();

                if (awaiting_endpoints.empty())
                {
                    // just add it to our accepting sockets vector under the writer lock
                    accepting_sockets.push_back(_weak_socket);
                }
                else
                {
                    auto waiting_endpoint = awaiting_endpoints.rbegin();

                    const auto existing_socket = std::find_if(
                        std::begin(connected_sockets),
                        std::end(connected_sockets),
                        [&](const std::shared_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) noexcept {
                            return waiting_endpoint->second == _connected_socket->get_remote_address();
                        });

                    if (existing_socket != std::end(connected_sockets))
                    {
                        ctsConfig::Settings->UdpStatusDetails.duplicate_frames.increment();
                        PrintDebugInfo(L"ctsMediaStreamServer::accept_socket - socket with remote address %ws asked to be Started but was already established",
                            waiting_endpoint->second.WriteCompleteAddress().c_str());
                        // return early if this was a duplicate request: this can happen if there is latency or drops
                        // between the client and server as they attempt to negotiating starting a new stream
                        return;
                    }

                    connected_sockets.emplace_back(
                        std::make_shared<ctsMediaStreamServerConnectedSocket>(
                            _weak_socket,
                            waiting_endpoint->first,
                            waiting_endpoint->second,
                            ConnectedSocketIo));

                    PrintDebugInfo(L"ctsMediaStreamServer::accept_socket - socket with remote address %ws added to connected_sockets",
                        waiting_endpoint->second.WriteCompleteAddress().c_str());

                    // now complete the ctsSocket 'Create' request
                    const auto found_socket = std::find_if(
                        listening_sockets.begin(),
                        listening_sockets.end(),
                        [&waiting_endpoint](const std::unique_ptr<ctsMediaStreamServerListeningSocket>& _listener) noexcept {
                            return (_listener->get_socket() == waiting_endpoint->first);
                        });
                    FAIL_FAST_IF_MSG(
                        found_socket == listening_sockets.end(),
                        "Could not find the socket (%Iu) in the waiting_endpoint from our listening sockets (%p)\n",
                        waiting_endpoint->first, &listening_sockets);

                    shared_socket->set_local_address((*found_socket)->get_address());
                    shared_socket->set_target_address(waiting_endpoint->second);
                    shared_socket->complete_state(NO_ERROR);

                    ctsConfig::PrintNewConnection(shared_socket->local_address(), shared_socket->target_address());
                    // if added to connected_sockets, can then safely remove it from the waiting endpoint
                    awaiting_endpoints.pop_back();
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
            const auto lock_connected_object = socket_vector_guard.lock();

            const auto found_socket = std::find_if(
                std::begin(connected_sockets),
                std::end(connected_sockets),
                [&_target_addr](const std::shared_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) noexcept {
                    return _target_addr == _connected_socket->get_remote_address();
                });

            if (found_socket != std::end(connected_sockets))
            {
                connected_sockets.erase(found_socket);
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Processes the incoming START request from the client
        /// - if we have a waiting ctsSocket to accept it, will add it to connected_sockets
        /// - else we'll queue it to awaiting_endpoints
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void Start(SOCKET _socket, const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _target_addr)
        {
            const auto lock_awaiting_object = socket_vector_guard.lock();

            const auto existing_socket = std::find_if(
                std::begin(connected_sockets),
                std::end(connected_sockets),
                [&_target_addr](const std::shared_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) noexcept {
                    return _target_addr == _connected_socket->get_remote_address();
                });
            if (existing_socket != std::end(connected_sockets))
            {
                ctsConfig::Settings->UdpStatusDetails.duplicate_frames.increment();
                PrintDebugInfo(L"ctsMediaStreamServer::start - socket with remote address %ws asked to be Started but was already in connected_sockets",
                    _target_addr.WriteCompleteAddress().c_str());
                // return early if this was a duplicate request: this can happen if there is latency or drops
                // between the client and server as they attempt to negotiating starting a new stream
                return;
            }
            const auto awaiting_endpoint = std::find_if(
                std::begin(awaiting_endpoints),
                std::end(awaiting_endpoints),
                [&_target_addr](const std::pair<SOCKET, ctl::ctSockaddr>& endpoint) noexcept {
                    return _target_addr == endpoint.second;
                });
            if (awaiting_endpoint != std::end(awaiting_endpoints))
            {
                ctsConfig::Settings->UdpStatusDetails.duplicate_frames.increment();
                PrintDebugInfo(L"ctsMediaStreamServer::start - socket with remote address %ws asked to be Started but was already in awaiting endpoints",
                    _target_addr.WriteCompleteAddress().c_str());
                // return early if this was a duplicate request: this can happen if there is latency or drops
                // between the client and server as they attempt to negotiating starting a new stream
                return;
            }

            // find a ctsSocket waiting to 'accept' a connection and complete it
            bool add_to_awaiting = true;
            while (!accepting_sockets.empty())
            {
                auto weak_instance = *accepting_sockets.rbegin();
                auto shared_instance = weak_instance.lock();
                if (shared_instance)
                {
                    // 'move' the accepting socket to connected
                    connected_sockets.emplace_back(
                        std::make_shared<ctsMediaStreamServerConnectedSocket>(weak_instance, _socket, _target_addr, ConnectedSocketIo));

                    PrintDebugInfo(L"ctsMediaStreamServer::start - socket with remote address %ws added to connected_sockets",
                        _target_addr.WriteCompleteAddress().c_str());

                    // verify is successfully added to connected_sockets before popping off accepting_sockets
                    add_to_awaiting = false;
                    accepting_sockets.pop_back();

                    // now complete the accepted ctsSocket back to the ctsSocketState
                    shared_instance->set_local_address(_local_addr);
                    shared_instance->set_target_address(_target_addr);
                    shared_instance->complete_state(NO_ERROR);

                    ctsConfig::PrintNewConnection(_local_addr, _target_addr);
                    break;
                }
            }

            // if we didn't find a waiting connection to accept it, queue it for when one arrives later
            if (add_to_awaiting)
            {
                PrintDebugInfo(L"ctsMediaStreamServer::start - socket with remote address %ws added to awaiting_endpoints",
                    _target_addr.WriteCompleteAddress().c_str());

                // only queue it if we aren't already waiting on this address
                awaiting_endpoints.emplace_back(_socket, _target_addr);
            }
        }


        wsIOResult ConnectedSocketIo(_In_ ctsMediaStreamServerConnectedSocket* connected_socket) noexcept
        {
            const SOCKET socket = connected_socket->get_sending_socket();
            if (INVALID_SOCKET == socket)
            {
                return wsIOResult(WSA_OPERATION_ABORTED);
            }

            const ctl::ctSockaddr& remote_addr(connected_socket->get_remote_address());
            const ctsIOTask next_task = connected_socket->get_nextTask();

            wsIOResult return_results;
            if (ctsIOTask::BufferType::UdpConnectionId == next_task.buffer_type)
            {
                // making a synchronous call
                WSABUF wsabuf{};
                wsabuf.buf = next_task.buffer;
                wsabuf.len = next_task.buffer_length;

                const auto send_result = WSASendTo(
                    socket,
                    &wsabuf,
                    1,
                    &return_results.bytes_transferred,
                    0,
                    remote_addr.sockaddr(),
                    remote_addr.length(),
                    nullptr,
                    nullptr);

                if (SOCKET_ERROR == send_result)
                {
                    const auto error = WSAGetLastError();
                    try
                    {
                        ctsConfig::PrintErrorInfo(
                            ctl::ctString::ctFormatString("WSASendTo(%Iu, %ws) for the Connection-ID failed [%d]",
                                socket,
                                remote_addr.WriteCompleteAddress().c_str(),
                                error).c_str());
                    }
                    catch (...)
                    {
                        // best effort
                    }
                    return wsIOResult(error);
                }

            }
            else
            {
                const auto seq_number = connected_socket->increment_sequence();

                PrintDebugInfo(
                    L"\t\tctsMediaStreamServer sending seq number %lld (%lu bytes)\n",
                    seq_number,
                    next_task.buffer_length);

                ctsMediaStreamSendRequests sending_requests(
                    next_task.buffer_length, // total bytes to send
                    seq_number,
                    next_task.buffer);

                for (auto& send_request : sending_requests)
                {
                    // making a synchronous call
                    DWORD bytes_sent{};
                    const auto send_result = WSASendTo(
                        socket,
                        send_request.data(),
                        static_cast<DWORD>(send_request.size()),
                        &bytes_sent,
                        0,
                        remote_addr.sockaddr(),
                        remote_addr.length(),
                        nullptr,
                        nullptr);

                    if (SOCKET_ERROR == send_result)
                    {
                        const auto error = WSAGetLastError();
                        try
                        {
                            if (WSAEMSGSIZE == error)
                            {
                                unsigned long bytes_requested = 0;
                                // iterate across each WSABUF* in the array
                                for (auto& wasbuf : send_request)
                                {
                                    bytes_requested += wasbuf.len;
                                }
                                ctsConfig::PrintErrorInfo(
                                    ctl::ctString::ctFormatString("WSASendTo(%Iu, seq %lld, %ws) failed with WSAEMSGSIZE : attempted to send datagram of size %u bytes",
                                        socket,
                                        seq_number,
                                        remote_addr.WriteCompleteAddress().c_str(),
                                        bytes_requested).c_str());
                            }
                            else
                            {
                                ctsConfig::PrintErrorInfo(
                                    ctl::ctString::ctFormatString("WSASendTo(%Iu, seq %lld, %ws) failed [%d]",
                                        socket,
                                        seq_number,
                                        remote_addr.WriteCompleteAddress().c_str(),
                                        error).c_str());
                            }
                        }
                        catch (...)
                        {
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
