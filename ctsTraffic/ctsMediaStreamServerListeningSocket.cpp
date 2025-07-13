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
// ctl headers
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
// project headers
#include "ctsMediaStreamServerListeningSocket.h"
#include "ctsMediaStreamServer.h"
#include "ctsMediaStreamProtocol.hpp"
#include "ctsConfig.h"
// wil headers always included last
#include <wil/stl.h>
#include <wil/resource.h>

namespace ctsTraffic
{
ctsMediaStreamServerListeningSocket::ctsMediaStreamServerListeningSocket(wil::unique_socket&& listeningSocket, ctl::ctSockaddr listeningAddr) :
    m_threadIocp(std::make_shared<ctl::ctThreadIocp>(listeningSocket.get(), ctsConfig::g_configSettings->pTpEnvironment)),
    m_listeningSocket(std::move(listeningSocket)),
    m_listeningAddr(std::move(listeningAddr))
{
    FAIL_FAST_IF_MSG(
        !!(ctsConfig::g_configSettings->Options & ctsConfig::OptionType::HandleInlineIocp),
        "ctsMediaStream sockets must not have HANDLE_INLINE_IOCP set on its datagram sockets");
}

ctsMediaStreamServerListeningSocket::~ctsMediaStreamServerListeningSocket() noexcept
{
    // close the socket, then end the TP
    {
        const auto lock = m_listeningSocketLock.lock();
        m_listeningSocket.reset();
    }
    m_threadIocp.reset();
}

SOCKET ctsMediaStreamServerListeningSocket::GetSocket() const noexcept
{
    const auto lock = m_listeningSocketLock.lock();
    return m_listeningSocket.get();
}

ctl::ctSockaddr ctsMediaStreamServerListeningSocket::GetListeningAddress() const noexcept
{
    return m_listeningAddr;
}

void ctsMediaStreamServerListeningSocket::InitiateRecv() noexcept
{
    // continue to try to post a recv if the call fails
    int error = SOCKET_ERROR;
    uint32_t failureCounter = 0;
    while (error != NO_ERROR)
    {
        try
        {
            const auto lock = m_listeningSocketLock.lock();
            if (m_listeningSocket)
            {
                WSABUF wsaBuffer{};
                wsaBuffer.buf = m_recvBuffer.data();
                wsaBuffer.len = static_cast<ULONG>(m_recvBuffer.size());
                ::ZeroMemory(m_recvBuffer.data(), m_recvBuffer.size());

                m_recvFlags = 0;
                m_remoteAddr.reset(m_remoteAddr.family(), ctl::ctSockaddr::AddressType::Any);
                m_remoteAddrLen = m_remoteAddr.length();
                OVERLAPPED* pOverlapped = m_threadIocp->new_request(
                    [this](OVERLAPPED* pCallbackOverlapped) noexcept {
                        RecvCompletion(pCallbackOverlapped);
                    });

                error = WSARecvFrom(
                    m_listeningSocket.get(),
                    &wsaBuffer,
                    1,
                    nullptr,
                    &m_recvFlags,
                    m_remoteAddr.sockaddr(),
                    &m_remoteAddrLen,
                    pOverlapped,
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
                        m_threadIocp->cancel_request(pOverlapped);
                        if (WSAECONNRESET == error)
                        {
                            // when this fails on retry, it has already failed from a prior WSARecvFrom request
                            // - no need to continue to log it and fill up the error log
                        }
                        else
                        {
                            ctsConfig::PrintErrorInfo(
                                L"WSARecvFrom failed (SOCKET %Iu) with error (%d)",
                                m_listeningSocket.get(), error);
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
            error = static_cast<int>(ctsConfig::PrintThrownException());
        }

        if (error != NO_ERROR && error != WSAECONNRESET)
        {
            ctsConfig::g_configSettings->UdpStatusDetails.m_errorFrames.Increment();
            ++failureCounter;

            ctsConfig::PrintErrorInfo(
                L"MediaStream Server : WSARecvFrom failed (%d) %u times in a row trying to get another recv posted",
                error, failureCounter);

            FAIL_FAST_IF_MSG(
                0 == failureCounter % 10,
                "ctsMediaStreamServer has failed to post another recv - it cannot accept any more client connections");

            Sleep(10);
        }
    }
}

void ctsMediaStreamServerListeningSocket::RecvCompletion(OVERLAPPED* pOverlapped) noexcept
{
    // Cannot be holding the object_guard when calling into any pimpl-> methods
    // - will risk deadlocking the server
    // Will store the pimpl call to be made in this std function to be exeucted outside the lock
    std::function<void()> pimplOperation(nullptr);

    try
    {
        // scope to the object lock
        {
            // must take the object lock before touching socket
            const auto lock = m_listeningSocketLock.lock();
            if (!m_listeningSocket)
            {
                // the listening socket was closed - just exit
                return;
            }

            DWORD bytesReceived{};
            if (!WSAGetOverlappedResult(m_listeningSocket.get(), pOverlapped, &bytesReceived, FALSE, &m_recvFlags))
            {
                // recvfrom failed
                if (WSAECONNRESET == WSAGetLastError())
                {
                    if (!m_priorFailureWasConnectionReset)
                    {
                        ctsConfig::PrintErrorInfo(L"ctsMediaStreamServer - WSARecvFrom failed as a prior WSASendTo from this socket silently failed with port unreachable");
                    }
                    m_priorFailureWasConnectionReset = true;
                }
                else
                {
                    ctsConfig::PrintErrorInfo(
                        L"ctsMediaStreamServer - WSARecvFrom failed [%d]", WSAGetLastError());
                    ctsConfig::g_configSettings->UdpStatusDetails.m_errorFrames.Increment();
                    m_priorFailureWasConnectionReset = false;
                }
                // this receive-call failed - do nothing immediately in response
                // - just attempt to post another recv at the end of this function
            }
            else
            {
                m_priorFailureWasConnectionReset = false;
                const ctsMediaStreamMessage message(ctsMediaStreamMessage::Extract(m_recvBuffer.data(), bytesReceived));
                switch (message.m_action)
                {
                    case MediaStreamAction::START:
                        PRINT_DEBUG_INFO(
                            L"\t\tctsMediaStreamServer - processing START from %ws\n",
                            m_remoteAddr.writeCompleteAddress().c_str());
#ifndef TESTING_IGNORE_START
                    // Cannot be holding the object_guard when calling into any pimpl-> methods
                        pimplOperation = [this] {
                            ctsMediaStreamServerImpl::Start(m_listeningSocket.get(), m_listeningAddr, m_remoteAddr);
                        };
#endif
                        break;

                    default: // NOLINT(clang-diagnostic-covered-switch-default)
                        FAIL_FAST_MSG("ctsMediaStreamServer - received an unexpected Action: %d (%p)\n", message.m_action, m_recvBuffer.data());
                }
            }
        }

        // now execute the stored call outside the lock but inside the try/catch
        if (pimplOperation)
        {
            pimplOperation();
        }
    }
    catch (...)
    {
        ctsConfig::PrintThrownException();
    }

    // finally post another recv
    InitiateRecv();
}
} // namespace
