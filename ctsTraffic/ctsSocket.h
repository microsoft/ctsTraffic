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
// os headers
#include <windows.h>
#include <Winsock2.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctThreadIocp.hpp>
#include <ctThreadPoolTimer.hpp>
#include <ctSockaddr.hpp>
#include <ctException.hpp>
// project headers
#include "ctsIOPattern.h"
#include "ctsIOTask.hpp"
#include "ctsSocketGuard.hpp"


namespace ctsTraffic {
    //
    // forward declare ctsSocketState
    // - can't include ctsSocketState.h in this header to avoid circular declarations
    //
    class ctsSocketState;

    //
    // A safe socket container
    // - ensures has a lock on the socket while in scope
    //
    class ctsSocket : public std::enable_shared_from_this<ctsSocket> {
    public:
        //
        // c'tor requiring a parent ctsSocket reference
        //
        ctsSocket(_In_ std::weak_ptr<ctsSocketState> _parent);

        _No_competing_thread_
        ~ctsSocket() NOEXCEPT;

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
        void set_socket(SOCKET _socket) NOEXCEPT;

        //
        // Safely closes the encapsulated socket 
        // - this is not necessary nor recommended for typical usage patterns
        // This is the *only* safe way to close the socket.
        // - calling closesocket() is not allowed for callers to invoke directly
        // - as doing so changes this SOCKET state outside of this container's knowledge
        //
        // It's made available for injectors who may want to close the SOCKET at random times
        // 
        void close_socket() NOEXCEPT;

        //
        // Provides access to the IOCP ThreadPool associated with the SOCKET
        // - if not already association with the TP, will associate on the first call
        //
        // This can fail under low-resource conditions
        // - can throw std::bad_alloc or ctl::ctException
        //
        std::shared_ptr<ctl::ctThreadIocp> thread_pool();

        //
        // Callers are expected to call this when their 'stage' is complete for this SOCKET
        // The only successful DWORD value is NO_ERROR (0)
        // Any other DWORD indicates error
        //
        void complete_state(DWORD _error_code) NOEXCEPT;

        //
        // Gets/Sets the local address of the SOCKET
        //
        const ctl::ctSockaddr local_address() const NOEXCEPT;
        void set_local_address(const ctl::ctSockaddr& _local) NOEXCEPT;

        //
        // Gets/Sets the target address of the SOCKET, if there is one
        //
        const ctl::ctSockaddr target_address() const NOEXCEPT;
        void set_target_address(const ctl::ctSockaddr& _target) NOEXCEPT;

        //
        // Get/Set the ctsIOPattern
        //
        std::shared_ptr<ctsIOPattern> io_pattern() const NOEXCEPT;
        void set_io_pattern(const std::shared_ptr<ctsIOPattern>& _pattern) NOEXCEPT;

        //
        // methods for functors to use for refcounting the # of IO they have issued on this socket
        //
        long increment_io() NOEXCEPT;
        long decrement_io() NOEXCEPT;
        long pended_io() NOEXCEPT;

        //
        // method for the parent to instruct the ctsSocket to print the connection data
        // - which it is tracking, including the internal statistics
        //
        void print_pattern_results(unsigned long _last_error) const NOEXCEPT;

        //
        // Function to register a task for completion at the future point in time referenced
        // - by ctsIOTask::time_offset_milliseconds
        //
        // set_timer stores a weak_ptr to 'this' ctsSocket object
        // - so that the object lifetime is not maintained just from a scheduled work item
        //
        void set_timer(const ctsIOTask& _task, std::function<void(std::weak_ptr<ctsSocket>, const ctsIOTask&)> _func);

        // no default c'tor, not copyable
        ctsSocket() = delete;
        ctsSocket(const ctsSocket&) = delete;
        ctsSocket& operator= (const ctsSocket&) = delete;

    private:
        //
        // ctsSocketState is given friend-access to call shutdown
        //
        friend class ctsSocketState;
        void shutdown() NOEXCEPT;

        // ctsSocketGuard is given friend-access to call lock & unlock
        friend class ctsSocketGuard <std::shared_ptr<ctsSocket>>;
        _Acquires_lock_(socket_cs) void lock_socket() const NOEXCEPT;
        _Releases_lock_(socket_cs) void unlock_socket() const NOEXCEPT;

        // private members for this socket instance
        // mutable is requred to EnterCS/LeaveCS in const methods

        mutable CRITICAL_SECTION socket_cs;
        _Guarded_by_(socket_cs) SOCKET socket;
        _Interlocked_ long io_count;

        // maintain a weak-reference to the parent and child
        std::weak_ptr<ctsSocketState> parent;
        // maintain a shared_ptr to the pattern
        std::shared_ptr<ctsIOPattern> pattern;

        /// only guarded when returning to the caller
        std::shared_ptr<ctl::ctThreadIocp>      tp_iocp;
        std::shared_ptr<ctl::ctThreadpoolTimer> tp_timer;

        ctl::ctSockaddr local_sockaddr;
        ctl::ctSockaddr target_sockaddr;
    };
} // namespace
