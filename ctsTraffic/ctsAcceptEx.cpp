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
#include <winsock2.h>
#include <mswsock.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include <ctSocketExtensions.hpp>
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
// project headers
#include "ctsSocket.h"

namespace ctsTraffic {
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

    namespace details {
        //
        // constant defining how many acceptex requests we want maintained per listener
        //
        static const unsigned PendedAcceptRequests = 100;

        //
        // necessary forward declarations of internal classes
        //
        struct ctsAcceptExImpl;
        class ctsAcceptSocketInfo;

        static void ctsAcceptExIoCompletionCallback(OVERLAPPED*, _In_ ctsAcceptSocketInfo* _accept_info) noexcept;

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// struct to capture relevant details of an accepted connection
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        struct ctsAcceptedConnection {
            wil::unique_socket accept_socket;
            ctl::ctSockaddr local_addr;
            ctl::ctSockaddr remote_addr;
            DWORD  gle = 0;
        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Struct to track listening sockets
        /// - must have a unique IOCP class for each listener
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        struct ctsListenSocketInfo {
            // c'tor throws a ctException or bad_alloc on failure
            explicit ctsListenSocketInfo(ctl::ctSockaddr _addr);
            ~ctsListenSocketInfo() = default;

            ctsListenSocketInfo(const ctsListenSocketInfo&) = delete;
            ctsListenSocketInfo& operator=(const ctsListenSocketInfo&) = delete;
            ctsListenSocketInfo(ctsListenSocketInfo&&) = delete;
            ctsListenSocketInfo& operator=(ctsListenSocketInfo&&) = delete;

            wil::unique_socket socket;
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
            explicit ctsAcceptSocketInfo(std::shared_ptr<ctsListenSocketInfo> _listen_socket);
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
            static const size_t SingleOutputBufferSize = sizeof(SOCKADDR_INET) + 16;

            // the lock to guard access to the SOCKET
            wil::critical_section cs;
            wil::unique_socket socket;
            // the OVERLAPPED* for the AcceptEx request
            OVERLAPPED* pov = nullptr;
            // the listening socket handle - needed for AcceptEx
            const std::shared_ptr<ctsListenSocketInfo> listening_socket_info;
            // the buffer to supply to AcceptEx to capture the address information
            char OutputBuffer[SingleOutputBufferSize * 2]{};
        };

        //
        // Impl object to carry around the real member data of ctsAcceptEx
        // - the shared_ptr to the Impl allows an instance of ctsAcceptEx to be copyable
        //
        struct ctsAcceptExImpl : public std::enable_shared_from_this<ctsAcceptExImpl> {
            // must guard access to internal containers
            wil::critical_section cs;
            std::vector<std::shared_ptr<ctsListenSocketInfo>> listeners;
            std::queue<std::weak_ptr<ctsSocket>> pended_accept_requests;
            std::queue<ctsAcceptedConnection> accepted_connections;

            //
            // ctsAcceptExImpl constructor
            // - start listening on all addresses specified tracked in ctsListenSocketInfo objects
            // - create ctsAcceptSocketInfo object to manage attempts to accept new connections
            // --- one object per accept socket
            //
            ctsAcceptExImpl()
            {
                // swap in the listen vector only if fully created
                // - if anything fails, this temp vector will go out of scope and safely be destroyed
                std::vector<std::shared_ptr<ctsListenSocketInfo>> temp_listeners;

                // listen to each address
                for (const auto& addr : ctsConfig::Settings->ListenAddresses) {
                    // Make the structures for the listener and its accept sockets
                    std::shared_ptr<ctsListenSocketInfo> listen_socket_info = std::make_shared<ctsListenSocketInfo>(addr);
                    PrintDebugInfo(L"\t\tListening to %ws\n", addr.WriteCompleteAddress().c_str());
                    //
                    // Add PendedAcceptRequests pended acceptex objects per listener
                    //
                    for (unsigned accept_counter = 0; accept_counter < PendedAcceptRequests; ++accept_counter) {
                        std::shared_ptr<ctsAcceptSocketInfo> accept_socket_info = std::make_shared<ctsAcceptSocketInfo>(listen_socket_info);
                        listen_socket_info->accept_sockets.push_back(accept_socket_info);
                        // post AcceptEx on this socket
                        accept_socket_info->InitatiateAcceptEx();
                    }

                    // all successful - save this listen socket
                    temp_listeners.push_back(listen_socket_info);
                }

                if (temp_listeners.empty()) {
                    throw std::exception("ctsAcceptEx invoked with no listening addresses specified");
                }

                // everything succeeded - safely save the listen queue
                listeners.swap(temp_listeners);
            }

            ~ctsAcceptExImpl() noexcept
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
        /// Definitions of ctsListenSocketInfo members
        ///
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ctsListenSocketInfo::ctsListenSocketInfo(ctl::ctSockaddr _addr) : addr(std::move(_addr))
        {
            wil::unique_socket tempsocket (
                ctsConfig::CreateSocket(addr.family(), SOCK_STREAM, IPPROTO_TCP, ctsConfig::Settings->SocketFlags));

            const auto error = ctsConfig::SetPreBindOptions(tempsocket.get(), addr);
            if (error != 0) {
                throw ctl::ctException(error, L"ctsConfig::SetPreBindOptions", L"ctsAcceptEx", false);
            }

            if (SOCKET_ERROR == ::bind(tempsocket.get(), addr.sockaddr(), addr.length())) {
                throw ctl::ctException(::WSAGetLastError(), L"bind", L"ctsAcceptEx", false);
            }

            if (SOCKET_ERROR == ::listen(tempsocket.get(), ctsConfig::GetListenBacklog())) {
                throw ctl::ctException(::WSAGetLastError(), L"listen", L"ctsAcceptEx", false);
            }

            iocp = std::make_shared<ctl::ctThreadIocp>(tempsocket.get(), ctsConfig::Settings->PTPEnvironment);

            // now save the socket after everything succeeded
            socket = std::move(tempsocket);
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        ///
        /// Definitions of ctsAcceptSocketInfo members
        ///
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ctsAcceptSocketInfo::ctsAcceptSocketInfo(std::shared_ptr<ctsListenSocketInfo> _listen_socket)
            : listening_socket_info(std::move(_listen_socket))
        {
        }

        void ctsAcceptSocketInfo::InitatiateAcceptEx()
        {
            const auto lock = cs.lock();

            if (this->socket.get() != INVALID_SOCKET) {
                // no need to post another AcceptEx
                return;
            }

            wil::unique_socket new_socket (
                ctsConfig::CreateSocket(
                    this->listening_socket_info->addr.family(),
                    SOCK_STREAM,
                    IPPROTO_TCP,
                    ctsConfig::Settings->SocketFlags));

            // since not inheriting from the listening socket, must explicity set options on the accept socket
            // - passing the listening address since that will be the local address of this accepted socket
            auto error = ctsConfig::SetPreBindOptions(new_socket.get(), this->listening_socket_info->addr);
            if (error != 0) {
                throw ctl::ctException(error, L"SetPreBindOptions", L"ctsAcceptEx", false);
            }
            error = ctsConfig::SetPreConnectOptions(new_socket.get());
            if (error != 0) {
                throw ctl::ctException(error, L"SetPreConnectOptions", L"ctsAcceptEx", false);
            }

            this->pov = this->listening_socket_info->iocp->new_request(
                [this](OVERLAPPED* _ov) noexcept
            { ctsAcceptExIoCompletionCallback(_ov, this); });

            ::ZeroMemory(this->OutputBuffer, SingleOutputBufferSize * 2);
            DWORD bytes_received;
            if (!ctl::ctAcceptEx(
                this->listening_socket_info->socket.get(),
                new_socket.get(),
                this->OutputBuffer,
                0, SingleOutputBufferSize, SingleOutputBufferSize,
                &bytes_received,
                this->pov))
            {
                error = ::WSAGetLastError();
                if (ERROR_IO_PENDING != error) {
                    // a real failure - must abort the IO
                    this->listening_socket_info->iocp->cancel_request(this->pov);
                    this->pov = nullptr;
                    ctsConfig::PrintErrorIfFailed(L"AcceptEx", error);
                    return;
                }

            } else if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP) {
                // AcceptEx completed inline - directly invoke the callback to handle the completion
                // - after canceling the TP request
                listening_socket_info->iocp->cancel_request(this->pov);
                this->pov = nullptr;
                ctsAcceptExIoCompletionCallback(nullptr, this);
            }

            // no failures - store the socket
            this->socket = std::move(new_socket);
        }

        ctsAcceptedConnection ctsAcceptSocketInfo::GetAcceptedSocket() noexcept
        {
            const auto lock = cs.lock();

            SOCKET listening_socket = listening_socket_info->socket.get();
            ctsAcceptedConnection return_details;

            // if the OVERLAPPED* is null, it means it completed inline (no OVERLAPPED async completion)
            // - thus we know it already succeeded
            if (this->pov) {
                DWORD transferred, flags;
                if (!::WSAGetOverlappedResult(
                    listening_socket,
                    this->pov,
                    &transferred,
                    FALSE,
                    &flags))
                {
                    return_details.gle = ::WSAGetLastError();

                    ctsConfig::PrintErrorIfFailed(L"AcceptEx", return_details.gle);
                    socket.reset();

                    // return empty/failed details object
                    return return_details;
                }
            }

            // if successful, update the socket context
            // this should never fail - break if it does to debug it
            const auto err = ::setsockopt(
                socket.get(),
                SOL_SOCKET,
                SO_UPDATE_ACCEPT_CONTEXT,
                reinterpret_cast<char *>(&listening_socket),
                sizeof(listening_socket));
            ctl::ctFatalCondition(
                (err != 0),
                L"setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed [%d], accept socket [%lld], listen socket [%lld]",
                ::WSAGetLastError(),
                static_cast<long long>(socket.get()),
                static_cast<long long>(listening_socket));

            SOCKADDR_INET* local_addr;
            auto local_addr_len = static_cast<int>(sizeof SOCKADDR_INET);
            SOCKADDR_INET* remote_addr;
            auto remote_addr_len = static_cast<int>(sizeof SOCKADDR_INET);

            ctl::ctGetAcceptExSockaddrs(
                OutputBuffer,
                0,
                SingleOutputBufferSize,
                SingleOutputBufferSize,
                reinterpret_cast<sockaddr**>(&local_addr),
                &local_addr_len,
                reinterpret_cast<sockaddr**>(&remote_addr),
                &remote_addr_len);

            // transfer ownership of the SOCKET to the caller
            return_details.accept_socket = std::move(socket);
            return_details.gle = 0;
            return_details.local_addr.set(local_addr);
            return_details.remote_addr.set(remote_addr);

            return return_details;
        }

        std::shared_ptr<ctsAcceptExImpl> s_pimpl;
        // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
        static INIT_ONCE s_ctsAcceptExImplInitOnce = INIT_ONCE_STATIC_INIT;
        static BOOL CALLBACK s_ctsAcceptExImplInitFn(PINIT_ONCE, PVOID perror, PVOID*)
        {
            try { s_pimpl = std::make_shared<ctsAcceptExImpl>(); }
            catch (const std::exception& e) {
                ctsConfig::PrintException(e);
                *static_cast<DWORD*>(perror) = ctl::ctErrorCode(e);
                return FALSE;
            }

            return TRUE;
        }

        static void ctsAcceptExIoCompletionCallback(_In_opt_ OVERLAPPED*, _In_ ctsAcceptSocketInfo* _accept_info) noexcept
        {
            ctsAcceptedConnection accepted_socket = _accept_info->GetAcceptedSocket();

            const auto lock = s_pimpl->cs.lock();

            if (!s_pimpl->pended_accept_requests.empty()) {
                //
                // we have unfulfilled requests for more connections
                // return a previously accepted socket
                //
                const auto weak_socket = s_pimpl->pended_accept_requests.front();
                s_pimpl->pended_accept_requests.pop();

                auto shared_socket(weak_socket.lock());
                if (shared_socket) {
                    ctsConfig::PrintErrorIfFailed(L"AcceptEx", accepted_socket.gle);

                    if (0 == accepted_socket.gle) {
                        // set the local addr
                        const ctl::ctSockaddr local_addr;
                        int local_addr_len = local_addr.length();
                        if (0 == ::getsockname(accepted_socket.accept_socket.get(), local_addr.sockaddr(), &local_addr_len)) {
                            shared_socket->set_local_address(local_addr);
                        }

                        // socket ownership was successfully transfered
                        shared_socket->set_socket(accepted_socket.accept_socket.release());
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
                try { s_pimpl->accepted_connections.push(std::move(accepted_socket)); }
                catch (const std::exception&) {
                    // if fails to be added to our queue, it's OK
                    // - it will be destroyed and we'll make another later
                }
            }

            //
            // always attempt another AcceptEx
            //
            _accept_info->InitatiateAcceptEx();
        }

    } // namespace details

    //
    //
    // An accepted socket is being requested
    // - if have one queued, return that
    // - else store the weak_ptr<ctsSocket> to be fulfilled later
    //
    //
    void ctsAcceptEx(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
    {
        DWORD error = 0;
        if (!::InitOnceExecuteOnce(&details::s_ctsAcceptExImplInitOnce, details::s_ctsAcceptExImplInitFn, &error, nullptr)) {
            auto shared_socket(_weak_socket.lock());
            if (shared_socket) {
                shared_socket->complete_state(error);
            }
            return;
        }

        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        details::ctsAcceptedConnection accepted_connection;

        // scoped to the auto-release CS object
        {
            const auto lock = details::s_pimpl->cs.lock();
            // guard access to internal queues
            if (details::s_pimpl->accepted_connections.empty()) {
                // no accepted connections yet -- save the weak_ptr, *not* the shared_ptr
                try { details::s_pimpl->pended_accept_requests.push(_weak_socket); }
                catch (const std::exception&) {
                    // fail the caller if can't save this request
                    error = WSAENOBUFS;
                }

            } else {
                // pull the next connection off the queue
                accepted_connection = std::move(details::s_pimpl->accepted_connections.front());
                details::s_pimpl->accepted_connections.pop();
                error = accepted_connection.gle;
            }
        }

        //
        // complete this socket state if something failed
        //
        ctsConfig::PrintErrorIfFailed(L"AcceptEx", error);
        if (error != 0) {
            shared_socket->complete_state(error);
            return;
        }

        //
        // if did not defer the accept request and we have a new accepted socket,
        // complete this socket state
        //
        if (accepted_connection.accept_socket.get() != INVALID_SOCKET) {
            // set the local addr
            const ctl::ctSockaddr local_addr;
            int local_addr_len = local_addr.length();
            if (0 == ::getsockname(accepted_connection.accept_socket.get(), local_addr.sockaddr(), &local_addr_len)) {
                shared_socket->set_local_address(local_addr);
            }

            // transfering ownership to the ctsSocket
            shared_socket->set_socket(accepted_connection.accept_socket.release());
            shared_socket->set_target_address(accepted_connection.remote_addr);
            shared_socket->complete_state(0);

            ctsConfig::PrintNewConnection(local_addr, accepted_connection.remote_addr);
        }
    }

} // namespace
