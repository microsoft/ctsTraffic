/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// parent header
#include "ctsSocket.h"

// ctl headers
#include <ctLocks.hpp>
#include <ctThreadPoolTimer.hpp>

// project headers
#include "ctsConfig.h"
#include "ctsSocketState.h"


namespace ctsTraffic {

    using namespace ctl;
    using namespace std;

    // default values are assigned in the class declaration
    ctsSocket::ctsSocket(const weak_ptr<ctsSocketState>& _parent) : parent(_parent)
    {
        /// using a common spin count from base OS usage & crt usage
        if (!::InitializeCriticalSectionEx(&this->socket_cs, 4000, 0)) {
            throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsSocket", false);
        }
    }

    _No_competing_thread_
    ctsSocket::~ctsSocket() NOEXCEPT
    {
        // shutdown() tears down the socket object
        this->shutdown();

        // if the IO pattern is still alive, must delete it once in the d'tor before this object goes away
        // - can't reset this in ctsSocket::shutdown since ctsSocket::shutdown can be called from the parent ctsSocketState 
        //   and there may be callbacks still running holding onto a reference to this ctsSocket object
        //   which causes the potential to AV in the io_pattern
        //   (a race-condition touching the io_pattern with deleting the io_pattern)
        this->pattern.reset();

        ::DeleteCriticalSection(&this->socket_cs);
    }

    _Acquires_lock_(socket_cs)
    void ctsSocket::lock_socket() const NOEXCEPT
    {
        ::EnterCriticalSection(&this->socket_cs);
    }

    _Releases_lock_(socket_cs)
    void ctsSocket::unlock_socket() const NOEXCEPT
    {
        ::LeaveCriticalSection(&this->socket_cs);
    }

    void ctsSocket::set_socket(SOCKET _socket) NOEXCEPT
    {
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);

        ctl::ctFatalCondition(
            (this->socket != INVALID_SOCKET),
            L"ctsSocket::set_socket trying to set a SOCKET (%Iu) when it has already been set in this object (%Iu)",
            _socket, this->socket);

        this->socket = _socket;
    }

    void ctsSocket::close_socket() NOEXCEPT
    {
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);
        if (this->socket != INVALID_SOCKET) {
            ::closesocket(this->socket);
            this->socket = INVALID_SOCKET;
        }
    }

    const shared_ptr<ctThreadIocp>& ctsSocket::thread_pool()
    {
        // use the SOCKET cs to also guard creation of this TP object
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);

        // must verify a valid socket first to avoid racing destrying the iocp shared_ptr as we try to create it here
        if ((this->socket != INVALID_SOCKET) && (!this->tp_iocp)) {
            this->tp_iocp = make_shared<ctThreadIocp>(this->socket, ctsConfig::Settings->PTPEnvironment); // can throw
        }

        return this->tp_iocp;
    }

    void ctsSocket::print_pattern_results(unsigned long _last_error) const NOEXCEPT
    {
        if (this->pattern) {
            this->pattern->print_stats(
                this->local_address(),
                this->target_address());
        } else {
            // failed during socket creation, bind, or connect
            ctsConfig::PrintConnectionResults(
                this->local_address(),
                this->target_address(),
                _last_error);

        }
    }

    void ctsSocket::complete_state(DWORD _error_code) NOEXCEPT
    {
        auto current_io_count = ctMemoryGuardRead(&this->io_count);
        ctFatalCondition(
            (current_io_count != 0),
            L"ctsSocket::complete_state is called with outstanding IO (%d)", current_io_count);

        DWORD recorded_error = _error_code;
        if (this->pattern) {
            // get the pattern's last_error
            recorded_error = this->pattern->get_last_error();
            // no longer allow any more callbacks
            this->pattern->register_callback(nullptr);
        }

        auto ref_parent(this->parent.lock());
        if (ref_parent) {
            ref_parent->complete_state(recorded_error);
        }
    }

    const ctSockaddr& ctsSocket::local_address() const NOEXCEPT
    {
        return this->local_sockaddr;
    }

    void ctsSocket::set_local_address(const ctSockaddr& _local) NOEXCEPT
    {
        this->local_sockaddr = _local;
    }

    const ctSockaddr& ctsSocket::target_address() const NOEXCEPT
    {
        return this->target_sockaddr;
    }

    void ctsSocket::set_target_address(const ctSockaddr& _target) NOEXCEPT
    {
        this->target_sockaddr = _target;
    }

    shared_ptr<ctsIOPattern> ctsSocket::io_pattern() const NOEXCEPT
    {
        return this->pattern;
    }

    void ctsSocket::set_io_pattern(const std::shared_ptr<ctsIOPattern>& _pattern) NOEXCEPT
    {
        this->pattern = _pattern;
        if (ctsConfig::Settings->PrePostSends == 0) {
            // user didn't specify a specific # of sends to pend
            // start ISB notifications (best effort)
            this->initiate_isb_notification();
        }
    }

    void ctsSocket::process_isb_notification()
    {
        // lock the socket
        auto socket_lock(ctsGuardSocket(shared_from_this()));
        SOCKET local_socket = socket_lock.get();
        if (local_socket != INVALID_SOCKET) {
            ULONG isb;
            if (0 == ::idealsendbacklogquery(local_socket, &isb)) {
                ctsConfig::PrintDebug(L"\t\tctsSocket::process_isb_notification : setting ISB to %u bytes\n", isb);
                this->pattern->set_ideal_send_backlog(isb);
            } else {
                int gle = ::WSAGetLastError();
                if (gle != ERROR_OPERATION_ABORTED && gle != WSAEINTR) {
                    ctsConfig::PrintErrorIfFailed(L"WSAIoctl(SIO_IDEAL_SEND_BACKLOG_QUERY)", gle);
                }
            }
        }
    }

    void ctsSocket::initiate_isb_notification()
    {
        try {
            auto& shared_iocp = thread_pool();
            LPOVERLAPPED ov = shared_iocp->new_request([weak_this_ptr = std::weak_ptr<ctsSocket>(this->shared_from_this())](OVERLAPPED* ov) {
                DWORD gle = NO_ERROR;

                auto shared_this_ptr = weak_this_ptr.lock();
                if (!shared_this_ptr) {
                    return;
                }

                auto socket_lock(ctsGuardSocket(shared_this_ptr));
                SOCKET local_socket = socket_lock.get();
                if (local_socket != INVALID_SOCKET) {
                    DWORD transferred, flags; // unneeded
                    if (!::WSAGetOverlappedResult(local_socket, ov, &transferred, FALSE, &flags)) {
                        gle = ::WSAGetLastError();
                        if (gle != ERROR_OPERATION_ABORTED && gle != WSAEINTR) {
                            // aborted is expected whenever the socket is closed
                            ctsConfig::PrintErrorIfFailed(L"WSAIoctl(SIO_IDEAL_SEND_BACKLOG_CHANGE)", gle);
                        }
                    }
                } else {
                    gle = WSAECANCELLED;
                }
                if (gle == NO_ERROR) {
                    // if the request succeeded, handle the ISB change
                    // and issue the next
                    shared_this_ptr->process_isb_notification();
                    shared_this_ptr->initiate_isb_notification();
                }
            }); // lambda for new_request

            auto socket_lock(ctsGuardSocket(this->shared_from_this()));
            SOCKET local_socket = socket_lock.get();
            if (local_socket != INVALID_SOCKET) {
                int error = ::idealsendbacklognotify(local_socket, ov, nullptr);
                if (SOCKET_ERROR == error) {
                    int gle = ::WSAGetLastError();
                    // expect this to be pending
                    if (gle != WSA_IO_PENDING)
                    {
                        // if the ISB notification failed, tell the TP to no longer track that IO
                        shared_iocp->cancel_request(ov);
                        if (gle != ERROR_OPERATION_ABORTED && gle != WSAEINTR) {
                            ctsConfig::PrintErrorIfFailed(L"WSAIoctl(SIO_IDEAL_SEND_BACKLOG_CHANGE)", gle);
                        }
                    }
                }
            } else {
                // there wasn't a SOCKET to initiate the ISB notification, tell the TP to no longer track that IO
                shared_iocp->cancel_request(ov);
            }
        }
        catch (const exception& e) {
            ctsConfig::PrintException(e);
        }
    }

    long ctsSocket::increment_io() NOEXCEPT
    {
        return ctMemoryGuardIncrement(&this->io_count);
    }

    long ctsSocket::decrement_io() NOEXCEPT
    {
        auto io_value = ctMemoryGuardDecrement(&this->io_count);
        ctl::ctFatalCondition(
            (io_value < 0),
            L"ctsSocket: io count fell below zero (%d)\n", io_value);
        return io_value;
    }

    long ctsSocket::pended_io() NOEXCEPT
    {
        return ctMemoryGuardRead(&this->io_count);
    }

    void ctsSocket::shutdown() NOEXCEPT
    {
        // close the socket to trigger IO to complete/shutdown
        this->close_socket();
        // Must destroy these threadpool objects outside the CS to prevent a deadlock
        // - from when worker threads attempt to callback this ctsSocket object when IO completes
        // Must wait for the threadpool from this method when ctsSocketState calls ctsSocket::shutdown
        // - instead of calling this from the d'tor of ctsSocket, as the final reference
        //   to this ctsSocket might be from a TP thread - in which case this d'tor will deadlock
        //   (it will wait for all TP threads to exit, but it is using/blocking on of those TP threads)
        this->tp_iocp.reset();
        this->tp_timer.reset();
    }

    ///
    /// SetTimer schedules the callback function to be invoked with the given ctsSocket and ctsIOTask
    /// - note that the timer 
    /// - can throw under low resource conditions
    ///
    void ctsSocket::set_timer(const ctsIOTask& _task, function<void(weak_ptr<ctsSocket>, const ctsIOTask&)> _func)
    {
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);
        if (!this->tp_timer) {
            this->tp_timer = make_shared<ctl::ctThreadpoolTimer>(ctsConfig::Settings->PTPEnvironment);
        }
        
        // register a weak pointer after creating a shared_ptr from the 'this' ptry
        weak_ptr<ctsSocket> weak_reference(this->shared_from_this());

        this->tp_timer->schedule_singleton(
            [_func = std::move(_func), weak_reference, _task] () { _func(weak_reference, _task); },
            _task.time_offset_milliseconds);
    }

} // namespace
