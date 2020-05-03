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
#include <utility>
// os headers
#include <Windows.h>
#include <WinSock2.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
#include <ctString.hpp>
// project headers
#include "ctsMediaStreamServerListeningSocket.h"
#include "ctsMediaStreamServer.h"
#include "ctsMediaStreamProtocol.hpp"
#include "ctsConfig.h"

namespace ctsTraffic {

    ctsMediaStreamServerListeningSocket::ctsMediaStreamServerListeningSocket(wil::unique_socket&& _listening_socket, ctl::ctSockaddr _listening_addr) :
        thread_iocp(std::make_shared<ctl::ctThreadIocp>(_listening_socket.get(), ctsConfig::Settings->PTPEnvironment)),
        listening_socket(std::move(_listening_socket)),
        listening_addr(std::move(_listening_addr))
    {
        FAIL_FAST_IF_MSG(
            !!(ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP),
            "ctsMediaStream sockets must not have HANDLE_INLINE_IOCP set on its datagram sockets");
    }

    ctsMediaStreamServerListeningSocket::~ctsMediaStreamServerListeningSocket() noexcept
    {
        // close the socket, then end the TP
        {
            const auto lock = listeningsocket_lock.lock();
            listening_socket.reset();
        }
        thread_iocp.reset();
    }

    SOCKET ctsMediaStreamServerListeningSocket::get_socket() const noexcept
    {
        const auto lock = listeningsocket_lock.lock();
        return listening_socket.get();
    }

    ctl::ctSockaddr ctsMediaStreamServerListeningSocket::get_address() const noexcept
    {
        return listening_addr;
    }

    void ctsMediaStreamServerListeningSocket::initiate_recv() noexcept
    {
        // continue to try to post a recv if the call fails
        int error = SOCKET_ERROR;
        unsigned long failure_counter = 0;
        while (error != NO_ERROR)
        {
            try
            {
                const auto lock = listeningsocket_lock.lock();
                if (listening_socket)
                {
                    WSABUF wsabuf;
                    wsabuf.buf = recv_buffer.data();
                    wsabuf.len = static_cast<ULONG>(recv_buffer.size());
                    ::ZeroMemory(recv_buffer.data(), recv_buffer.size());

                    recv_flags = 0;
                    remote_addr.set(remote_addr.family(), ctl::ctSockaddr::AddressType::Any);
                    remote_addr_len = remote_addr.length();
                    OVERLAPPED* pov = thread_iocp->new_request(
                        [this](OVERLAPPED* _ov) noexcept {
                            recv_completion(_ov); });

                    error = WSARecvFrom(
                        listening_socket.get(),
                        &wsabuf,
                        1,
                        nullptr,
                        &recv_flags,
                        remote_addr.sockaddr(),
                        &remote_addr_len,
                        pov,
                        nullptr);
                    if (SOCKET_ERROR == error)
                    {
                        error = WSAGetLastError();
                        if (WSA_IO_PENDING == error)
                        {
                            // pending is not an error
                            error = NO_ERROR;
                        }
                        else
                        {
                            thread_iocp->cancel_request(pov);
                            if (WSAECONNRESET == error)
                            {
                                // when this fails on retry, it has already failed from a prior WSARecvFrom request
                                // - no need to continue to log it and fill up the error log
                            }
                            else
                            {
                                ctsConfig::PrintErrorInfo(
                                    ctl::ctString::ctFormatString("WSARecvFrom failed (SOCKET %Iu) with error (%d)",
                                        listening_socket.get(),
                                        error).c_str());
                            }
                        }
                    }
                    else
                    {
                        error = NO_ERROR;
                    }
                }
                else
                {
                    // if we no longer have a socket exit the loop
                    break;
                }
            }
            catch (const std::exception& e)
            {
                ctsConfig::PrintException(e);
                error = ERROR_OUTOFMEMORY;
            }

            if (error != NO_ERROR && error != WSAECONNRESET)
            {
                ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                ++failure_counter;

                try
                {
                    ctsConfig::PrintErrorInfo(
                        ctl::ctString::ctFormatString("MediaStream Server : WSARecvFrom failed (%d) %u times in a row trying to get another recv posted",
                            error, failure_counter).c_str());
                }
                catch (...)
                {
                }

                FAIL_FAST_IF_MSG(
                    0 == failure_counter % 10,
                    "ctsMediaStreamServer has failed to post another recv - it cannot accept any more client connections");

                Sleep(10);
            }
        }
    }

    void ctsMediaStreamServerListeningSocket::recv_completion(OVERLAPPED* _ov) noexcept
    {
        // Cannot be holding the object_guard when calling into any pimpl-> methods
        // - will risk deadlocking the server
        // Will store the pimpl call to be made in this std function to be exeucted outside the lock
        std::function<void()> pimpl_operation(nullptr);

        try
        {
            // scope to the object lock
            {
                // must take the object lock before touching socket
                const auto lock = listeningsocket_lock.lock();
                if (!listening_socket)
                {
                    // the listening socket was closed - just exit
                    _Analysis_assume_lock_released_(socket_lock);
                    return;
                }

                DWORD bytes_received;
                if (!WSAGetOverlappedResult(listening_socket.get(), _ov, &bytes_received, FALSE, &recv_flags))
                {
                    // recvfrom failed
                    try
                    {
                        const auto gle = WSAGetLastError();
                        if (WSAECONNRESET == gle)
                        {
                            ctsConfig::PrintErrorInfo(
                                ctl::ctString::ctFormatString("ctsMediaStreamServer - WSARecvFrom failed as the prior WSASendTo(%ws) failed with port unreachable",
                                    remote_addr.WriteCompleteAddress().c_str()).c_str());
                        }
                        else
                        {
                            ctsConfig::PrintErrorInfo(
                                ctl::ctString::ctFormatString("ctsMediaStreamServer - WSARecvFrom failed [%d]",
                                    WSAGetLastError()).c_str());
                        }
                    }
                    catch (...)
                    {
                        // best effort
                    }
                    // this receive failed - do nothing immediately in response
                    // - just attempt to post another recv at the end of this function
                }
                else
                {
                    const ctsMediaStreamMessage message(ctsMediaStreamMessage::Extract(recv_buffer.data(), bytes_received));
                    switch (message.action)
                    {
                        case MediaStreamAction::START:
                            PrintDebugInfo(
                                L"\t\tctsMediaStreamServer - processing START from %ws\n",
                                remote_addr.WriteCompleteAddress().c_str());
#ifndef TESTING_IGNORE_START
                            // Cannot be holding the object_guard when calling into any pimpl-> methods
                            pimpl_operation = [this]() {
                                ctsMediaStreamServerImpl::Start(listening_socket.get(), listening_addr, remote_addr);
                            };
#endif
                            break;

                        default:
                            FAIL_FAST_MSG("ctsMediaStreamServer - received an unexpected Action: %d (%p)\n", message.action, recv_buffer.data());
                    }
                }
            }

            // now execute the stored call outside the lock but inside the try/catch
            if (pimpl_operation)
            {
                pimpl_operation();
            }
        }
        catch (const std::exception& e)
        {
            ctsConfig::PrintException(e);
        }

        // finally post another recv
        initiate_recv();
    }

} // namespace
