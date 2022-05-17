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
#include <functional>
#include <type_traits>
#include <utility>
// os headers
#include <Windows.h>
#include <WinSock2.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
// project headers
#include "ctsIOPattern.h"
#include "ctsIOTask.hpp"

namespace ctsTraffic
{
//
// forward declare ctsSocketState
// - can't include ctsSocketState.h in this header to avoid circular declarations
//
class ctsSocketState;

//
// A safe socket container
// - ensures has a lock on the socket while in scope
//
class ctsSocket : public std::enable_shared_from_this<ctsSocket>
{
public:
    class SocketReference
    {
    public:
        SocketReference(SocketReference&&) = default;
        SocketReference& operator=(SocketReference&&) = delete;
        SocketReference(const SocketReference&) = delete;
        SocketReference& operator=(const SocketReference&) = delete;
        ~SocketReference() = default;

        [[nodiscard]] SOCKET GetSocket() const noexcept
        {
            return m_socket;
        }

        [[nodiscard]] std::shared_ptr<ctsIoPattern> GetPattern() const noexcept
        {
            return m_patternWeakRef.lock();
        }

    private:
        friend class ctsSocket;

        SocketReference(wil::cs_leave_scope_exit&& socketLock, SOCKET socket, std::weak_ptr<ctsIoPattern> patternWeakRef) noexcept :
            m_socketLock(std::move(socketLock)), m_socket(socket), m_patternWeakRef(std::move(patternWeakRef))
        {
        }

        const wil::cs_leave_scope_exit m_socketLock;
        const SOCKET m_socket = INVALID_SOCKET;
        const std::weak_ptr<ctsIoPattern> m_patternWeakRef;
    };

    [[nodiscard]] SocketReference AcquireSocketLock() const noexcept;

    //
    // c'tor requiring a parent ctsSocketState weak reference
    //
    explicit ctsSocket(std::weak_ptr<ctsSocketState> parent) noexcept;

    _No_competing_thread_ ~ctsSocket() noexcept;

    //
    // Assigns the object a new SOCKET value and fully initializes the object for use
    //
    // Must still be the default initialized SOCKET value
    // - if set_socket() is called twice, will RaiseException
    //
    // Cannot call any method in this object before this method succeeds
    //
    // A no-fail operation
    //
    void SetSocket(SOCKET socket) noexcept;

    //
    // Safely closes the encapsulated socket 
    // - this is not necessary nor recommended for typical usage patterns
    // This is the *only* safe way to close the socket.
    // - calling closesocket() is not allowed for callers to invoke directly
    // - as doing so changes this SOCKET state outside of this container's knowledge
    //
    // It's made available for injectors who may want to close the SOCKET at random times
    // 
    int CloseSocket(uint32_t errorCode = NO_ERROR) noexcept;

    //
    // Provides access to the IOCP ThreadPool associated with the SOCKET
    // - if not already association with the TP, will associate on the first call
    //
    // This can fail under low-resource conditions
    // - can throw std::bad_alloc or wil::ResultException
    //
    const std::shared_ptr<ctl::ctThreadIocp>& GetIocpThreadpool();

    //
    // Callers are expected to call this when their 'stage' is complete for this SOCKET
    // The only successful DWORD value is NO_ERROR (0)
    // Any other DWORD indicates error
    //
    void CompleteState(DWORD errorCode) noexcept;

    //
    // Gets/Sets the local address of the SOCKET
    //
    const ctl::ctSockaddr& GetLocalSockaddr() const noexcept;
    void SetLocalSockaddr(const ctl::ctSockaddr& localAddress) noexcept;

    //
    // Gets/Sets the target address of the SOCKET, if there is one
    //
    const ctl::ctSockaddr& GetRemoteSockaddr() const noexcept;
    void SetRemoteSockaddr(const ctl::ctSockaddr& targetAddress) noexcept;

    //
    // Get/Set the ctsIOPattern
    //
    void SetIoPattern();

    //
    // methods for functors to use for refcounting the # of IO they have issued on this socket
    //
    int32_t IncrementIo() noexcept;
    int32_t DecrementIo() noexcept;
    int32_t GetPendedIoCount() noexcept;

    //
    // method for the parent to instruct the ctsSocket to print the connection data
    // - which it is tracking, including the internal statistics
    //
    void PrintPatternResults(uint32_t lastError) const noexcept;

    //
    // Function to register a task for completion at the future point in time referenced
    // - by ctsIOTask::time_offset_milliseconds
    //
    // set_timer stores a weak_ptr to 'this' ctsSocket object
    // - so that the object lifetime is not maintained just from a scheduled work item
    //
    void SetTimer(const ctsTask& task, std::function<void(std::weak_ptr<ctsSocket>, const ctsTask&)>&& func);

    // not copyable or movable
    ctsSocket(const ctsSocket&) = delete;
    ctsSocket& operator=(const ctsSocket&) = delete;
    ctsSocket(ctsSocket&&) = delete;
    ctsSocket& operator=(ctsSocket&&) = delete;

private:
    // ctsSocketState is given friend-access to call shutdown
    friend class ctsSocketState;
    void Shutdown() noexcept;

    // ctsIoPattern is given friend-access to acquire the socket lock
    friend class ctsIoPattern;

    auto AcquireLock() const noexcept
    {
        return m_lock.lock();
    }

    void InitiateIsbNotification() noexcept;

    // private members for this socket instance
    // mutable is requred to EnterCS/LeaveCS in const methods

    mutable wil::critical_section m_lock{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
    _Guarded_by_(m_lock) wil::unique_socket m_socket;
    _Interlocked_ long m_ioCount = 0L;

    // maintain a weak-reference to the parent and child
    std::weak_ptr<ctsSocketState> m_parent;
    // maintain a shared_ptr to the pattern
    std::shared_ptr<ctsIoPattern> m_pattern;

    /// only guarded when returning to the caller
    std::shared_ptr<ctl::ctThreadIocp> m_tpIocp;
    wil::unique_threadpool_timer m_tpTimer;
    ctsTask m_timerTask{};
    std::function<void(std::weak_ptr<ctsSocket>, const ctsTask&)> m_timerCallback;

    ctl::ctSockaddr m_localSockaddr;
    ctl::ctSockaddr m_targetSockaddr;

    static void NTAPI ThreadPoolTimerCallback(PTP_CALLBACK_INSTANCE, PVOID pContext, PTP_TIMER);
};
} // namespace
