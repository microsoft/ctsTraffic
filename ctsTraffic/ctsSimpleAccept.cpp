/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <vector>
#include <memory>
#include <exception>
// os headers
#include <windows.h>
#include <winsock2.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctSockaddr.hpp>
#include <ctException.hpp>
#include <ctLocks.hpp>
// project headers
#include "ctsSocket.h"
#include "ctsConfig.h"


namespace ctsTraffic {
    //
    // Functor class for implementing ctsSocketFunction
    //
    // Implements listing/accepting connections in the simplest form
    // - posting an accept() for each functor instance
    //
    // Each listener functor will be copy constructed
    // - so need to ensure that all members are easily copied
    // - also means the counter will need to be a ptr
    //
    // The refcount_sockets vector will optimize in balancing accept calls
    // - across all listeners
    //
    namespace details {

        class ctsSimpleAcceptImpl {
        private:
            PTP_WORK thread_pool_worker = nullptr;
            TP_CALLBACK_ENVIRON thread_pool_environment;
            // CS guards access to the accepting_sockets vector
            CRITICAL_SECTION accepting_cs;

            std::vector<LONG> listening_sockets_refcount;

            _Guarded_by_(accepting_cs)
                std::vector<SOCKET> listening_sockets;
            _Guarded_by_(accepting_cs)
                std::vector<std::weak_ptr<ctsSocket>> accepting_sockets;

        public:
            ctsSimpleAcceptImpl()
            {
                ::ZeroMemory(&thread_pool_environment, sizeof thread_pool_environment);
                ::ZeroMemory(&accepting_cs, sizeof accepting_cs);

                // need a CS to guard access to our vectors
                if (!::InitializeCriticalSectionEx(&accepting_cs, 4000, 0)) {
                    throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsSimpleAccept", false);
                }

                // will use the global threadpool, but will mark these work-items as running long
                ::InitializeThreadpoolEnvironment(&thread_pool_environment);
                ::SetThreadpoolCallbackRunsLong(&thread_pool_environment);

                // can *not* pass the this ptr to the threadpool, since this object can be copied
                thread_pool_worker = ::CreateThreadpoolWork(ThreadPoolWorker, this, &thread_pool_environment);
                if (nullptr == thread_pool_worker) {
                    throw ctl::ctException(::GetLastError(), L"CreateThreadpoolWork", L"ctsSimpleAccept", false);
                }

                // listen to each address
                for (const auto& addr : ctsConfig::Settings->ListenAddresses) {
#pragma warning(suppress: 28193) // PREFast isn't seeing that listening is indeed evaluated before being referenced
                    SOCKET listening = ctsConfig::CreateSocket(addr.family(), SOCK_STREAM, IPPROTO_TCP, ctsConfig::Settings->SocketFlags);
                    ctlScopeGuard(closeSocketOnError, { ::closesocket(listening); });

                    auto gle = ctsConfig::SetPreBindOptions(listening, addr);
                    if (gle != NO_ERROR) {
                        throw ctl::ctException(gle, L"SetPreBindOptions", L"ctsSimpleAccept", false);
                    }

                    gle = ctsConfig::SetPreConnectOptions(listening);
                    if (gle != NO_ERROR) {
                        throw ctl::ctException(gle, L"SetPreConnectOptions", L"ctsSimpleAccept", false);
                    }

                    if (SOCKET_ERROR == ::bind(listening, addr.sockaddr(), addr.length())) {
                        throw ctl::ctException(::WSAGetLastError(), L"bind", L"ctsSimpleAccept", false);
                    }

                    if (SOCKET_ERROR == ::listen(listening, ctsConfig::GetListenBacklog())) {
                        throw ctl::ctException(::WSAGetLastError(), L"listen", L"ctsSimpleAccept", false);
                    }

                    listening_sockets.push_back(listening);
                    // socket is now being tracked in listening_sockets, dismiss the scope guard
                    closeSocketOnError.dismiss();

                    PrintDebugInfo(
                        L"\t\tListening to %s\n", addr.writeCompleteAddress().c_str());
                }

                if (listening_sockets.empty()) {
                    throw std::exception("ctsSimpleAccept invoked with no listening addresses specified");
                }
                listening_sockets_refcount.resize(listening_sockets.size(), 0L);
            }
            ~ctsSimpleAcceptImpl()
            {
                ::EnterCriticalSection(&accepting_cs);
                /// close all listening sockets to release any pended accept's
                for (auto& listening_socket : listening_sockets) {
                    if (listening_socket != INVALID_SOCKET) {
                        ::closesocket(listening_socket);
                        listening_socket = INVALID_SOCKET;
                    }
                }
                ::LeaveCriticalSection(&accepting_cs);

                if (thread_pool_worker != nullptr) {
                    // ctsSimpleAccept object was initialized
                    ::WaitForThreadpoolWorkCallbacks(thread_pool_worker, TRUE);
                    ::CloseThreadpoolWork(thread_pool_worker);
                }

                ::DeleteCriticalSection(&accepting_cs);
            }

            //
            // Needs to not block ctsSocketState - will just schedule work on its own TP
            //
            void accept_socket(const std::weak_ptr<ctsSocket>& _weak_socket)
            {
                ctl::ctAutoReleaseCriticalSection lock(&accepting_cs);
                accepting_sockets.push_back(_weak_socket);
                ::SubmitThreadpoolWork(thread_pool_worker);
            }

            // non-copyable
            ctsSimpleAcceptImpl(const ctsSimpleAcceptImpl&) = delete;
            ctsSimpleAcceptImpl& operator=(const ctsSimpleAcceptImpl&) = delete;

        private:
            static VOID NTAPI ThreadPoolWorker(PTP_CALLBACK_INSTANCE, PVOID _context, PTP_WORK) NOEXCEPT
            {
                ctsSimpleAcceptImpl* pimpl = reinterpret_cast<ctsSimpleAcceptImpl*>(_context);

                // get an accept-socket off the vector (protected with its cs)
                ::EnterCriticalSection(&pimpl->accepting_cs);
                ctlScopeGuard(leaveCriticalSectionOnExit, { ::LeaveCriticalSection(&pimpl->accepting_cs); });

                std::weak_ptr<ctsSocket> weak_socket(*pimpl->accepting_sockets.rbegin());
                pimpl->accepting_sockets.pop_back();

                auto accept_socket(weak_socket.lock());
                if (!accept_socket) {
                    return;
                }

                // based off of the refcount, choose a socket that's least used
                // - not taking a lock: it doesn't have to be that precise
                LONG lowest_refcount = pimpl->listening_sockets_refcount[0];
                unsigned listener_counter = 0;
                unsigned listener_position = 0;
                for (const auto& refcount : pimpl->listening_sockets_refcount) {
                    if (refcount < lowest_refcount) {
                        lowest_refcount = refcount;
                        listener_position = listener_counter;
                    }
                    ++listener_counter;
                }

                SOCKET listener = pimpl->listening_sockets[listener_position];
                if (INVALID_SOCKET == listener) {
                    return;
                }

                // now leave the CS before making the blocking call to accept()
                leaveCriticalSectionOnExit.run_once();

                // increment the listening socket before calling accept on the blocking socket
                ::InterlockedIncrement(&pimpl->listening_sockets_refcount[listener_position]);
                ctl::ctSockaddr remote_addr;
                int remote_addr_len = remote_addr.length();
                SOCKET new_socket = ::accept(listener, remote_addr.sockaddr(), &remote_addr_len);
                DWORD gle = ::WSAGetLastError();
                ::InterlockedDecrement(&pimpl->listening_sockets_refcount[listener_position]);

                // if failed complete the ctsSocket and return 
                if (new_socket == INVALID_SOCKET) {
                    ctsConfig::PrintErrorIfFailed(L"accept", gle);
                    accept_socket->complete_state(gle);
                    return;
                }

                // successfully accepted a connection
                accept_socket->set_socket(new_socket);
                accept_socket->set_target_address(remote_addr);

                ctl::ctSockaddr local_addr;
                int local_addr_len = local_addr.length();
                if (0 == ::getsockname(new_socket, local_addr.sockaddr(), &local_addr_len)) {
                    accept_socket->set_local_address(local_addr);
                }
                else if (0 == ::getsockname(listener, local_addr.sockaddr(), &local_addr_len)) {
                    accept_socket->set_local_address(local_addr);
                }

                gle = ctsConfig::SetPreBindOptions(new_socket, local_addr);
                if (gle != NO_ERROR) {
                    ctsConfig::PrintErrorIfFailed(L"SetPreBindOptions", gle);
                    accept_socket->complete_state(gle);
                    return;
                }

                gle = ctsConfig::SetPreConnectOptions(new_socket);
                if (gle != NO_ERROR) {
                    ctsConfig::PrintErrorIfFailed(L"SetPreConnectOptions", gle);
                    accept_socket->complete_state(gle);
                    return;
                }

                accept_socket->complete_state(0);
                ctsConfig::PrintNewConnection(local_addr, remote_addr);
            }
        };

        std::shared_ptr<ctsSimpleAcceptImpl> s_pimpl;
        static INIT_ONCE s_ctsSimpleAcceptImplInitOnce = INIT_ONCE_STATIC_INIT;
        static BOOL CALLBACK s_ctsSimpleAcceptImplInitFn(_In_ PINIT_ONCE, _In_ PVOID perror, _In_ PVOID*)
        {
            try {
                s_pimpl = std::make_shared<ctsSimpleAcceptImpl>();
            }
            catch (const ctl::ctException& e) {
                ctsConfig::PrintException(e);
                *reinterpret_cast<DWORD*>(perror) = e.why();
                return FALSE;
            }
            catch (const std::exception e) {
                ctsConfig::PrintException(e);
                *reinterpret_cast<DWORD*>(perror) = ERROR_OUTOFMEMORY;
                return FALSE;
            }

            return TRUE;
        }

    }

    void ctsSimpleAccept(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT
    {
        DWORD error = 0;
        if (!::InitOnceExecuteOnce(&details::s_ctsSimpleAcceptImplInitOnce, details::s_ctsSimpleAcceptImplInitFn, &error, nullptr)) {
            auto shared_socket(_weak_socket.lock());
            if (shared_socket) {
                shared_socket->complete_state(error);
            }

        } else {
            try { details::s_pimpl->accept_socket(_weak_socket); }
            catch (const std::exception&) {
                auto shared_socket(_weak_socket.lock());
                if (shared_socket) {
                    shared_socket->complete_state(ERROR_OUTOFMEMORY);
                }
            }
        }
    }
} // namespace
