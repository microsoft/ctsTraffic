/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <exception>
#include <memory>

// os headers
#include <Windows.h>
#include <WinSock2.h>

// ctl headers
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
#include <ctException.hpp>
#include <ctHandle.hpp>
#include <utility>

// project headers
#include "ctsMediaStreamServerListeningSocket.h"
#include "ctsMediaStreamServer.h"
#include "ctsMediaStreamProtocol.hpp"
#include "ctsConfig.h"


namespace ctsTraffic {

    ctsMediaStreamServerListeningSocket::ctsMediaStreamServerListeningSocket(ctl::ctScopedSocket&& _listening_socket, ctl::ctSockaddr _listening_addr) :
        thread_iocp(std::make_shared<ctl::ctThreadIocp>(_listening_socket.get(), ctsConfig::Settings->PTPEnvironment)),
        socket(std::move(_listening_socket)),
        listening_addr(std::move(_listening_addr))
    {
        ctl::ctFatalCondition(
            !!(ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP),
            L"ctsMediaStream sockets must not have HANDLE_INLINE_IOCP set on its datagram sockets");

        if (!::InitializeCriticalSectionEx(&object_guard, 4000, 0)) {
            throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsMediaStreamServer", false);
        }
    }

    ctsMediaStreamServerListeningSocket::~ctsMediaStreamServerListeningSocket() NOEXCEPT
    {
        // close the socket, then end the TP
        this->reset();
        this->thread_iocp.reset();
        ::DeleteCriticalSection(&object_guard);
    }

    SOCKET ctsMediaStreamServerListeningSocket::get_socket() const NOEXCEPT
    {
        const ctl::ctAutoReleaseCriticalSection object_lock(&this->object_guard);
        return this->socket.get();
    }

    ctl::ctSockaddr ctsMediaStreamServerListeningSocket::get_address() const NOEXCEPT
    {
        const ctl::ctAutoReleaseCriticalSection object_lock(&this->object_guard);
        return this->listening_addr;
    }

    void ctsMediaStreamServerListeningSocket::reset() NOEXCEPT
    {
        const ctl::ctAutoReleaseCriticalSection object_lock(&this->object_guard);
        this->socket.reset();
    }

    void ctsMediaStreamServerListeningSocket::initiate_recv() NOEXCEPT
    {
        // continue to try to post a recv if the call fails
        int error = SOCKET_ERROR;
        unsigned long failure_counter = 0;
        while (error != NO_ERROR) {
            try {
                const ctl::ctAutoReleaseCriticalSection lock_socket(&this->object_guard);
                if (this->socket.get() != INVALID_SOCKET) {
                    WSABUF wsabuf;
                    wsabuf.buf = this->recv_buffer.data();
                    wsabuf.len = static_cast<ULONG>(this->recv_buffer.size());
                    ::ZeroMemory(this->recv_buffer.data(), this->recv_buffer.size());

                    this->recv_flags = 0;
                    this->remote_addr.reset();
                    this->remote_addr_len = this->remote_addr.length();
                    OVERLAPPED* pov = this->thread_iocp->new_request(
                        [this] (OVERLAPPED* _ov) NOEXCEPT {
                        this->recv_completion(_ov); });

                    error = ::WSARecvFrom(
                        this->socket.get(), 
                        &wsabuf, 
                        1, 
                        nullptr, 
                        &this->recv_flags, 
                        this->remote_addr.sockaddr(), 
                        &this->remote_addr_len,
                        pov,
                        nullptr);

                    if (SOCKET_ERROR == error) {
                        error = ::WSAGetLastError();
                        if (WSA_IO_PENDING == error) {
                            // pending is not an error
                            error = NO_ERROR; 
                        } else {
                            this->thread_iocp->cancel_request(pov);
                            if (WSAECONNRESET == error) {
                                // when this fails on retry, it has already failed from a prior WSARecvFrom request
                                // - no need to continue to log it and fill up the error log
                            } else {
                                ctsConfig::PrintErrorInfo(
                                    L"WSARecvFrom failed (SOCKET %Iu) with error (%d)",
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
            catch (const std::exception& e) {
                ctsConfig::PrintException(e);
                error = ERROR_OUTOFMEMORY;
            }

            if (error != NO_ERROR && error != WSAECONNRESET) {
                ++failure_counter;

                ctsConfig::PrintErrorInfo(
                    L"MediaStream Server : WSARecvFrom failed (%d) %u times in a row trying to get another recv posted",
                    error, failure_counter);

                ctl::ctFatalCondition(
                    (0 == failure_counter % 10),
                    L"ctsMediaStreamServer has failed to post another recv - it cannot accept any more client connections");

                ::Sleep(10);
            }
        }
    }

    void ctsMediaStreamServerListeningSocket::recv_completion(OVERLAPPED* _ov) NOEXCEPT
    {
        // Cannot be holding the object_guard when calling into any pimpl-> methods
        // - will risk deadlocking the server
        // Will store the pimpl call to be made in this std function to be exeucted outside the lock
        std::function<void()> pimpl_operation(nullptr);

        try {
            // scope to the object lock
            {
                // must take the object lock before touching this->socket
                const ctl::ctAutoReleaseCriticalSection lock_object(&this->object_guard);

                if (INVALID_SOCKET == this->socket.get()) {
                    // the listening socket was closed - just exit
                    return;
                }

                DWORD bytes_received;
                if (!::WSAGetOverlappedResult(this->socket.get(), _ov, &bytes_received, FALSE, &this->recv_flags)) {
                    // recvfrom failed
                    try {
                        const auto gle = ::WSAGetLastError();
                        if (WSAECONNRESET == gle) {
                            ctsConfig::PrintErrorInfo(
                                L"ctsMediaStreamServer - WSARecvFrom failed as the prior WSASendTo(%ws) failed with port unreachable",
                                this->remote_addr.writeCompleteAddress().c_str());
                        } else {
                            ctsConfig::PrintErrorInfo(
                                L"ctsMediaStreamServer - WSARecvFrom failed [%d]",
                                ::WSAGetLastError());
                        }
                    }
                    catch (const std::exception&) {
                        // best effort
                    }

                    // this receive failed - do nothing immediately in response
                    // - just attempt to post another recv at the end of this function

                } else {
                    const ctsMediaStreamMessage message(ctsMediaStreamMessage::Extract(this->recv_buffer.data(), bytes_received));
                    switch (message.action) {
                        case MediaStreamAction::START:
                            PrintDebugInfo(
                                L"\t\tctsMediaStreamServer - processing START from %ws\n",
                                this->remote_addr.writeCompleteAddress().c_str());
#ifndef TESTING_IGNORE_START
                            // Cannot be holding the object_guard when calling into any pimpl-> methods
                            pimpl_operation = [this] () {
                                ctsMediaStreamServerImpl::start(this->socket, this->listening_addr, this->remote_addr);
                            };
#endif
                            break;

                        default:
                            ctl::ctAlwaysFatalCondition(L"ctsMediaStreamServer - received an unexpected Action: %d (%p)\n", message.action, this->recv_buffer.data());
                    }
                }
            }

            // now execute the stored call outside the lock but inside the try/catch
            if (pimpl_operation) {
                pimpl_operation();
            }
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
        }

        // finally post another recv
        this->initiate_recv();
    }

} // namespace
