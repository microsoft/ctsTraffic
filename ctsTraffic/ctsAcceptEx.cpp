/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <memory>
#include <utility>
#include <vector>
#include <queue>
// os headers
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
// ctl headers
#include <ctSocketExtensions.hpp>
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
// project headers
#include "ctsSocket.h"

using ctsTraffic::ctsConfig::g_configSettings;

namespace ctsTraffic
{
//
// Requirements:
// - must be able to accept a connection from all listening sockets (cannot round-robin listeners)
// - must return one accepted socket only after operator() is invoked
//
// General Algorithm
// - initiate an AcceptEx on every address at startup (after posting a listen)
// - a common "accept handler" routine is invoked directly from both:
// --- operator()
// --- the IOCP callback function
//
// The 'accept handler' manages the interation between returning connections and posting more AcceptEx calls:
//
// - if operator() is called and a connection is ready,
// --- set_socket() and complete() are invoked
// --- a new AcceptEx call is posted on that listening socket
//
// - if operator() is called and no connection is ready,
// --- a counter is incremented to reflect a request arrived
//
// - if the callback is called and the counter reflects a request for a new socket is pending,
// --- set_socket() and complete() are invoked
// --- decrement the counter tracking requests
// --- a new AcceptEx call is posted on that listening socket
// - if the callback is called and the counter reflects no request arrived yet,
// --- the new connection is added to a queue and AcceptEx is not reposted
//
namespace details
{
    //
    // constant defining how many acceptex requests we want maintained per listener
    //
    constexpr uint32_t c_pendedAcceptRequests = 100;

    //
    // necessary forward declarations of internal classes
    //
    struct ctsAcceptExImpl;
    class ctsAcceptSocketInfo;

    static void ctsAcceptExIoCompletionCallback(OVERLAPPED*, _In_ ctsAcceptSocketInfo* acceptInfo) noexcept;

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// struct to capture relevant details of an accepted connection
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    struct ctsAcceptedConnection
    {
        wil::unique_socket m_acceptSocket;
        ctl::ctSockaddr m_localAddr;
        ctl::ctSockaddr m_remoteAddr;
        DWORD m_lastError = 0;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Struct to track listening sockets
    /// - must have a unique IOCP class for each listener
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    struct ctsListenSocketInfo
    {
        // c'tor throws a wil::ResultException or bad_alloc on failure
        explicit ctsListenSocketInfo(ctl::ctSockaddr addr) :
            m_sockaddr(std::move(addr))
        {
            wil::unique_socket tempsocket(
                ctsConfig::CreateSocket(m_sockaddr.family(), SOCK_STREAM, IPPROTO_TCP, g_configSettings->SocketFlags));

            auto error = ctsConfig::SetPreBindOptions(tempsocket.get(), m_sockaddr);
            if (error != 0)
            {
                THROW_WIN32_MSG(error, "ctsConfig::SetPreBindOptions (ctsAcceptEx)");
            }

            if (SOCKET_ERROR == bind(tempsocket.get(), m_sockaddr.sockaddr(), m_sockaddr.length()))
            {
                error = WSAGetLastError();
                char addrBuffer[ctl::ctSockaddr::FixedStringLength]{};
                m_sockaddr.writeCompleteAddress(addrBuffer);
                THROW_WIN32_MSG(error, "bind %hs (ctsAcceptEx)", addrBuffer);
            }

            if (SOCKET_ERROR == listen(tempsocket.get(), ctsConfig::GetListenBacklog()))
            {
                error = WSAGetLastError();
                THROW_WIN32_MSG(error, "listen (ctsAcceptEx)");
            }

            m_iocp = std::make_unique<ctl::ctThreadIocp>(tempsocket.get(), g_configSettings->pTpEnvironment);

            // now save the socket after everything succeeded
            m_listenSocket = std::move(tempsocket);
        }

        ~ctsListenSocketInfo() noexcept
        {
            // close the socket then wait for all IO to stop
            m_listenSocket.reset();
            m_iocp.reset();
        }

        ctsListenSocketInfo(const ctsListenSocketInfo&) = delete;
        ctsListenSocketInfo& operator=(const ctsListenSocketInfo&) = delete;
        ctsListenSocketInfo(ctsListenSocketInfo&&) = delete;
        ctsListenSocketInfo& operator=(ctsListenSocketInfo&&) = delete;

        wil::unique_socket m_listenSocket;
        ctl::ctSockaddr m_sockaddr;
        std::unique_ptr<ctl::ctThreadIocp> m_iocp;
        std::vector<std::shared_ptr<ctsAcceptSocketInfo>> m_acceptSockets;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// struct to track accepted sockets
    /// - tracks the 'parent' listen socket structure
    /// - preallocates the buffer to use for AcceptEx calls
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsAcceptSocketInfo
    {
    public:
        // c'tor throws wil::ResultException on failure
        explicit ctsAcceptSocketInfo(const std::shared_ptr<ctsListenSocketInfo>& listenSocket) noexcept :
            m_listeningSocketInfo(listenSocket)
        {
        }

        ~ctsAcceptSocketInfo() noexcept = default;

        // attempts to post a new AcceptEx - internally tracks if succeeds or fails
        void InitatiateAcceptEx();

        // returns a ctsAcceptedConnection struct describing the result of an AcceptEx call
        // - must be called only after the previous AcceptEx call has completed its OVERLAPPED call
        ctsAcceptedConnection GetAcceptedSocket() noexcept;

        // non-copyable
        ctsAcceptSocketInfo(const ctsAcceptSocketInfo&) = delete;
        ctsAcceptSocketInfo& operator=(const ctsAcceptSocketInfo&) = delete;
        ctsAcceptSocketInfo(ctsAcceptSocketInfo&&) = delete;
        ctsAcceptSocketInfo& operator=(ctsAcceptSocketInfo&&) = delete;

    private:
        static constexpr size_t c_singleOutputBufferSize = sizeof(SOCKADDR_INET) + 16;

        // the lock to guard access to the SOCKET
        wil::critical_section m_lock{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
        wil::unique_socket m_acceptSocket;
        // the raw (non-owning) OVERLAPPED* for the AcceptEx request
        OVERLAPPED* m_pOverlapped = nullptr;
        // a weak reference back to the parent listening object
        const std::weak_ptr<ctsListenSocketInfo> m_listeningSocketInfo;
        // the buffer to supply to AcceptEx to capture the address information
        char m_outputBuffer[c_singleOutputBufferSize * 2]{};
    };

    //
    // Impl object to carry around the real member data of ctsAcceptEx
    // - the shared_ptr to the Impl allows an instance of ctsAcceptEx to be copyable
    //
    struct ctsAcceptExImpl
    {
        // must guard access to internal containers
        wil::critical_section m_lock{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
        std::vector<std::shared_ptr<ctsListenSocketInfo>> m_listeners;
        std::queue<std::weak_ptr<ctsSocket>> m_pendedAcceptRequests;
        std::queue<ctsAcceptedConnection> m_acceptedConnections;
        bool m_shuttingDown = false;

        //
        // ctsAcceptExImpl constructor
        // - start listening on all addresses specified tracked in ctsListenSocketInfo objects
        // - create ctsAcceptSocketInfo object to manage attempts to accept new connections
        // --- one object per accept socket
        //
        ctsAcceptExImpl() = default;

        void Start()
        {
            // swap in the listen vector only if fully created
            // - if anything fails, this temp vector will go out of scope and safely be destroyed
            std::vector<std::shared_ptr<ctsListenSocketInfo>> tempListeners;

            // listen to each address
            for (const auto& addr : g_configSettings->ListenAddresses)
            {
                try
                {
                    // Make the structures for the listener and its accept sockets
                    auto listenSocketInfo(std::make_shared<ctsListenSocketInfo>(addr));
                    PRINT_DEBUG_INFO(L"\t\tListening to %ws\n", addr.writeCompleteAddress().c_str());
                    //
                    // Add PendedAcceptRequests pended acceptex objects per listener
                    //
                    for (auto acceptCounter = 0ul; acceptCounter < c_pendedAcceptRequests; ++acceptCounter)
                    {
                        auto acceptSocketInfo = std::make_shared<ctsAcceptSocketInfo>(listenSocketInfo);
                        listenSocketInfo->m_acceptSockets.push_back(acceptSocketInfo);
                        // post AcceptEx on this socket
                        acceptSocketInfo->InitatiateAcceptEx();
                    }

                    // all successful - save this listen socket
                    tempListeners.push_back(listenSocketInfo);
                }
                catch (...)
                {
                    ctsConfig::PrintThrownException();
                }
            }

            if (tempListeners.empty())
            {
                throw std::exception("ctsAcceptEx invoked with no listening sockets successfully created");
            }

            // everything succeeded - safely save the listen queue
            m_listeners.swap(tempListeners);
        }

        ~ctsAcceptExImpl() noexcept
        {
            // remove anything pended under lock since the IOCP callbacks still might be invoked
            {
                const auto lock = m_lock.lock();
                m_shuttingDown = true;

                // close out all caller requests for new accepted sockets
                while (!m_pendedAcceptRequests.empty())
                {
                    auto weakSocket = m_pendedAcceptRequests.front();

                    if (const auto sharedSocket = weakSocket.lock())
                    {
                        sharedSocket->CompleteState(WSAECONNABORTED);
                    }

                    m_pendedAcceptRequests.pop();
                }

                while (!m_acceptedConnections.empty())
                {
                    m_acceptedConnections.pop();
                }
            }

            // now stop the listeners and accepted sockets
            m_listeners.clear();
        }

        // non-copyable
        ctsAcceptExImpl(const ctsAcceptExImpl&) = delete;
        ctsAcceptExImpl& operator=(const ctsAcceptExImpl&) = delete;
        ctsAcceptExImpl(ctsAcceptExImpl&&) = delete;
        ctsAcceptExImpl& operator=(ctsAcceptExImpl&&) = delete;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///
    /// Definitions of ctsAcceptSocketInfo members
    ///
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsAcceptSocketInfo::InitatiateAcceptEx()
    {
        const auto listeningSocketObject = m_listeningSocketInfo.lock();
        if (!listeningSocketObject)
        {
            return;
        }

        const auto lock = m_lock.lock();

        if (m_acceptSocket.get() != INVALID_SOCKET)
        {
            return;
        }

        wil::unique_socket newAcceptedSocket(
            ctsConfig::CreateSocket(
                listeningSocketObject->m_sockaddr.family(),
                SOCK_STREAM,
                IPPROTO_TCP,
                g_configSettings->SocketFlags));

        // since not inheriting from the listening socket, must explicity set options on the accept socket
        // - passing the listening address since that will be the local address of this accepted socket
        auto error = ctsConfig::SetPreBindOptions(newAcceptedSocket.get(), listeningSocketObject->m_sockaddr);
        if (error != 0)
        {
            THROW_WIN32_MSG(error, "SetPreBindOptions (ctsAcceptEx)");
        }
        error = ctsConfig::SetPreConnectOptions(newAcceptedSocket.get());
        if (error != 0)
        {
            THROW_WIN32_MSG(error, "SetPreConnectOptions (ctsAcceptEx)");
        }

        m_pOverlapped = listeningSocketObject->m_iocp->new_request(
            [this](OVERLAPPED* pCallbackOverlapped) noexcept { ctsAcceptExIoCompletionCallback(pCallbackOverlapped, this); });

        ::ZeroMemory(m_outputBuffer, c_singleOutputBufferSize * 2);
        DWORD bytesReceived{};
        if (!ctl::ctAcceptEx(
            listeningSocketObject->m_listenSocket.get(),
            newAcceptedSocket.get(),
            m_outputBuffer,
            0, c_singleOutputBufferSize, c_singleOutputBufferSize,
            &bytesReceived,
            m_pOverlapped))
        {
            error = WSAGetLastError();
            if (ERROR_IO_PENDING != error)
            {
                // a real failure - must abort the IO
                listeningSocketObject->m_iocp->cancel_request(m_pOverlapped);
                m_pOverlapped = nullptr;
                ctsConfig::PrintErrorIfFailed("AcceptEx", error);
                return;
            }
        }
        else if (g_configSettings->Options & ctsConfig::OptionType::HandleInlineIocp)
        {
            // AcceptEx completed inline - directly invoke the callback to handle the completion
            // - after canceling the TP request
            listeningSocketObject->m_iocp->cancel_request(m_pOverlapped);
            m_pOverlapped = nullptr;
            ctsAcceptExIoCompletionCallback(nullptr, this);
        }

        // no failures - store the socket
        m_acceptSocket = std::move(newAcceptedSocket);
    }

    ctsAcceptedConnection ctsAcceptSocketInfo::GetAcceptedSocket() noexcept
    {
        ctsAcceptedConnection returnDetails;

        const auto listeningSocketObject = m_listeningSocketInfo.lock();
        if (!listeningSocketObject)
        {
            returnDetails.m_lastError = WSAECONNABORTED;
            m_acceptSocket.reset();
            // return empty/failed details object
            return returnDetails;
        }
        const auto listeningSocket = listeningSocketObject->m_listenSocket.get();

        const auto lock = m_lock.lock();

        // if the OVERLAPPED* is null, it means it completed inline (no OVERLAPPED async completion)
        // - thus we know it already succeeded
        if (m_pOverlapped)
        {
            DWORD transferred{};
            DWORD flags{};
            if (!WSAGetOverlappedResult(
                listeningSocket,
                m_pOverlapped,
                &transferred,
                FALSE,
                &flags))
            {
                returnDetails.m_lastError = WSAGetLastError();
                ctsConfig::PrintErrorIfFailed("AcceptEx", returnDetails.m_lastError);
                m_acceptSocket.reset();
                // return empty/failed details object
                return returnDetails;
            }
        }

        // if successful, update the socket context
        // this should never fail - break if it does to debug it
        const auto err = setsockopt(
            m_acceptSocket.get(),
            SOL_SOCKET,
            SO_UPDATE_ACCEPT_CONTEXT,
            reinterpret_cast<const char*>(&listeningSocket),
            sizeof listeningSocket);
        FAIL_FAST_IF_MSG(
            err != 0,
            "setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed [%d], accept socket [%zu], listen socket [%zu]",
            WSAGetLastError(), m_acceptSocket.get(), listeningSocket);

        SOCKADDR_INET* localAddr{};
        auto localAddrLen = static_cast<int>(sizeof SOCKADDR_INET);
        SOCKADDR_INET* remoteAddr{};
        auto remoteAddrLen = static_cast<int>(sizeof SOCKADDR_INET);

        ctl::ctGetAcceptExSockaddrs(
            m_outputBuffer,
            0,
            c_singleOutputBufferSize,
            c_singleOutputBufferSize,
            reinterpret_cast<sockaddr**>(&localAddr),
            &localAddrLen,
            reinterpret_cast<sockaddr**>(&remoteAddr),
            &remoteAddrLen);

        // transfer ownership of the SOCKET to the caller
        returnDetails.m_acceptSocket = std::move(m_acceptSocket);
        returnDetails.m_lastError = 0;
        returnDetails.m_localAddr.setSockaddr(localAddr);
        returnDetails.m_remoteAddr.setSockaddr(remoteAddr);

        return returnDetails;
    }

    ctsAcceptExImpl g_acceptExImpl; // NOLINT(clang-diagnostic-exit-time-destructors)
    // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
    static INIT_ONCE g_acceptExImplInitOnce = INIT_ONCE_STATIC_INIT;

    static BOOL CALLBACK ctsAcceptExImplInitFn(PINIT_ONCE, PVOID perror, PVOID*) noexcept try
    {
        g_acceptExImpl.Start();
        return TRUE;
    }
    catch (...)
    {
        *static_cast<DWORD*>(perror) = ctsConfig::PrintThrownException();
        return FALSE;
    }

    static void ctsAcceptExIoCompletionCallback(OVERLAPPED*, _In_ ctsAcceptSocketInfo* acceptInfo) noexcept try
    {
        ctsAcceptedConnection acceptedSocket = acceptInfo->GetAcceptedSocket();

        const auto lock = g_acceptExImpl.m_lock.lock();
        if (g_acceptExImpl.m_shuttingDown)
        {
            return;
        }

        if (!g_acceptExImpl.m_pendedAcceptRequests.empty())
        {
            //
            // we have unfulfilled requests for more connections
            // return a previously accepted socket
            //
            const auto weakSocket = g_acceptExImpl.m_pendedAcceptRequests.front();
            g_acceptExImpl.m_pendedAcceptRequests.pop();

            if (const auto sharedSocket = weakSocket.lock())
            {
                ctsConfig::PrintErrorIfFailed("AcceptEx", acceptedSocket.m_lastError);

                if (0 == acceptedSocket.m_lastError)
                {
                    // set the local addr
                    ctl::ctSockaddr localAddr;
                    int localAddrLen = localAddr.length();
                    if (0 == getsockname(acceptedSocket.m_acceptSocket.get(), localAddr.sockaddr(), &localAddrLen))
                    {
                        sharedSocket->SetLocalSockaddr(localAddr);
                    }

                    // socket ownership was successfully transfered
                    sharedSocket->SetSocket(acceptedSocket.m_acceptSocket.release());
                    sharedSocket->SetRemoteSockaddr(acceptedSocket.m_remoteAddr);
                    sharedSocket->CompleteState(0);

                    ctsConfig::PrintNewConnection(localAddr, acceptedSocket.m_remoteAddr);
                }
                else
                {
                    sharedSocket->CompleteState(acceptedSocket.m_lastError);
                }
            }
            else
            {
                // socket was closed from beneath us
                ctsConfig::PrintErrorIfFailed("AcceptEx", WSAECONNABORTED);
            }
        }
        else
        {
            //
            // else, we have no requests for another connection,
            // - queue this one for when a request comes in
            //
            g_acceptExImpl.m_acceptedConnections.push(std::move(acceptedSocket));
        }

        //
        // always attempt another AcceptEx
        //
        acceptInfo->InitatiateAcceptEx();
    }
    catch (...)
    {
        ctsConfig::PrintThrownException();
    }
} // namespace details

//
//
// An accepted socket is being requested
// - if there is one queued, return that
// - else store the weak_ptr<ctsSocket> to be fulfilled later
//
//
void ctsAcceptEx(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
{
    DWORD error = 0;
    if (!InitOnceExecuteOnce(&details::g_acceptExImplInitOnce, details::ctsAcceptExImplInitFn, &error, nullptr))
    {
        if (const auto sharedSocket = weakSocket.lock())
        {
            sharedSocket->CompleteState(error);
        }
        return;
    }

    auto sharedSocket(weakSocket.lock());
    if (!sharedSocket)
    {
        return;
    }

    details::ctsAcceptedConnection acceptedConnection;

    // scoped to the auto-release CS object
    {
        const auto lock = details::g_acceptExImpl.m_lock.lock();
        // guard access to internal queues
        if (details::g_acceptExImpl.m_acceptedConnections.empty())
        {
            // no accepted connections yet -- save the weak_ptr, *not* the shared_ptr
            try { details::g_acceptExImpl.m_pendedAcceptRequests.push(weakSocket); }
            catch (...)
            {
                // fail the caller if we can't save this request
                error = WSAENOBUFS;
            }
        }
        else
        {
            // pull the next connection off the queue
            acceptedConnection = std::move(details::g_acceptExImpl.m_acceptedConnections.front());
            details::g_acceptExImpl.m_acceptedConnections.pop();
            error = acceptedConnection.m_lastError;
        }
    }

    //
    // complete this socket state if something failed
    //
    ctsConfig::PrintErrorIfFailed("AcceptEx", error);
    if (error != 0)
    {
        sharedSocket->CompleteState(error);
        return;
    }

    //
    // if did not defer the accept request, and we have a new accepted socket,
    // complete this socket state
    //
    if (acceptedConnection.m_acceptSocket.get() != INVALID_SOCKET)
    {
        // set the local addr
        ctl::ctSockaddr localAddr;
        auto localAddrLen = localAddr.length();
        if (0 == getsockname(acceptedConnection.m_acceptSocket.get(), localAddr.sockaddr(), &localAddrLen))
        {
            sharedSocket->SetLocalSockaddr(localAddr);
        }

        // transfering ownership to the ctsSocket
        sharedSocket->SetSocket(acceptedConnection.m_acceptSocket.release());
        sharedSocket->SetRemoteSockaddr(acceptedConnection.m_remoteAddr);
        sharedSocket->CompleteState(0);

        ctsConfig::PrintNewConnection(localAddr, acceptedConnection.m_remoteAddr);
    }
}
} // namespace
