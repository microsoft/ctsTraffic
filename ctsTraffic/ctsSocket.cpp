/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// parent header
#include "ctsSocket.h"
// OS headers
#include <Windows.h>
// project headers
#include "ctsConfig.h"
#include "ctsSocketState.h"
#include "ctsWinsockLayer.h"
// wil headers always included last
#include <wil/win32_helpers.h>

namespace ctsTraffic
{
    using namespace ctl;
    using namespace std;

    // default values are assigned in the class declaration
    ctsSocket::ctsSocket(weak_ptr<ctsSocketState> parent) noexcept :
        m_parent(std::move(parent))
    {
    }

    _No_competing_thread_ ctsSocket::~ctsSocket() noexcept
    {
        // shutdown() tears down the socket object
        Shutdown();

        /*
        // wait for all IO to be completed before deleting the pattern if this is RIO
        // as the IO requests are still using the RIO Buffers we gave it
        if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
        {
            while (GetPendedIoCount() != 0)
            {
                YieldProcessor();
            }
        }
        */

        // if the IO pattern is still alive, must delete it once in the destructor before this object goes away
        // - can't reset this in ctsSocket::shutdown since ctsSocket::shutdown can be called from the parent ctsSocketState 
        //   and there may be callbacks still running holding onto a reference to this ctsSocket object
        //   which causes the potential to AV in the io_pattern
        //   (a race-condition touching the io_pattern with deleting the io_pattern)
        m_pattern.reset();
    }

    [[nodiscard]] ctsSocket::SocketReference ctsSocket::AcquireSocketLock() const noexcept
    {
        auto lock = m_lock.lock();
        const SOCKET lockedSocketValue = m_socket.get();
        return {std::move(lock), lockedSocketValue, m_pattern};
    }

    void ctsSocket::SetSocket(SOCKET socket) noexcept
    {
        const auto lock = m_lock.lock();

        FAIL_FAST_IF_MSG(
            !!m_socket,
            "ctsSocket::set_socket trying to set a SOCKET (%Iu) when it has already been set in this object (%Iu)",
            socket, m_socket.get());

        m_socket.reset(socket);
    }

    int ctsSocket::CloseSocket(uint32_t errorCode) noexcept
    {
        const auto lock = m_lock.lock();

        auto error = 0l;
        if (m_socket)
        {
            if (errorCode != 0)
            {
                // always try to RST if we are closing due to an error
                // to best-effort notify the opposite endpoint
                const wsIOResult result = ctsSetLingerToResetSocket(m_socket.get());
                error = static_cast<int>(result.m_errorCode);
            }

            if (m_pattern)
            {
                // if the user asked for TCP details, capture them before we close the socket
                m_pattern->PrintTcpInfo(m_localSockaddr, m_targetSockaddr, m_socket.get());
            }

            m_socket.reset();
        }
        return error;
    }

    const shared_ptr<ctThreadIocp>& ctsSocket::GetIocpThreadpool()
    {
        // use the SOCKET cs to also guard creation of this TP object
        const auto lock = m_lock.lock();
        // must verify a valid socket first to avoid racing destroying the iocp shared_ptr as we try to create it here
        if (m_socket && !m_tpIocp)
        {
            // can throw
            m_tpIocp = make_shared<ctThreadIocp>(m_socket.get(), ctsConfig::g_configSettings->pTpEnvironment);
        }
        return m_tpIocp;
    }

    void ctsSocket::PrintPatternResults(uint32_t lastError) const noexcept
    {
        if (m_pattern)
        {
            const auto lock = m_lock.lock();
            m_pattern->PrintStatistics(
                GetLocalSockaddr(),
                GetRemoteSockaddr());
        }
        else
        {
            // failed during socket creation, bind, or connect
            ctsConfig::PrintConnectionResults(lastError);
        }
    }

    void ctsSocket::CompleteState(DWORD errorCode) const noexcept
    {
        DWORD recordedError = errorCode;
        {
            const auto lock = m_lock.lock();
            FAIL_FAST_IF_MSG(
                m_ioCount != 0,
                "ctsSocket::complete_state is called with outstanding IO (%ld)", m_ioCount);

            if (m_pattern)
            {
                // get the pattern's last_error
                recordedError = m_pattern->GetLastPatternError();
                // no longer allow any more callbacks
                m_pattern->RegisterCallback(nullptr);
            }
        }

        // don't hold any locks when calling back into the parent
        if (const auto refParent = m_parent.lock())
        {
            refParent->CompleteState(recordedError);
        }
    }

    const ctSockaddr& ctsSocket::GetLocalSockaddr() const noexcept
    {
        return m_localSockaddr;
    }

    void ctsSocket::SetLocalSockaddr(const ctSockaddr& localAddress) noexcept
    {
        m_localSockaddr = localAddress;
    }

    const ctSockaddr& ctsSocket::GetRemoteSockaddr() const noexcept
    {
        return m_targetSockaddr;
    }

    void ctsSocket::SetRemoteSockaddr(const ctSockaddr& targetAddress) noexcept
    {
        m_targetSockaddr = targetAddress;
    }

    void ctsSocket::SetIoPattern()
    {
        m_pattern = ctsIoPattern::MakeIoPattern();
        if (!m_pattern)
        {
            // in test scenarios
            return;
        }

        m_pattern->SetParent(shared_from_this());

        if (ctsConfig::g_configSettings->PrePostSends == 0)
        {
            // user didn't specify a specific # of sends to pend
            // start ISB notifications (best-effort)
            InitiateIsbNotification();
        }
    }

    void ctsSocket::InitiateIsbNotification() noexcept try
    {
        const auto sharedThis = shared_from_this();
        const auto lockedSocket(sharedThis->AcquireSocketLock());

        const auto& sharedIocp = GetIocpThreadpool();
        OVERLAPPED* ov = sharedIocp->new_request(
            [weak_this_ptr = std::weak_ptr(shared_from_this())](OVERLAPPED* pOverlapped) noexcept
            {
                const auto lambdaSharedThis = weak_this_ptr.lock();
                if (!lambdaSharedThis)
                {
                    return;
                }

                DWORD gle = NO_ERROR;
                const auto lambdaLockedSocket(lambdaSharedThis->AcquireSocketLock());
                const auto lambdaSocket = lambdaLockedSocket.GetSocket();
                if (lambdaSocket != INVALID_SOCKET)
                {
                    DWORD transferred; // unneeded
                    DWORD flags; // unneeded
                    if (!WSAGetOverlappedResult(lambdaSocket, pOverlapped, &transferred, FALSE, &flags))
                    {
                        gle = WSAGetLastError();
                        if (gle != ERROR_OPERATION_ABORTED && gle != WSAEINTR)
                        {
                            // aborted is expected whenever the socket is closed
                            ctsConfig::PrintErrorIfFailed("WSAIoctl(SIO_IDEAL_SEND_BACKLOG_CHANGE)", gle);
                        }
                    }
                }
                else
                {
                    gle = WSAECANCELLED;
                }

                if (gle == NO_ERROR)
                {
                    // if the request succeeded, handle the ISB change
                    // and issue the next
                    ULONG isb;
                    if (0 == idealsendbacklogquery(lambdaSocket, &isb))
                    {
                        const auto lock = lambdaSharedThis->m_lock.lock();
                        PRINT_DEBUG_INFO(L"\t\tctsSocket::process_isb_notification : setting ISB to %lu bytes\n", isb);
                        lambdaSharedThis->m_pattern->SetIdealSendBacklog(isb);
                    }
                    else
                    {
                        gle = WSAGetLastError();
                        if (gle != ERROR_OPERATION_ABORTED && gle != WSAEINTR)
                        {
                            ctsConfig::PrintErrorIfFailed("WSAIoctl(SIO_IDEAL_SEND_BACKLOG_QUERY)", gle);
                        }
                    }

                    lambdaSharedThis->InitiateIsbNotification();
                }
            }); // lambda for new_request

        const auto localSocket = lockedSocket.GetSocket();
        if (localSocket != INVALID_SOCKET)
        {
            if (SOCKET_ERROR == idealsendbacklognotify(localSocket, ov, nullptr))
            {
                const auto gle = WSAGetLastError();
                // expect this to be pending
                if (gle != WSA_IO_PENDING)
                {
                    // if the ISB notification failed, tell the TP to no longer track that IO
                    sharedIocp->cancel_request(ov);
                    if (gle != ERROR_OPERATION_ABORTED && gle != WSAEINTR)
                    {
                        ctsConfig::PrintErrorIfFailed("WSAIoctl(SIO_IDEAL_SEND_BACKLOG_CHANGE)", gle);
                    }
                }
            }
        }
        else
        {
            // there wasn't a SOCKET to initiate the ISB notification, tell the TP to no longer track that IO
            sharedIocp->cancel_request(ov);
        }
    }
    catch (...)
    {
        ctsConfig::PrintThrownException();
    }

    // AcquireSocketLock() must have been called when calling this
    int32_t ctsSocket::IncrementIo() noexcept
    {
        ++m_ioCount;
        return m_ioCount;
    }

    // AcquireSocketLock() must have been called when calling this
    int32_t ctsSocket::DecrementIo() noexcept
    {
        --m_ioCount;
        FAIL_FAST_IF_MSG(
            m_ioCount < 0,
            "ctsSocket: io count fell below zero (%ld)\n", m_ioCount);
        return m_ioCount;
    }

    // AcquireSocketLock() must have been called when calling this
    int32_t ctsSocket::GetPendedIoCount() const noexcept
    {
        return m_ioCount;
    }

    void ctsSocket::Shutdown() noexcept
    {
        // close the socket to trigger IO to complete/shutdown
        CloseSocket();
        // Must destroy these threadpool objects outside the CS to prevent a deadlock
        // - from when worker threads attempt to callback this ctsSocket object when IO completes
        // Must wait for the threadpool from this method when ctsSocketState calls ctsSocket::shutdown
        // - instead of calling this from the destructor of ctsSocket, as the final reference
        //   to this ctsSocket might be from a TP thread - in which case this destructor will deadlock
        //   (it will wait for all TP threads to exit, but it is using/blocking on of those TP threads)
        m_tpIocp.reset();
        m_tpTimer.reset();
    }

    ///
    /// SetTimer schedules the callback function to be invoked with the given ctsSocket and ctsIOTask
    /// - note that the timer 
    /// - can throw under low resource conditions
    ///
    void ctsSocket::SetTimer(const ctsTask& task, function<void(weak_ptr<ctsSocket>, const ctsTask&)>&& func)
    {
        const auto lock = m_lock.lock();
        m_timerTask = task;
        m_timerCallback = std::move(func);

        if (!m_tpTimer)
        {
            m_tpTimer.reset(
                CreateThreadpoolTimer(ThreadPoolTimerCallback, this, ctsConfig::g_configSettings->pTpEnvironment));
            THROW_LAST_ERROR_IF(!m_tpTimer);
        }

        FILETIME relativeTimeout = wil::filetime::from_int64(
            -1 * wil::filetime_duration::one_millisecond * task.m_timeOffsetMilliseconds);
        SetThreadpoolTimer(m_tpTimer.get(), &relativeTimeout, 0, 0);
    }

    void NTAPI ctsSocket::ThreadPoolTimerCallback(PTP_CALLBACK_INSTANCE, PVOID pContext, PTP_TIMER)
    {
        auto* pThis = static_cast<ctsSocket*>(pContext);

        ctsTask task;
        function<void(weak_ptr<ctsSocket>, const ctsTask&)> callback;

        {
            const auto lock = pThis->m_lock.lock();
            task = pThis->m_timerTask;
            callback = std::move(pThis->m_timerCallback);
        }

        // invoke the callback outside the lock
        callback(pThis->weak_from_this(), task);
    }
} // namespace
