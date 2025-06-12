/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <vector>
#include <memory>
#include <string>
#include <exception>
// using wil::network to pull in all necessary networking headers
#include <wil/network.h>
// project headers
#include "ctsSocket.h"
#include "ctsConfig.h"

namespace ctsTraffic
{
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
    // The ref-count_sockets vector will optimize in balancing accept calls
    // - across all listeners
    //
    namespace details
    {
        class ctsSimpleAcceptImpl
        {
        private:
            PTP_WORK m_threadPoolWorker = nullptr;
            TP_CALLBACK_ENVIRON m_threadPoolEnvironment{};
            // CS guards access to the accepting_sockets vector
            wil::critical_section m_acceptingCs{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};

            std::vector<LONG> m_listeningSocketsRefCount{};

            _Guarded_by_(m_acceptingCs) std::vector<SOCKET> m_listeningSockets{};
            _Guarded_by_(m_acceptingCs) std::vector<std::weak_ptr<ctsSocket>> m_acceptingSockets{};

        public:
            ctsSimpleAcceptImpl()
            {
                // will use the global threadpool, but will mark these work-items as running long
                ::ZeroMemory(&m_threadPoolEnvironment, sizeof m_threadPoolEnvironment);
                InitializeThreadpoolEnvironment(&m_threadPoolEnvironment);
                SetThreadpoolCallbackRunsLong(&m_threadPoolEnvironment);

                // can *not* pass the 'this' ptr to the threadpool, since this object can be copied
                m_threadPoolWorker = CreateThreadpoolWork(ThreadPoolWorker, this, &m_threadPoolEnvironment);
                if (nullptr == m_threadPoolWorker)
                {
                    THROW_WIN32_MSG(GetLastError(), "CreateThreadpoolWork (ctsSimpleAccept)");
                }

                // listen to each address
                for (const auto& addr : ctsConfig::g_configSettings->ListenAddresses)
                {
                    wil::unique_socket listening{
                        ctsConfig::CreateSocket(addr.family(),
                                                SOCK_STREAM,
                                                IPPROTO_TCP,
                                                ctsConfig::g_configSettings->SocketFlags)
                    };

                    auto error = ctsConfig::SetPreBindOptions(listening.get(), addr);
                    if (error != NO_ERROR)
                    {
                        THROW_WIN32_MSG(error, "SetPreBindOptions (ctsSimpleAccept)");
                    }

                    error = ctsConfig::SetPreConnectOptions(listening.get());
                    if (error != NO_ERROR)
                    {
                        THROW_WIN32_MSG(error, "SetPreConnectOptions (ctsSimpleAccept)");
                    }

                    if (SOCKET_ERROR == bind(listening.get(), addr.sockaddr(), addr.size()))
                    {
                        THROW_WIN32_MSG(WSAGetLastError(), "bind (ctsSimpleAccept)");
                    }

                    if (SOCKET_ERROR == listen(listening.get(), ctsConfig::GetListenBacklog()))
                    {
                        THROW_WIN32_MSG(WSAGetLastError(), "listen (ctsSimpleAccept)");
                    }

                    m_listeningSockets.push_back(listening.get());
                    // socket is now being tracked in listening_sockets, release ownership
                    listening.release();

                    PRINT_DEBUG_INFO(L"\t\tListening to %ws\n", addr.format_complete_address().c_str());
                }

                if (m_listeningSockets.empty())
                {
                    throw std::exception("ctsSimpleAccept invoked with no listening addresses specified");
                }
                m_listeningSocketsRefCount.resize(m_listeningSockets.size(), 0L);
            }

            ~ctsSimpleAcceptImpl()
            {
                auto lock = m_acceptingCs.lock();
                /// close all listening sockets to release any pended accept's
                for (auto& listeningSocket : m_listeningSockets)
                {
                    if (listeningSocket != INVALID_SOCKET)
                    {
                        closesocket(listeningSocket);
                        listeningSocket = INVALID_SOCKET;
                    }
                }
                lock.reset();

                if (m_threadPoolWorker != nullptr)
                {
                    // ctsSimpleAccept object was initialized
                    WaitForThreadpoolWorkCallbacks(m_threadPoolWorker, TRUE);
                    CloseThreadpoolWork(m_threadPoolWorker);
                }
            }

            //
            // Needs to not block ctsSocketState - will just schedule work on its own TP
            //
            void AcceptSocket(const std::weak_ptr<ctsSocket>& weakSocket)
            {
                const auto lock = m_acceptingCs.lock();
                m_acceptingSockets.push_back(weakSocket);
                SubmitThreadpoolWork(m_threadPoolWorker);
            }

            // non-copyable
            ctsSimpleAcceptImpl(const ctsSimpleAcceptImpl&) = delete;
            ctsSimpleAcceptImpl& operator=(const ctsSimpleAcceptImpl&) = delete;
            ctsSimpleAcceptImpl(ctsSimpleAcceptImpl&&) = delete;
            ctsSimpleAcceptImpl& operator=(ctsSimpleAcceptImpl&&) = delete;

        private:
            static VOID NTAPI ThreadPoolWorker(PTP_CALLBACK_INSTANCE, PVOID pContext, PTP_WORK) noexcept
            {
                auto* pimpl = static_cast<ctsSimpleAcceptImpl*>(pContext);

                // get an accept-socket off the vector (protected with its cs)
                auto lock = pimpl->m_acceptingCs.lock();

                const std::weak_ptr weakSocket(*pimpl->m_acceptingSockets.rbegin());
                pimpl->m_acceptingSockets.pop_back();

                const auto acceptSocket(weakSocket.lock());
                if (!acceptSocket)
                {
                    return;
                }

                // based off of the ref-count, choose a socket that's least used
                // - not taking a lock: it doesn't have to be that precise
                auto lowestRefCount = pimpl->m_listeningSocketsRefCount[0];
                uint32_t listenerCounter = 0;
                uint32_t listenerPosition = 0;
                for (const auto& refCount : pimpl->m_listeningSocketsRefCount)
                {
                    if (refCount < lowestRefCount)
                    {
                        lowestRefCount = refCount;
                        listenerPosition = listenerCounter;
                    }
                    ++listenerCounter;
                }

                const SOCKET listener = pimpl->m_listeningSockets[listenerPosition];
                if (INVALID_SOCKET == listener)
                {
                    return;
                }

                // now leave the CS before making the blocking call to accept()
                lock.reset();

                // increment the listening socket before calling accept on the blocking socket
                ::InterlockedIncrement(&pimpl->m_listeningSocketsRefCount[listenerPosition]);
                socket_address remoteAddr;
                auto remoteAddrLen = remoteAddr.size();
                const SOCKET newSocket = accept(listener, remoteAddr.sockaddr(), &remoteAddrLen);
                auto gle = WSAGetLastError();
                ::InterlockedDecrement(&pimpl->m_listeningSocketsRefCount[listenerPosition]);

                // if failed complete the ctsSocket and return
                if (newSocket == INVALID_SOCKET)
                {
                    ctsConfig::PrintErrorIfFailed("accept", gle);
                    acceptSocket->CompleteState(gle);
                    return;
                }

                // successfully accepted a connection
                acceptSocket->SetSocket(newSocket);
                acceptSocket->SetRemoteSockaddr(remoteAddr);

                socket_address localAddr;
                auto localAddrLen = localAddr.size();
                if (0 == getsockname(newSocket, localAddr.sockaddr(), &localAddrLen))
                {
                    acceptSocket->SetLocalSockaddr(localAddr);
                }
                else if (0 == getsockname(listener, localAddr.sockaddr(), &localAddrLen))
                {
                    acceptSocket->SetLocalSockaddr(localAddr);
                }

                gle = ctsConfig::SetPreBindOptions(newSocket, localAddr);
                if (gle != NO_ERROR)
                {
                    ctsConfig::PrintErrorIfFailed("SetPreBindOptions", gle);
                    acceptSocket->CompleteState(gle);
                    return;
                }

                gle = ctsConfig::SetPreConnectOptions(newSocket);
                if (gle != NO_ERROR)
                {
                    ctsConfig::PrintErrorIfFailed("SetPreConnectOptions", gle);
                    acceptSocket->CompleteState(gle);
                    return;
                }

                acceptSocket->CompleteState(0);
                ctsConfig::PrintNewConnection(localAddr, remoteAddr);
            }
        };

        static std::shared_ptr<ctsSimpleAcceptImpl> g_pimpl; // NOLINT(clang-diagnostic-exit-time-destructors)
        // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
        static INIT_ONCE g_ctsSimpleAcceptImplInitOnce = INIT_ONCE_STATIC_INIT;

        static BOOL CALLBACK ctsSimpleAcceptImplInitFn(PINIT_ONCE, PVOID pError, PVOID*) noexcept try
        {
            g_pimpl = std::make_shared<ctsSimpleAcceptImpl>();
            return TRUE;
        }
        catch (...)
        {
            *static_cast<DWORD*>(pError) = ctsConfig::PrintThrownException();
            return FALSE;
        }
    }

    void ctsSimpleAccept(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
    {
        DWORD error = 0;
        if (!InitOnceExecuteOnce(
            &details::g_ctsSimpleAcceptImplInitOnce,
            details::ctsSimpleAcceptImplInitFn,
            &error,
            nullptr))
        {
            if (const auto sharedSocket = weakSocket.lock())
            {
                sharedSocket->CompleteState(error);
            }
            return;
        }

        try
        {
            details::g_pimpl->AcceptSocket(weakSocket);
        }
        catch (...)
        {
            if (const auto sharedSocket = weakSocket.lock())
            {
                sharedSocket->CompleteState(ERROR_OUTOFMEMORY);
            }
        }
    }
} // namespace
