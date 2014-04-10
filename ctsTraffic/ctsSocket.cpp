/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

/// parent header
#include "ctsSocket.h"

/// c++ headers
#include <exception>

/// ctl headers
#include <ctLocks.hpp>
#include <ctString.hpp>
#include <ctTimer.hpp>
#include <ctThreadPoolTimer.hpp>

// project headers
#include "ctsConfig.h"
#include "ctsSocketState.h"


namespace ctsTraffic {

    using namespace ctl;
    using namespace std;

    ctsSocket::ctsSocket(_In_ std::weak_ptr<ctsSocketState> _parent)
    : socket_cs(),
      socket(INVALID_SOCKET),
      io_count(0),
      local_address(),
      target_address(),
      tp_iocp(),
      tp_timer(),
      io_pattern(),
      parent(_parent),
      last_error(ctsIOPatternStatusIORunning)
    {
        /// using a common spin count from base OS usage & crt usage
        if (!::InitializeCriticalSectionEx(&this->socket_cs, 4000, 0)) {
            ctl::ctAlwaysFatalCondition(L"InitializeCriticalSectionEx failed [%u]", ::GetLastError());
        }
    }

    _No_competing_thread_
    ctsSocket::~ctsSocket() throw()
    {
        // shutdown() tears down the socket object
        this->shutdown();

        // if the IO pattern is still alive, must delete it once in the d'tor before this object goes away
        // - can't reset this in ctsSocket::shutdown since ctsSocket::shutdown can be called from the parent ctsSocketState 
        //   and there may be callbacks still running holding onto a reference to this ctsSocket object
        //   which causes the potential to AV in the io_pattern
        //   (a race-condition touching the io_pattern with deleting the io_pattern)
        this->io_pattern.reset();

        ::DeleteCriticalSection(&this->socket_cs);
    }

    _Acquires_lock_(this->socket_cs)
    SOCKET ctsSocket::lock_socket() throw()
    {
        ::EnterCriticalSection(&this->socket_cs);
        return this->socket;
    }

    _Releases_lock_(this->socket_cs)
    void ctsSocket::unlock_socket() throw()
    {
        ::LeaveCriticalSection(&this->socket_cs);
    }

    void ctsSocket::set_socket(SOCKET _socket) throw()
    {
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);

        ctl::ctFatalCondition(
            (this->socket != INVALID_SOCKET),
            L"ctsSocket::set_socket trying to set a SOCKET (%Iu) when it has already been set in this object (%Iu)",
            _socket, this->socket);

        this->socket = _socket;
    }

    void ctsSocket::close_socket() throw()
    {
        // not holding the socket lock when trying to call back through the iopattern
        // - to avoid potential deadlocks
        auto ref_io_pattern(this->io_pattern);
        if (ref_io_pattern) {
            ref_io_pattern->end_pattern();
        }

        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);

        if (this->socket != INVALID_SOCKET) {
            ::closesocket(this->socket);
            this->socket = INVALID_SOCKET;
        }
    }

    std::shared_ptr<ctThreadIocp> ctsSocket::thread_pool()
    {
        // use the SOCKET cs to also guard creation of this TP object
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);

        // must verify a valid socket first to avoid racing destrying the iocp shared_ptr as we try to create it here
        if ((this->socket != INVALID_SOCKET) && (!this->tp_iocp)) {
            this->tp_iocp = std::make_shared<ctThreadIocp>(this->socket, ctsConfig::Settings->PTPEnvironment); // can throw
        }

        return this->tp_iocp;
    }

    DWORD ctsSocket::construct_pattern() throw()
    {
        // *NOT* taking a ctsSocket lock before calling through io_pattern
        // - as IOPattern can also initiate calls through ctsSocket, which can then deadlock
        // caller (parent) is assumed to serialize access
        try {
            this->io_pattern = ctsIOPattern::MakeIOPattern();
        }
        catch (const ctException& e) {
            return (e.why() != 0 ? e.why() : ERROR_OUTOFMEMORY);
        }
        catch (const exception&) {
            return ERROR_OUTOFMEMORY;
        }
        return NO_ERROR;
    }

    void ctsSocket::print_pattern_results() const throw()
    {
        auto ref_io_pattern(this->io_pattern);
        if (ref_io_pattern) {
            ref_io_pattern->print_io_results(
                this->get_local(),
                this->get_target(),
                this->get_last_error());
        } else {
            // otherwise this failed before we ever constructed an io_pattern
            // - provide an empty statistics object
            if (ctsConfig::ProtocolType::TCP == ctsConfig::Settings->Protocol) {
                ctsTcpStatistics empty_stats(0LL);
                ctsConfig::PrintConnectionResults(
                    this->get_local(),
                    this->get_target(),
                    this->get_last_error(),
                    empty_stats);
            } else {
                ctsUdpStatistics empty_stats(0LL);
                ctsConfig::PrintConnectionResults(
                    this->get_local(),
                    this->get_target(),
                    this->get_last_error(),
                    empty_stats);
            }
        }
    }

    void ctsSocket::register_pattern_callback(std::function<void(const ctsIOTask&)> _callback)
    {
        // *NOT* taking a ctsSocket lock before calling through io_pattern
        // - as IOPattern can also initiate calls through ctsSocket, which can then deadlock
        // Instead just holding a ref on the pattern
        auto ref_io_pattern(this->io_pattern);
        if (ref_io_pattern) {
            ref_io_pattern->register_callback(_callback);
        }
    }

    ctsIOTask ctsSocket::initiate_io() throw()
    {
        // *NOT* taking a ctsSocket lock before calling through io_pattern
        // - as IOPattern can also initiate calls through ctsSocket, which can then deadlock
        // default is an empty task to do nothing
        ctsIOTask return_task;
        auto ref_io_pattern(this->io_pattern);
        if (ref_io_pattern) {
            return_task = ref_io_pattern->initiate_io();
        }
        return return_task;
    }

    ///
    /// complete_io needs to inform the caller of what to do next:
    ///
    /// - No errors (ignore status), we just don't need to send more IO right now
    /// - No errors (ignore status), we are done sending IO
    /// - Error - we are done sending IO
    ///
    ctsSocket::IOStatus ctsSocket::complete_io(ctsIOTask _task, unsigned _bytes_transferred, unsigned _status_code) throw()
    {
        //
        // ignore completions for tasks on None
        // 
        if (ctsIOTask::IOAction::None == _task.ioAction) {
            return IOStatus::SuccessMoreIO;
        }
        //
        // if FatalAbort, no IO was completed, but last_error might need to be set (only if not yet set to an error)
        //
        if (ctsIOTask::IOAction::FatalAbort == _task.ioAction) {
            this->set_last_error(_status_code);
            return IOStatus::Failure;
        }
        //
        // pass all other completions to the protocol layer
        // *NOT* taking a ctsSocket lock before calling through io_pattern
        // - as IOPattern can also initiate calls through ctsSocket, which can then deadlock
        //
        ctsIOPatternStatus next_pattern_status;
        auto ref_io_pattern(this->io_pattern);
        if (!ref_io_pattern) {
            this->set_last_error(WSAENOTSOCK);
            next_pattern_status = ctsIOPatternStatus::ErrorIOFailed;
        } else {
            // not holding a lock when calling back through the ctsIOPattern
            next_pattern_status = ref_io_pattern->complete_io(_task, _bytes_transferred, _status_code);
        }
        //
        // now that we know the pattern status, update last_error as needed
        //
        if (NO_ERROR == _status_code) {
            // If IO failed, capture the IO error
            if (ctsIOPatternProtocolError(next_pattern_status)) {
                this->set_last_error(next_pattern_status);
            }
        } else {
            // _status_code is an error and the IOPattern didn't choose to ignore it
            if (ctsIOPatternError(next_pattern_status)) {
                this->set_last_error(_status_code);
            }
        }
        //
        // if we now need a FIN, invoke shutdown() first to ensure a FIN is sent to the target
        //
        if (ctsIOPatternStatus::RequestFIN == next_pattern_status) {
            SOCKET s = this->lock_socket();
            { // scoping the lifetime of the lock
                if (s != INVALID_SOCKET) {
                    if (SOCKET_ERROR == ::shutdown(this->socket, SD_SEND)) {
                        auto gle = ::WSAGetLastError();
                        this->set_last_error(gle);

                        ctsConfig::PrintErrorInfo(
                            L"[%.3f] ctsSocket - failed to initiate shutdown(SD_SEND) [%d] for a graceful disconnect",
                            ctsConfig::GetStatusTimeStamp(),
                            gle);

                        // can't continue - can't reliably request a FIN
                        next_pattern_status = ctsIOPatternStatus::ErrorIOFailed;
                    }
                } else {
                    // otherwise indicate could not set 
                    this->set_last_error(WSAENOTSOCK);
                    // can't continue - can't reliably request a FIN
                    next_pattern_status = ctsIOPatternStatus::ErrorIOFailed;
                }
            }
            this->unlock_socket();

        } else if (ctsIOPatternStatus::CompletedTransfer == next_pattern_status) {
            // If the protocol has successfully completed, update last_error to no longer be ctsIOPatternStatusIORunning
            this->set_last_error(NO_ERROR);
        }
        //
        // return to the user how to interpret this IO
        //
        if (ctsIOPatternError(next_pattern_status)) {
            // always close the socket if the protocol sees this as a failure
            this->close_socket();
            return IOStatus::Failure;

        } else if (ctsIOPatternContinueIO(next_pattern_status)) {
            return IOStatus::SuccessMoreIO;

        } else {
            ctl::ctFatalCondition(
                (ctsIOPatternStatus::CompletedTransfer != next_pattern_status),
                L"ctsSocket: Invalid ctsIOPatternStatus (%u)\n", next_pattern_status);
            // If UDP, close the socket as pended IO could now be blocked forever
            if (ctsConfig::Settings->Protocol != ctsConfig::ProtocolType::TCP) {
                this->close_socket();
            }
            return IOStatus::SuccessDone;
        }
    }

    void ctsSocket::complete_state(DWORD _dwerror) throw()
    {
        LONG current_io_count = ::InterlockedCompareExchange(&this->io_count, 0, 0);
        ctFatalCondition(
            current_io_count != 0,
            L"ctsSocket::complete_state is called with outstanding IO (%d)", current_io_count);

        //
        // *NOT* taking a ctsSocket lock before calling through io_pattern
        // - as IOPattern can also initiate calls through ctsSocket, which can then deadlock
        //
        DWORD recorded_error = _dwerror;
        auto ref_io_pattern(this->io_pattern);
        if (ref_io_pattern) {
            if ((ctsIOPatternStatusIORunning == this->get_last_error()) && (NO_ERROR == _dwerror)) {
                recorded_error = ref_io_pattern->verify_io();
            }
            // no longer allow any more callbacks
            ref_io_pattern->register_callback(nullptr);
        }

        // update last_error with those results
        this->set_last_error(recorded_error);

        auto ref_parent(this->parent.lock());
        if (ref_parent) {
            auto gle = this->get_last_error();
            ref_parent->complete_state(gle);
        }
    }

    const ctSockaddr ctsSocket::get_local() const throw()
    {
        return this->local_address;
    }

    void ctsSocket::set_local(const ctSockaddr& _target) throw()
    {
        this->local_address = _target;
    }

    const ctSockaddr ctsSocket::get_target() const throw()
    {
        return this->target_address;
    }

    void ctsSocket::set_target(const ctSockaddr& _target) throw()
    {
        this->target_address = _target;
    }

    LONG ctsSocket::increment_io() throw()
    {
        return ::InterlockedIncrement(&this->io_count);
    }

    LONG ctsSocket::decrement_io() throw()
    {
        LONG io_value = ::InterlockedDecrement(&this->io_count);
        ctl::ctFatalCondition(
            (io_value < 0),
            L"ctsSocket: io count fell below zero (%d)\n", io_value);
        return io_value;
    }

    unsigned int ctsSocket::get_last_error() const throw()
    {
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);
        return this->last_error;
    }

    ///
    /// set_last_error will attempt to keep the first error reported
    /// - this will only update the value if an error has not yet been report for this state
    /// reset_last_error will directly overwrite last_error back for the start of the next state
    ///
    /// both of these methods are private, only to be called by ctsSocket and ctsSocketState
    ///
    void ctsSocket::set_last_error(DWORD _error) throw()
    {
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);
        // update last_error under lock, only if not previously set
        if (ctsIOPatternStatusIORunning == this->last_error) {
            this->last_error = _error;
        }
    }

    void ctsSocket::reset_last_error() throw()
    {
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);
        this->last_error = ctsIOPatternStatusIORunning;
    }

    void ctsSocket::shutdown() throw()
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
    void ctsSocket::set_timer(const ctsIOTask& _task, std::function<void(std::weak_ptr<ctsSocket>, const ctsIOTask&)> _func)
    {
        ctAutoReleaseCriticalSection auto_lock(&this->socket_cs);
        if (!this->tp_timer) {
            this->tp_timer = std::make_shared<ctl::ctThreadpoolTimer>(ctsConfig::Settings->PTPEnvironment);
        }
        // register a weak pointer after creating a shared_ptr from the 'this' ptry
        this->tp_timer->schedule_singleton(
            _func,
            std::weak_ptr<ctsSocket>(this->shared_from_this()),
            _task,
            _task.time_offset_milliseconds);
    }

} // namespace
