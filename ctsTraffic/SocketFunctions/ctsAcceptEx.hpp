/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <memory>
#include <vector>
#include <string>
#include <queue>
// os headers
#include <Windows.h>
#include <winsock2.h>
#include <mswsock.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctSocketExtensions.hpp>
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
#include <ctScopeGuard.hpp>
#include <ctLocks.hpp>
#include <ctTimer.hpp>
// project headers
#include "ctsSocket.h"


namespace ctsTraffic {

    class ctsAcceptEx {
        ///
        /// Requirements:
        /// - must be able to accept a connection from all listening sockets (cannot round-robin listeners)
        /// - must return one accepted socket only after operator() is invoked
        ///
        /// General Algorithm
        /// - initiate an AcceptEx on every address at startup (after posting a listen)
        /// - a common "accept handler" routine is invoked directly from both:
        /// --- operator()
        /// --- the IOCP callback function
        /// 
        /// The 'accept handler' manages the interation between returning connections and posting more AcceptEx calls:
        ///
        /// - if operator() is called and a connection is ready, 
        /// --- set_socket() and complete() are invoked
        /// --- a new AcceptEx call is posted on that listening socket
        ///
        /// - if operator() is called and no connection is ready, 
        /// --- a counter is incremented to reflect a request arrived
        ///
        /// - if the callback is called and the counter reflects a request for a new socket is pending, 
        /// --- set_socket() and complete() are invoked
        /// --- decrement the counter tracking requests
        /// --- a new AcceptEx call is posted on that listening socket
        /// - if the callback is called and the counter reflects no request arrived yet,
        /// --- the new connection is added to a queue and AcceptEx is not reposted
        ///

    private:
        ///
        /// constant defining how many acceptex requests we want maintained per listener
        ///
        static const unsigned PendedAcceptRequests = 100;

        ///
        /// necessary forward declarations of internal classes
        ///
        struct ctsAcceptExImpl;
        class ctsAcceptSocketInfo;


        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// struct to capture relevant details of an accepted connection
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        struct ctsAcceptedConnection {
            SOCKET accept_socket;
            DWORD  gle;
            ctl::ctSockaddr local_addr;
            ctl::ctSockaddr remote_addr;

            ctsAcceptedConnection() : accept_socket(INVALID_SOCKET), gle(0), local_addr(), remote_addr()
            {
            }
        };


        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Struct to track listening sockets
        /// - must have a unique IOCP class for each listener
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        struct ctsListenSocketInfo {
            // c'tor throws a ctException or bad_alloc on failure
            ctsListenSocketInfo(const ctl::ctSockaddr& _listening_addr);
            ~ctsListenSocketInfo() NOEXCEPT;

            // attempt to restart any accept sockets which failed last time they were attempted
            void RestartStalledAccepts(std::shared_ptr<ctsAcceptEx::ctsAcceptExImpl> _pimpl);

            SOCKET socket;
            ctl::ctSockaddr addr;
            std::shared_ptr<ctl::ctThreadIocp> iocp;
            std::vector<std::shared_ptr<ctsAcceptSocketInfo>> accept_sockets;
        };


        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// struct to track accepted sockets
        /// - tracks the 'parent' listen socket structure
        /// - preallocates the buffer to use for AcceptEx calls
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        class ctsAcceptSocketInfo {
        public:
            // c'tor throws ctException on failure
            ctsAcceptSocketInfo(std::shared_ptr<ctsListenSocketInfo>& _listen_socket);
            ~ctsAcceptSocketInfo() NOEXCEPT;

            // attempts to post a new AcceptEx - internally tracks if succeeds or fails
            void InitatiateAcceptEx(std::shared_ptr<ctsAcceptEx::ctsAcceptExImpl> _pimpl);

            // returns a ctsAcceptedConnection struct describing the result of an AcceptEx call
            // - must be called only after the previous AcceptEx call has completed its OVERLAPPED call
            ctsAcceptedConnection GetAcceptedSocket() NOEXCEPT;

            // non-copyable
            ctsAcceptSocketInfo(const ctsAcceptSocketInfo&) = delete;
            ctsAcceptSocketInfo& operator=(const ctsAcceptSocketInfo&) = delete;

        private:
            static const size_t SingleOutputBufferSize = sizeof (SOCKADDR_INET) +16;

            SOCKET socket;
            // the lock to guard access to the SOCKET
            CRITICAL_SECTION cs;
            // the OVERLAPPED* for the AcceptEx request
            OVERLAPPED* pov;
            // the listening socket handle - needed for AcceptEx
            SOCKET listening_socket;
            // the listening address to know how to create the matching accept socket
            ctl::ctSockaddr listening_addr;
            // the IOCP object that is associated with the listening socket
            std::shared_ptr<ctl::ctThreadIocp> listening_iocp;
            // the buffer to supply to AcceptEx to capture the address information
            char OutputBuffer[SingleOutputBufferSize * 2];

            // returns details of the addresses on an accepted socket after AcceptEx has completed successfully
            ctsAcceptedConnection make_sockaddr_details() NOEXCEPT;
        };



    private:
        ///
        ///
        /// Impl object to carry around the real member data of ctsAcceptEx
        /// - the shared_ptr to the Impl allows an instance of ctsAcceptEx to be copyable
        ///
        ///
        struct ctsAcceptExImpl {
            // must guard access to internal containers
            CRITICAL_SECTION cs;
            std::vector<std::shared_ptr<ctsListenSocketInfo>> listeners;
            std::queue<std::weak_ptr<ctsSocket>> pended_accept_requests;
            std::queue<ctsAcceptedConnection> accepted_connections;

            ctsAcceptExImpl() : cs(), listeners(), accepted_connections(), pended_accept_requests()
            {
                if (!::InitializeCriticalSectionAndSpinCount(&cs, 4000)) {
                    throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionAndSpinCount", L"ctsAcceptEx", false);
                }
            }

            ~ctsAcceptExImpl() NOEXCEPT
            {
                // close out all caller requests for new accepted sockets 
                while (!pended_accept_requests.empty()) {
                    auto weak_socket = pended_accept_requests.front();
                    auto shared_socket(weak_socket.lock());
                    if (shared_socket) {
                        shared_socket->complete_state(WSAECONNABORTED);
                    }

                    pended_accept_requests.pop();
                }

                listeners.clear();
                while (!accepted_connections.empty()) {
                    accepted_connections.pop();
                }

                ::DeleteCriticalSection(&cs);
            }

            // non-copyable
            ctsAcceptExImpl(const ctsAcceptExImpl&) = delete;
            ctsAcceptExImpl& operator=(const ctsAcceptExImpl&) = delete;
        };

        std::shared_ptr<ctsAcceptExImpl> pimpl;


    public:
        ///
        ///
        /// ctsAcceptEx constructor
        /// - start listening on all addresses specified tracked in ctsListenSocketInfo objects
        /// - create ctsAcceptSocketInfo object to manage attempts to accept new connections
        /// --- one object per accept socket
        ///
        ctsAcceptEx() : pimpl(new ctsAcceptExImpl)
        {
            ctl::ctAutoReleaseCriticalSection auto_lock(&pimpl->cs);

            // swap in the listen vector only if fully created
            // - if anything fails, this temp vector will go out of scope and safely be destroyed
            std::vector<std::shared_ptr<ctsListenSocketInfo>> temp_listeners;

            // listen to each address
            for (const auto& addr : ctsConfig::Settings->ListenAddresses) {
                // Make the structures for the listener and its accept sockets
                std::shared_ptr<ctsListenSocketInfo> listen_socket_info = std::make_shared<ctsListenSocketInfo>(addr);
                ctsConfig::PrintDebug(L"\t\tListening to %s\n", addr.writeCompleteAddress().c_str());
                //
                // Add PendedAcceptRequests pended acceptex objects per listener
                //
                for (unsigned accept_counter = 0; accept_counter < PendedAcceptRequests; ++accept_counter) {
                    std::shared_ptr<ctsAcceptSocketInfo> accept_socket_info = std::make_shared<ctsAcceptSocketInfo>(listen_socket_info);
                    listen_socket_info->accept_sockets.push_back(accept_socket_info);
                    // post AcceptEx on this socket
                    accept_socket_info->InitatiateAcceptEx(pimpl);
                }

                // all successful - save this listen socket
                temp_listeners.push_back(listen_socket_info);
            }

            if (temp_listeners.empty()) {
                throw std::exception("ctsAcceptEx invoked with no listening addresses specified");
            }

            // everything succeeded - safely save the listen queue
            pimpl->listeners.swap(temp_listeners);
        }

        //
        //
        // An accepted socket is being requested
        // - if have one queued, return that
        // - else store the weak_ptr<ctsSocket> to be fulfilled later
        //
        //
        void operator() (std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT
        {
            auto shared_socket(_weak_socket.lock());
            if (!shared_socket) {
                return;
            }

            ctsAcceptedConnection accepted_connection;
            int error = 0;

            // scoped to the auto-release CS object
            {
                ctl::ctAutoReleaseCriticalSection csLock(&pimpl->cs);
                // guard access to internal queues
                if (pimpl->accepted_connections.empty()) {
                    try {
                        // no accepted connections yet -- save the weak_ptr, *not* the shared_ptr
                        pimpl->pended_accept_requests.push(_weak_socket);
                    }
                    catch (const std::bad_alloc&) {
                        // fail the caller if can't save this request
                        error = WSAENOBUFS;
                    }
                } else {
                    // pull the next connection off the queue
                    accepted_connection = pimpl->accepted_connections.front();
                    error = accepted_connection.gle;
                    pimpl->accepted_connections.pop();
                }
            }

            //
            // complete this socket state if something failed, or if have a new socket to return
            //
            ctsConfig::PrintErrorIfFailed(L"AcceptEx", error);
            if (error != 0) {
                shared_socket->complete_state(error);
            } else {
                // if did not defer the accept request, return the socket
                if (accepted_connection.accept_socket != INVALID_SOCKET) {
                    // set the local addr
                    ctl::ctSockaddr local_addr;
                    int local_addr_len = local_addr.length();
                    if (0 == ::getsockname(accepted_connection.accept_socket, local_addr.sockaddr(), &local_addr_len)) {
                        shared_socket->set_local_address(local_addr);
                    }
                    shared_socket->set_socket(accepted_connection.accept_socket);
                    shared_socket->set_target_address(accepted_connection.remote_addr);
                    shared_socket->complete_state(0);

                    ctsConfig::PrintNewConnection(local_addr, accepted_connection.remote_addr);
                }
            }
        }


    private:
        static
        void ctsAcceptExIoCompletionCallback(
            OVERLAPPED* /*_overlapped*/,
            std::shared_ptr<ctsAcceptExImpl> _pimpl,
            ctsAcceptSocketInfo* _accept_info
            ) NOEXCEPT
        {
            ctsAcceptedConnection accepted_socket = _accept_info->GetAcceptedSocket();

            ctl::ctAutoReleaseCriticalSection auto_lock(&_pimpl->cs);
            if (_pimpl->pended_accept_requests.size() > 0) {
                //
                // we have unfulfilled requests for more connections
                // return a previously accepted socket
                //
                auto weak_socket = _pimpl->pended_accept_requests.front();
                _pimpl->pended_accept_requests.pop();

                auto shared_socket(weak_socket.lock());
                if (shared_socket) {
                    ctsConfig::PrintErrorIfFailed(L"AcceptEx", accepted_socket.gle);

                    if (0 == accepted_socket.gle) {
                        // set the local addr
                        ctl::ctSockaddr local_addr;
                        int local_addr_len = local_addr.length();
                        if (0 == ::getsockname(accepted_socket.accept_socket, local_addr.sockaddr(), &local_addr_len)) {
                            shared_socket->set_local_address(local_addr);
                        }
                        shared_socket->set_socket(accepted_socket.accept_socket);
                        shared_socket->set_target_address(accepted_socket.remote_addr);
                        shared_socket->complete_state(0);

                        ctsConfig::PrintNewConnection(local_addr, accepted_socket.remote_addr);
                    } else {
                        shared_socket->complete_state(accepted_socket.gle);
                    }
                } else {
                    // socket was closed from beneath us
                    ctsConfig::PrintErrorIfFailed(L"AcceptEx", WSAECONNABORTED);
                }
            } else {
                //
                // else, we have no requests for another connection,
                // - queue this one for when a request comes in
                //
                try {
                    _pimpl->accepted_connections.push(accepted_socket);
                }
                catch (const std::bad_alloc&) {
                    // if fails to be added to our queue, it's OK 
                    // - it will be destroyed and we'll make another later
                }
            }

            //
            // always attempt another AcceptEx
            //
            _accept_info->InitatiateAcceptEx(_pimpl);
        }
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///
    /// Definitions of ctsListenSocketInfo members
    ///
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    ctsAcceptEx::ctsListenSocketInfo::ctsListenSocketInfo(const ctl::ctSockaddr& _addr)
    : socket(INVALID_SOCKET), addr(_addr), iocp(), accept_sockets()
    {
        //
        // Create, Bind, Listen, create an IOCP thread pool
        //
        socket = ::WSASocket(addr.family(), SOCK_STREAM, IPPROTO_TCP, NULL, 0, ctsConfig::Settings->SocketFlags);
        if (INVALID_SOCKET == socket) {
            throw ctl::ctException(::WSAGetLastError(), L"socket", L"ctsAcceptEx", false);
        }
        // close the socket on failure
        ctlScopeGuard(closeListenSocketOnFailure, { ::closesocket(socket); socket = INVALID_SOCKET; });

        auto error = ctsConfig::SetPreBindOptions(socket, addr);
        if (error != 0) {
            throw ctl::ctException(error, L"ctsConfig::SetPreBindOptions", L"ctsAcceptEx", false);
        }

        if (SOCKET_ERROR == ::bind(socket, addr.sockaddr(), addr.length())) {
            throw ctl::ctException(::WSAGetLastError(), L"bind", L"ctsAcceptEx", false);
        }

        if (SOCKET_ERROR == ::listen(socket, ctsConfig::GetListenBacklog())) {
            throw ctl::ctException(::WSAGetLastError(), L"listen", L"ctsAcceptEx", false);
        }

        iocp = std::make_shared<ctl::ctThreadIocp>(socket, ctsConfig::Settings->PTPEnvironment);

        // everything succeeded, dismiss closing the socket
        closeListenSocketOnFailure.dismiss();
    }

    inline
    ctsAcceptEx::ctsListenSocketInfo::~ctsListenSocketInfo() NOEXCEPT
    {
        if (socket != INVALID_SOCKET) {
            ::closesocket(socket);
        }
    }

    inline
    void ctsAcceptEx::ctsListenSocketInfo::RestartStalledAccepts(std::shared_ptr<ctsAcceptEx::ctsAcceptExImpl> _pimpl)
    {
        for (auto& _socket : accept_sockets) {
            _socket->InitatiateAcceptEx(_pimpl);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///
    /// Definitions of ctsAcceptSocketInfo members
    ///
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    ctsAcceptEx::ctsAcceptSocketInfo::ctsAcceptSocketInfo(std::shared_ptr<ctsAcceptEx::ctsListenSocketInfo>& _listen_socket)
    : socket(INVALID_SOCKET),
      cs(),
      pov(NULL),
      listening_socket(_listen_socket->socket),
      listening_addr(_listen_socket->addr),
      listening_iocp(_listen_socket->iocp)
    {
        if (!::InitializeCriticalSectionAndSpinCount(&cs, 4000)) {
            throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionAndSpinCount", L"ctsAcceptEx", false);
        }
    }

    inline
    ctsAcceptEx::ctsAcceptSocketInfo::~ctsAcceptSocketInfo() NOEXCEPT
    {
        if (socket != INVALID_SOCKET) {
            ::closesocket(socket);
        }
        ::DeleteCriticalSection(&cs);
    }

    inline
    void ctsAcceptEx::ctsAcceptSocketInfo::InitatiateAcceptEx(std::shared_ptr<ctsAcceptEx::ctsAcceptExImpl> _pimpl)
    {
        ctl::ctAutoReleaseCriticalSection lock(&this->cs);

        if (this->socket != INVALID_SOCKET) {
            // no need to post another AcceptEx
            return;
        }

        SOCKET new_socket = ::WSASocket(this->listening_addr.family(), SOCK_STREAM, IPPROTO_TCP, NULL, 0, ctsConfig::Settings->SocketFlags);
        if (INVALID_SOCKET == new_socket) {
            throw ctl::ctException(::WSAGetLastError(), L"WSASocket", L"ctsAcceptEx", false);
        }
        ctlScopeGuard(closeSocketOnError, { ::closesocket(new_socket); });

        // since not inheriting from the listening socket, must explicity set options on the accept socket
        // - passing the listening address since that will be the local address of this accepted socket
        auto options = ctsConfig::SetPreBindOptions(new_socket, this->listening_addr);
        if (options != 0) {
            throw ctl::ctException(options, L"SetPreBindOptions", L"ctsAcceptEx", false);
        }
        options = ctsConfig::SetPreConnectOptions(new_socket);
        if (options != 0) {
            throw ctl::ctException(options, L"SetPreConnectOptions", L"ctsAcceptEx", false);
        }

        ::ZeroMemory(this->OutputBuffer, SingleOutputBufferSize * 2);
        DWORD bytes_received;

        this->pov = this->listening_iocp->new_request(
            ctsAcceptExIoCompletionCallback,
            _pimpl,
            this);
        if (!ctl::ctAcceptEx(
                this->listening_socket,
                new_socket,
                this->OutputBuffer,
                0, SingleOutputBufferSize, SingleOutputBufferSize,
                &bytes_received,
                this->pov)) {
            int error = ::WSAGetLastError();
            if (ERROR_IO_PENDING != error) {
                // a real failure - must abort the IO
                this->listening_iocp->cancel_request(this->pov);
                this->pov = nullptr;
                ::closesocket(new_socket);
                ctsConfig::PrintErrorIfFailed(L"AcceptEx", error);
                return;
            }

        } else if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP) {
            // AcceptEx completed inline - directly invoke the callback to handle the completion
            // - after canceling the TP request
            this->listening_iocp->cancel_request(this->pov);
            this->pov = nullptr;
            ctsAcceptExIoCompletionCallback(nullptr, _pimpl, this);
        }

        // no failures - store the socket
        closeSocketOnError.dismiss();
        this->socket = new_socket;
    }

    inline
    ctsAcceptEx::ctsAcceptedConnection ctsAcceptEx::ctsAcceptSocketInfo::GetAcceptedSocket() NOEXCEPT
    {
        ctl::ctAutoReleaseCriticalSection auto_lock(&this->cs);

        ctsAcceptEx::ctsAcceptedConnection return_details;
        // if the OVERLAPPED* is null, it means it completed inline (no OVERLAPPED async completion)
        // - thus we know it already succeeded
        if (this->pov) {
            DWORD transferred, flags;
            if (!::WSAGetOverlappedResult(
                this->listening_socket,
                this->pov,
                &transferred,
                FALSE,
                &flags)) {
                return_details.gle = ::WSAGetLastError();

                ctsConfig::PrintErrorIfFailed(L"AcceptEx", return_details.gle);
                if (this->socket != INVALID_SOCKET) {
                    ::closesocket(this->socket);
                    this->socket = INVALID_SOCKET;
                }
                // return empty/failed details object
                return return_details;
            }
        }

        // if successful, update the socket context
        // this should never fail - break if it does to debug it
        int err = ::setsockopt(
                socket,
                SOL_SOCKET,
                SO_UPDATE_ACCEPT_CONTEXT,
                reinterpret_cast<char *>(&this->listening_socket),
                sizeof(this->listening_socket));
        ctl::ctFatalCondition(
            (err != 0),
            L"setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed [%d], accept socket [%lld], listen socket [%lld]",
            ::WSAGetLastError(),
            static_cast<long long>(socket),
            static_cast<long long>(this->listening_socket));

        return_details = this->make_sockaddr_details();
        // about to return the socket to the user, nullify it here first
        this->socket = INVALID_SOCKET;
        return return_details;
    }

    inline
    ctsAcceptEx::ctsAcceptedConnection ctsAcceptEx::ctsAcceptSocketInfo::make_sockaddr_details() NOEXCEPT
    {
        SOCKADDR_INET* local_addr;
        int local_addr_len = static_cast<int>(sizeof SOCKADDR_INET);
        SOCKADDR_INET* remote_addr;
        int remote_addr_len = static_cast<int>(sizeof SOCKADDR_INET);

        ctl::ctGetAcceptExSockaddrs(
            OutputBuffer,
            0,
            SingleOutputBufferSize,
            SingleOutputBufferSize,
            reinterpret_cast<sockaddr**>(&local_addr),
            &local_addr_len,
            reinterpret_cast<sockaddr**>(&remote_addr),
            &remote_addr_len);

        ctsAcceptEx::ctsAcceptedConnection return_details;
        return_details.accept_socket = this->socket;
        return_details.gle = 0;
        return_details.local_addr.setSockaddr(local_addr);
        return_details.remote_addr.setSockaddr(remote_addr);

        return return_details;
    }
} // namespace
