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
#include <wil/stl.h>
#include <wil/resource.h>
// ctl headers
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
// project headers
#include "ctsMediaStreamServerListeningSocket.h"
#include "ctsMediaStreamServer.h"
#include "ctsMediaStreamProtocol.hpp"
#include "ctsConfig.h"

namespace ctsTraffic
{
    ctsMediaStreamServerListeningSocket::ctsMediaStreamServerListeningSocket(wil::unique_socket&& _listening_socket, ctl::ctSockaddr _listening_addr) :
        threadIocp(std::make_shared<ctl::ctThreadIocp>(_listening_socket.get(), ctsConfig::Settings->PTPEnvironment)),
        listeningSocket(std::move(_listening_socket)),
        listeningAddr(std::move(_listening_addr))
    {
        FAIL_FAST_IF_MSG(
            !!(ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP),
            "ctsMediaStream sockets must not have HANDLE_INLINE_IOCP set on its datagram sockets");
    }

    ctsMediaStreamServerListeningSocket::~ctsMediaStreamServerListeningSocket() noexcept
    {
        // close the socket, then end the TP
        {
            const auto lock = listeningsocketLock.lock();
            listeningSocket.reset();
        }
        threadIocp.reset();
    }

    SOCKET ctsMediaStreamServerListeningSocket::get_socket() const noexcept
    {
        const auto lock = listeningsocketLock.lock();
        return listeningSocket.get();
    }

    ctl::ctSockaddr ctsMediaStreamServerListeningSocket::get_address() const noexcept
    {
        return listeningAddr;
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
                const auto lock = listeningsocketLock.lock();
                if (listeningSocket)
                {
                    WSABUF wsabuf;
                    wsabuf.buf = recv_buffer.data();
                    wsabuf.len = static_cast<ULONG>(recv_buffer.size());
                    ::ZeroMemory(recv_buffer.data(), recv_buffer.size());

                    recvFlags = 0;
                    remoteAddr.set(remoteAddr.family(), ctl::ctSockaddr::AddressType::Any);
                    remoteAddrLen = remoteAddr.length();
                    OVERLAPPED* pov = threadIocp->new_request(
                        [this](OVERLAPPED* _ov) noexcept {
                            recv_completion(_ov); });

                    error = WSARecvFrom(
                        listeningSocket.get(),
                        &wsabuf,
                        1,
                        nullptr,
                        &recvFlags,
                        remoteAddr.sockaddr(),
                        &remoteAddrLen,
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
                            threadIocp->cancel_request(pov);
                            if (WSAECONNRESET == error)
                            {
                                // when this fails on retry, it has already failed from a prior WSARecvFrom request
                                // - no need to continue to log it and fill up the error log
                            }
                            else
                            {
                                ctsConfig::PrintErrorInfo(
                                    wil::str_printf<std::wstring>(L"WSARecvFrom failed (SOCKET %Iu) with error (%d)",
                                        listeningSocket.get(),
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
            catch (...)
            {
                error = ctsConfig::PrintThrownException();
            }

            if (error != NO_ERROR && error != WSAECONNRESET)
            {
                ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                ++failure_counter;

                try
                {
                    ctsConfig::PrintErrorInfo(
                        wil::str_printf<std::wstring>(L"MediaStream Server : WSARecvFrom failed (%d) %u times in a row trying to get another recv posted",
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
                const auto lock = listeningsocketLock.lock();
                if (!listeningSocket)
                {
                    // the listening socket was closed - just exit
                    return;
                }

                DWORD bytes_received;
                if (!WSAGetOverlappedResult(listeningSocket.get(), _ov, &bytes_received, FALSE, &recvFlags))
                {
                    // recvfrom failed
                    try
                    {
                        const auto gle = WSAGetLastError();
                        if (WSAECONNRESET == gle)
                        {
                            if (!priorFailureWasConectionReset)
                            {
                                ctsConfig::PrintErrorInfo(L"ctsMediaStreamServer - WSARecvFrom failed as a prior WSASendTo from this socket silently failed with port unreachable");
                            }
                            priorFailureWasConectionReset = true;
                        }
                        else
                        {
                            ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                            priorFailureWasConectionReset = false;
                            ctsConfig::PrintErrorInfo(
                                wil::str_printf<std::wstring>(L"ctsMediaStreamServer - WSARecvFrom failed [%d]", WSAGetLastError()).c_str());
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
                    priorFailureWasConectionReset = false;
                    const ctsMediaStreamMessage message(ctsMediaStreamMessage::Extract(recv_buffer.data(), bytes_received));
                    switch (message.action)
                    {
                        case MediaStreamAction::START:
                            PRINT_DEBUG_INFO(
                                L"\t\tctsMediaStreamServer - processing START from %ws\n",
                                remoteAddr.WriteCompleteAddress().c_str())
#ifndef TESTING_IGNORE_START
                            // Cannot be holding the object_guard when calling into any pimpl-> methods
                            pimpl_operation = [this]() {
                                ctsMediaStreamServerImpl::Start(listeningSocket.get(), listeningAddr, remoteAddr);
                            };
#endif
                            break;

                        default:  // NOLINT(clang-diagnostic-covered-switch-default)
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
        catch (...)
        {
            ctsConfig::PrintThrownException();
        }

        // finally post another recv
        initiate_recv();
    }

} // namespace
