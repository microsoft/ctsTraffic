/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <memory>
#include <vector>
#include <string>
#include <algorithm>

#include <Windows.h>
#include <WinSock2.h>

#include <ctLocks.hpp>
#include <ctException.hpp>
#include <ctScopeGuard.hpp>
#include <ctHandle.hpp>
#include <ctSockaddr.hpp>

#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"
#include "ctsMediaStreamServer.h"
#include "ctsMediaStreamServerConnectedSocket.h"
#include "ctsMediaStreamServerListeningSocket.h"


namespace ctsTraffic {
    namespace ctsMediaStreamServerImpl {
        // ctsMediaStreamServerListeningSocket doesn't allow copies so using unique_ptr's to move them around
        std::vector<std::unique_ptr<ctsMediaStreamServerListeningSocket>> listening_sockets;

        CRITICAL_SECTION connected_object_guard;
        // ctsMediaStreamServerConnectedSocket doesn't allow copies so using unique_ptr's to move them around
        _Guarded_by_(connected_object_guard)
        std::vector<std::unique_ptr<ctsMediaStreamServerConnectedSocket>> connected_sockets;

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
                ctlScopeGuard(deleteConnectedObjectguardOnError, { ::DeleteCriticalSection(&ctsMediaStreamServerImpl::connected_object_guard); });

                if (!::InitializeCriticalSectionEx(&ctsMediaStreamServerImpl::awaiting_object_guard, 4000, 0)) {
                    throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsMediaStreamServer", false);
                }
                ctlScopeGuard(deleteAwaitingObjectguardOnError, { ::DeleteCriticalSection(&ctsMediaStreamServerImpl::awaiting_object_guard); });

                // 'listen' to each address
                for (const auto& addr : ctsConfig::Settings->ListenAddresses) {
                    ctl::ctScopedSocket listening(::WSASocket(addr.family(), SOCK_DGRAM, IPPROTO_UDP, NULL, 0, ctsConfig::Settings->SocketFlags));
                    if (INVALID_SOCKET == listening.get()) {
                        throw ctl::ctException(::WSAGetLastError(), L"socket", L"ctsMediaStreamServer", false);
                    }

                    int error = ctsConfig::SetPreBindOptions(listening.get(), addr);
                    if (error != NO_ERROR) {
                        throw ctl::ctException(error, L"SetPreBindOptions", L"ctsMediaStreamServer", false);
                    }

                    if (SOCKET_ERROR == ::bind(listening.get(), addr.sockaddr(), addr.length())) {
                        throw ctl::ctException(::WSAGetLastError(), L"bind", L"ctsMediaStreamServer", false);
                    }

                    SOCKET listening_socket_to_print(listening.get());
                    ctsMediaStreamServerImpl::listening_sockets.emplace_back(
                        std::make_unique<ctsMediaStreamServerListeningSocket>(
                        std::move(listening),
                        addr));

                    ctsConfig::PrintDebug(
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

        void init_once()
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

            // must guard connected_sockets since we need to add it
            ctl::ctAutoReleaseCriticalSection lock_connected_object(&ctsMediaStreamServerImpl::connected_object_guard);

            // find the matching connected_socket
            auto found_socket = std::find_if(
                std::begin(ctsMediaStreamServerImpl::connected_sockets),
                std::end(ctsMediaStreamServerImpl::connected_sockets),
                [&] (const std::unique_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) {
                return (shared_socket->target_address() == _connected_socket->get_address());
            });

            if (found_socket == std::end(ctsMediaStreamServerImpl::connected_sockets)) {
                ctsConfig::PrintDebug(
                    L"\t\tctsMediaStreamServer - failed to find the socket with remote address %s in our connected socket list\n",
                    shared_socket->target_address().writeCompleteAddress().c_str());
                throw ctl::ctException(ERROR_INVALID_DATA, L"ctsSocket was not found in the Connected Sockets", L"ctsMediaStreamServer", false);
            }

            (*found_socket)->schedule_task(_task);
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
                        ctsMediaStreamServerImpl::connected_sockets.emplace_back(std::make_unique<ctsMediaStreamServerConnectedSocket>(
                            _weak_socket, waiting_endpoint->first, waiting_endpoint->second));
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
        void ctsMediaStreamServerImpl::remove_socket(const ctl::ctSockaddr& _target_addr, unsigned long _error_code)
        {
            std::unique_ptr<ctsMediaStreamServerConnectedSocket> removed_socket;
            // scoping to the lock
            {
                ctl::ctAutoReleaseCriticalSection lock_connected_object(&ctsMediaStreamServerImpl::connected_object_guard);

                auto found_socket = std::find_if(
                    std::begin(ctsMediaStreamServerImpl::connected_sockets),
                    std::end(ctsMediaStreamServerImpl::connected_sockets),
                    [&_target_addr] (const std::unique_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) {
                    return _target_addr == _connected_socket->get_address();
                });
                // complete its ctsSocket and remove it from the connected socket list
                if (found_socket != std::end(ctsMediaStreamServerImpl::connected_sockets)) {
                    // can't erase it while holding a lock, so move it out of the vector
                    // then erase that moved-from slot from the vector
                    removed_socket = std::move(*found_socket);
                    ctsMediaStreamServerImpl::connected_sockets.erase(found_socket);

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
                    shared_socket->complete_state(_error_code);
                }
            }
            // only after releasing the lock can we delete the removed ctsMediaStreamServerConnectedSocket
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
        void ctsMediaStreamServerImpl::start(const ctl::ctScopedSocket& _socket, const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _target_addr)
        {
            // before starting a socket, verify there is not already a connected socket with this same socket address
            // scope the lock
            {
                ctl::ctAutoReleaseCriticalSection lock_connected_object(&ctsMediaStreamServerImpl::connected_object_guard);

                auto found_socket = std::find_if(
                    std::begin(ctsMediaStreamServerImpl::connected_sockets),
                    std::end(ctsMediaStreamServerImpl::connected_sockets),
                    [&_target_addr] (const std::unique_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) {
                    return _target_addr == _connected_socket->get_address();
                });

                if (found_socket != std::end(ctsMediaStreamServerImpl::connected_sockets)) {
                    ctsConfig::PrintDebug(
                        L"ctsMediaStreamServer - socket with remote address %s asked to be Started but was already established\n",
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
                            std::make_unique<ctsMediaStreamServerConnectedSocket>(
                            weak_instance,
                            _socket.get(),
                            _target_addr));
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

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Process an incoming RESEND request
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void ctsMediaStreamServerImpl::resend(const ctsMediaStreamMessage& _message, const ctl::ctSockaddr& _target_addr)
        {
            ctl::ctAutoReleaseCriticalSection lock_connected_object(&ctsMediaStreamServerImpl::connected_object_guard);

            // find the connected socket to resend a datagram
            auto found_socket = std::find_if(
                std::begin(ctsMediaStreamServerImpl::connected_sockets),
                std::end(ctsMediaStreamServerImpl::connected_sockets),
                [&_target_addr] (const std::unique_ptr<ctsMediaStreamServerConnectedSocket>& _connected_socket) {
                return _target_addr == _connected_socket->get_address();
            });
            if (found_socket == std::end(ctsMediaStreamServerImpl::connected_sockets)) {
                throw ctl::ctException(
                    ERROR_INVALID_DATA,
                    ctl::ctString::format_string(
                    L"ctsMediaStreamServer - socket with remote address %s asked to be Resend but was not found\n",
                    _target_addr.writeCompleteAddress().c_str()).c_str(),
                    L"ctsMediaStreamServer::resend",
                    true);

            }

            const std::unique_ptr<ctsMediaStreamServerConnectedSocket>& found_protected_socket = *found_socket;
            ctl::ctSockaddr target_addr(found_protected_socket->get_address());

            SOCKET socket = found_protected_socket->socket_lock();
#pragma warning(suppress: 26110)   //  PREFast is getting confused with the scope guard
            ctlScopeGuard(releaseSocketLockOnExit, { found_protected_socket->socket_release(); });

            if (socket != INVALID_SOCKET) {
                long long seq_number(ctl::ctMemoryGuardRead(&_message.sequence_number));
                ctsConfig::PrintDebug(
                    L"\t\tctsMediaStreamServer resending seq number %lld (%lu bytes)",
                    seq_number,
                    static_cast<unsigned long>(ctsConfig::GetMediaStream().FrameSizeBytes));

                ctsMediaStreamSendRequests sending_requests(
                    ctsConfig::GetMediaStream().FrameSizeBytes, // bytes to send
                    seq_number,
                    ctsIOPattern::AccessSharedBuffer());

                for (auto& send_request : sending_requests) {
                    ctsConfig::PrintDebug(
                        L" (%u bytes)",
                        static_cast<unsigned long>(send_request.size()));
                    DWORD bytes_sent;
                    if (SOCKET_ERROR == ::WSASendTo(socket, send_request.data(), static_cast<DWORD>(send_request.size()), &bytes_sent, 0, target_addr.sockaddr(), target_addr.length(), nullptr, nullptr)) {
                        auto error = ::WSAGetLastError();
                        ctsConfig::PrintErrorInfo(
                            L"[%.3f] WSASendTo(%Iu, seq %lld, %s) for a RESEND request failed [%d]\n",
                            ctsConfig::GetStatusTimeStamp(),
                            socket,
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
                ctsConfig::PrintDebug(L"\n");
            }
        }
    }
}
