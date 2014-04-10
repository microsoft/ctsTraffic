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
#include <Winsock2.h>
#include <windows.h>
// ctl headers
#include <ctThreadIocp.hpp>
#include <ctThreadPoolTimer.hpp>
#include <ctSockaddr.hpp>
// project headers
#include "ctsIOPattern.h"
#include "ctsIOTask.hpp"



namespace ctsTraffic {
    ///
    /// forward declare ctsSocketState
    /// - can't include ctsSocketState.h in this header to avoid circular declarations
    ///
    class ctsSocketState;


    ///
    /// A safe socket container
    /// - ensures has a lock on the socket while in scope
    ///
    class ctsSocket : public std::enable_shared_from_this<ctsSocket> {
    public:
        ///
        /// c'tor requiring a parent ctsSocket reference
        ///
        ctsSocket(_In_ std::weak_ptr<ctsSocketState> _parent);

        _No_competing_thread_
        ~ctsSocket() throw();

        ///
        /// Assigns the object a new SOCKET value and fully initializes the object for use
        ///
        /// Must still be the default initialized SOCKET value
        /// - if set_socket() is called twice, will RaiseException
        ///
        /// Cannot call any method in this object before this method succeeds
        ///
        /// Does not associate with an IOCP ThreadPool by default
        ///
        /// A no-fail operation
        ///
        void set_socket(SOCKET _socket) throw();

        ///
        /// Callers should call lock_socket() in order to gain access to the SOCKET.
        /// Callers then have exclusive access to the SOCKET until unlock_socket() is called.
        /// Callers are expected to hold this lock just long enough to make API calls with the SOCKET
        /// - immediately after which they should call unlock_socket()
        ///
        /// Callers *must* call unlock_socket() on the same thread as lock_socket()
        /// - the same requirement that comes with a CRITICAL_SECTION 
        /// - the primative these functions use
        ///
        /// Callers are *not* allowed to call closesocket() with the returned SOCKET, even under a lock
        /// - as doing so changes this SOCKET state outside of this container's knowledge
        ///
        /// Callers may call any other method in ctsSocket with or without this lock
        ///
        /// Both functions are no-throw
        ///
        _Acquires_lock_(this->socket_cs) SOCKET lock_socket() throw();
        _Releases_lock_(this->socket_cs) void unlock_socket() throw();

        ///
        /// Safely closes the encapsulated socket 
        /// - this is not necessary nor recommended for typical usage patterns
        /// This is the *only* safe way to close the socket.
        /// - calling closesocket() is not allowed for callers to invoke directly
        /// - as doing so changes this SOCKET state outside of this container's knowledge
        ///
        /// It's made available for injectors who may want to close the SOCKET at random times
        /// 
        /// Does not require the lock_socket
        ///
        void close_socket() throw();

        ///
        /// Provides access to the IOCP ThreadPool associated with the SOCKET
        /// - if not already association with the TP, will associate on the first call
        ///
        /// This can fail under low-resource conditions
        /// - can throw std::bad_alloc or ctl::ctException
        ///
        std::shared_ptr<ctl::ctThreadIocp> thread_pool();

        ///
        /// Callers access initiate_io() to retrieve a ctsIOTask object for the next IO operation
        /// - they are expected to retain that ctsIOTask object until the IO operation completes
        /// - at which time they pass it back to complete_io()
        ///
        /// initiate_io() can be called repeatedly by the caller if they want overlapping IO calls
        /// - without forced to wait for complete_io() for the next IO request
        ///
        /// complete_io() should be called for every returned initiate_io with the following:
        ///   _task : the ctsIOTask that was provided to perform
        ///   _current_transfer : the number of bytes successfully transferred from the task
        ///   _status_code: the return code from the prior IO operation [assumes a Win32 error code]
        ///
        /// initiate_io() cannot fail
        /// complete_io() cannot fail
        ///
        ctsIOTask initiate_io() throw();
        ///
        /// complete_io instructs the caller
        /// - if the IO should be treated as a failure
        /// - if the protocol is complete [no more IO on this socket]
        ///
        /// failure == no more IO on this socket
        ///
        enum IOStatus : unsigned short {
            SuccessMoreIO,
            SuccessDone,
            Failure
        };
        IOStatus complete_io(ctsIOTask _task, unsigned _bytes_transferred, unsigned _status_code) throw();

        ///
        /// Callers are expected to call this when their 'stage' is complete for this SOCKET
        /// The only successful DWORD value is NO_ERROR (0)
        /// Any other DWORD indicates error
        ///
        void complete_state(DWORD _dwerror) throw();

        ///
        /// Gets/Sets the local address of the SOCKET
        ///
        const ctl::ctSockaddr get_local() const throw();
        void set_local(const ctl::ctSockaddr& _target) throw();

        ///
        /// Gets/Sets the target address of the SOCKET, if there is one
        ///
        const ctl::ctSockaddr get_target() const throw();
        void set_target(const ctl::ctSockaddr& _target) throw();

        ///
        /// Makes available to callers the error code recorded for the socket
        /// Will return MAXUINT while still connected and processing IO
        ///
        unsigned get_last_error() const throw();

        ///
        /// methods for functors to use for refcounting the # of IO they have issued on this socket
        ///
        LONG increment_io() throw();
        LONG decrement_io() throw();

        ///
        /// methods for constructing a new IO Pattern
        /// - construct returns a Win32 error code if can construct the pattern
        ///
        DWORD construct_pattern() throw();

        ///
        /// method for the parent to instruct the ctsSocket to print the connection data
        /// - which it is tracking, including the internal statistics
        ///
        void print_pattern_results() const throw();

        ///
        /// Optional callback function which is passed down to the IOPattern instance
        /// 
        void register_pattern_callback(std::function<void(const ctsIOTask&)> _callback);

        ///
        /// Function to register a task for completion at the future point in time referenced
        /// - by ctsIOTask::time_offset_milliseconds
        ///
        /// set_timer stores a weak_ptr to 'this' ctsSocket object
        /// - so that the object lifetime is not maintained just from a scheduled work item
        ///
        void set_timer(const ctsIOTask& _task, std::function<void(std::weak_ptr<ctsSocket>, const ctsIOTask&)> _func);

        // no default c'tor, not copyable
        ctsSocket() = delete;
        ctsSocket(const ctsSocket&) = delete;
        ctsSocket& operator= (const ctsSocket&) = delete;

    private:
        ///
        /// ctsSocketState is given friend-access to call shutdown and set_last_error
        ///
        friend class ctsSocketState;
        void shutdown() throw();
        void set_last_error(DWORD _error) throw();
        void reset_last_error() throw();

        // private members for this socket instance
        // mutable is requred to EnterCS/LeaveCS in const methods

        mutable CRITICAL_SECTION            socket_cs;

        _Guarded_by_(socket_cs)
        SOCKET                              socket;

        _Interlocked_
        LONG                                io_count;

        // maintain a weak-reference to the parent
        std::weak_ptr<ctsSocketState>       parent;

        /// only guarded when returning to the caller
        std::shared_ptr<ctl::ctThreadIocp>      tp_iocp;
        std::shared_ptr<ctl::ctThreadpoolTimer> tp_timer;

        /// to avoid race conditions, can't be guarded when calling into the IOPattern
        // _Requires_lock_not_held_(socket_cs)
        std::shared_ptr<ctsIOPattern>       io_pattern;

        ctl::ctSockaddr                     local_address;
        ctl::ctSockaddr                     target_address;

        _Guarded_by_(socket_cs)
        unsigned                            last_error;
    };

} // namespace
