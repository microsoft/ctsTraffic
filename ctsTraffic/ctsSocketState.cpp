/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// parent header
#include "ctsSocketState.h"

// ctl headers
#include <ctString.hpp>
#include <ctTimer.hpp>
#include <ctLocks.hpp>
// local headers
#include "ctsSocket.h"
#include "ctsSocketBroker.h"
#include "ctsConfig.h"
#include "ctsIOPattern.h"


namespace ctsTraffic {

    using namespace ctl;
    using namespace std;


    ctsSocketState::ctsSocketState(ctsSocketBroker* _broker)
    : thread_pool_worker(nullptr),
      state_guard(),
      broker_guard(),
      socket(),
      broker(_broker),
      state(Creating),
      initiated_io(false)
    {
        if (!::InitializeCriticalSectionEx(&state_guard, 4000, 0)) {
            throw ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsSocketState", false);
        }
        if (!::InitializeCriticalSectionEx(&broker_guard, 4000, 0)) {
            auto gle = ::GetLastError();
            ::DeleteCriticalSection(&state_guard);
            throw ctException(gle, L"InitializeCriticalSectionEx", L"ctsSocketState", false);
        }

        thread_pool_worker = ::CreateThreadpoolWork(ThreadPoolWorker, this, ctsConfig::Settings->PTPEnvironment);
        if (nullptr == thread_pool_worker) {
            auto gle = ::GetLastError();
            ::DeleteCriticalSection(&state_guard);
            ::DeleteCriticalSection(&broker_guard);
            throw ctException(gle, L"CreateThreadpoolWork", L"ctsSocketState", false);
        }
    }

    ctsSocketState::~ctsSocketState() throw()
    {
        //
        // In order for a graceful shutdown without risking socket extension:
        //  - shutdown() must be invoked on ctsSocket to close the underlying socket
        //    and wait for all of its TP callback to complete
        // - then all pending ctsSocketState TP callbacks must be canceled
        //    - and must wait for any of those TP callbacks to complete
        // - then can close the TP
        //
        if (socket) {
            this->socket->shutdown();
        }

        ::WaitForThreadpoolWorkCallbacks(thread_pool_worker, TRUE);
        ::CloseThreadpoolWork(thread_pool_worker);

        ::DeleteCriticalSection(&state_guard);
        ::DeleteCriticalSection(&broker_guard);
    }

    void ctsSocketState::start() throw()
    {
        ctl::ctFatalCondition(
            state != Creating,
            L"ctsSocketState::start must only be called once at the initial state of the object (this == %p)", this);
        ::SubmitThreadpoolWork(this->thread_pool_worker);
    }

    void ctsSocketState::complete_state(DWORD _dwerror) throw()
    {
        bool initiating_io = false;
        //
        // must guard the entire switch statement with a state guard
        //
        ::EnterCriticalSection(&this->state_guard);
        if (NO_ERROR == _dwerror) {
            switch (this->state) {
                case Created: {
                    // if no connectFunction specified, go straight to IO
                    if (ctsConfig::Settings->ConnectFunction) {
                        this->state = Connecting;

                    } else {
                        // try to construct the IO Pattern
                        // - if fails, treat this as an error for the entire socket
                        auto pattern_error = this->socket->construct_pattern();
                        if (NO_ERROR == pattern_error) {
                            initiating_io = true;
                            this->state = InitiatingIO;
                            ctsConfig::Settings->ConnectionStatusDetails.active_connection_count.increment();

                        } else {
                            this->state = Closing;
                            this->socket->set_last_error(pattern_error);
                        }
                    }
                    break;
                }

                case Connected: {
                    // try to construct the IO Pattern
                    // - if fails, treat this as an error for the entire socket
                    auto pattern_error = this->socket->construct_pattern();
                    if (NO_ERROR == pattern_error) {
                        initiating_io = true;
                        this->state = InitiatingIO;
                        ctsConfig::Settings->ConnectionStatusDetails.active_connection_count.increment();

                    } else {
                        this->state = Closing;
                        this->socket->set_last_error(pattern_error);
                    }
                    break;
                }

                case InitiatedIO:
                    this->initiated_io = true;
                    this->state = Closing;
                    break;

                default:
                    //    
                    // these are transitory states - complete() should never see these
                    // case Creating:
                    // case Connecting:
                    // case InitiatingIO:
                    // case Closed:
                    //
                    ctl::ctAlwaysFatalCondition(
                        L"ctsSocketState::complete_state - invalid internal state [%d]",
                        this->state);
            }

        } else {
            if (InitiatedIO == this->state) {
                this->initiated_io = true;
            }
            this->state = Closing;
        }
        //
        // release the state lock now that transitions were performed
        //
        ::LeaveCriticalSection(&this->state_guard);
        //
        // updates to ctsSocketBroker must be made outside the state_guard
        //
        if (initiating_io) {
            ::EnterCriticalSection(&this->broker_guard);
            {
                if (this->broker != nullptr) {
                    this->broker->initiating_io();
                }
            }
            ::LeaveCriticalSection(&this->broker_guard);
        }
        //
        // set the last error on the socket with the error given
        // - if the socket already had an error, this will be ignored
        //   but complete_state can be called under low memory conditions before an error was set
        // - otherwise, if not closing, reset the last error for the next state
        //
        if (Closing == this->state) {
            this->socket->set_last_error(_dwerror);
        } else {
            this->socket->reset_last_error();
        }

        // schedule the next functor to run when not closing down the socket
        ::SubmitThreadpoolWork(this->thread_pool_worker);
    }


    bool ctsSocketState::is_closed() const throw()
    {
        ctl::ctAutoReleaseCriticalSection lock_state(&this->state_guard);
        return (Closed == this->state);
    }
    ///
    /// guarded to allow safe detach from the parent broker
    ///
    void ctsSocketState::detach() throw()
    {
        ctl::ctAutoReleaseCriticalSection lock_broker(&this->broker_guard);
        this->broker = nullptr;
    }


    VOID NTAPI ctsSocketState::ThreadPoolWorker(PTP_CALLBACK_INSTANCE, PVOID _context, PTP_WORK) throw()
    {
        //
        // invoke the corresponding function object
        // - these cannot throw: they must complete
        //   if they do throw, it will go unhandled and break in the debugger
        // must track the state change before the functor is invoked
        // - since this could complete inline if it fails, and complete_state
        //   needs to know that we already tried to run the functor for this state
        //
        ctsSocketState* context = reinterpret_cast<ctsSocketState*>(_context);
        switch (context->state) {
            case Creating: {
                unsigned long error = 0;
                try {
                    context->socket = make_shared<ctsSocket>(std::weak_ptr<ctsSocketState>(context->shared_from_this()));
                }
                catch (const ctl::ctException& e) {
                    error = e.why() == 0 ? ERROR_OUTOFMEMORY : e.why();
                }
                catch (const exception&) {
                    error = ERROR_OUTOFMEMORY;
                }

                if (error != 0) {
                    context->complete_state(error);

                } else {
                    ::EnterCriticalSection(&context->state_guard);
                    context->state = Created;
                    ::LeaveCriticalSection(&context->state_guard);

                    ctsConfig::Settings->CreateFunction(weak_ptr<ctsSocket>(context->socket));
                    ctsConfig::PrintDebug(L"\t\tctsSocketState Created\n");
                }
                break;
            }

            case Connecting:
                ::EnterCriticalSection(&context->state_guard);
                context->state = Connected;
                ::LeaveCriticalSection(&context->state_guard);

                ctsConfig::Settings->ConnectFunction(weak_ptr<ctsSocket>(context->socket));
                ctsConfig::PrintDebug(L"\t\tctsSocketState Connected\n");
                break;

            case InitiatingIO:
                ::EnterCriticalSection(&context->state_guard);
                context->state = InitiatedIO;
                ::LeaveCriticalSection(&context->state_guard);

                ctsConfig::Settings->IoFunction(weak_ptr<ctsSocket>(context->socket));
                ctsConfig::PrintDebug(L"\t\tctsSocketState InitiatedIO\n");
                break;

                ///
                /// Processing all closing tasks on a separate threadpool thread
                /// - this guarantees no other locks are taken
                /// - this guarantess ctsSocket won't hold the final reference to the ctsSocketState
                ///   on a threadpool thread - in which case it would deadlock on itself
                ///
            case Closing: {
                ::EnterCriticalSection(&context->broker_guard);
                if (context->broker != nullptr) {
                    context->broker->closing(context->initiated_io);
                }
                ::LeaveCriticalSection(&context->broker_guard);

                if (context->initiated_io) {
                    auto gle = context->socket->get_last_error();

                    // Update the status counter if we previously tracked this connection as active
                    ctsConfig::Settings->ConnectionStatusDetails.active_connection_count.decrement();

                    // Update the historic stats for this connection
                    if (0 == gle) {
                        ctsConfig::Settings->HistoricConnectionDetails.successful_connections.increment();
                        ctsConfig::Settings->ConnectionStatusDetails.successful_completion_count.increment();

                    } else if (ctsIOPatternProtocolError(static_cast<ctsIOPatternStatus>(gle))) {
                        ctsConfig::Settings->HistoricConnectionDetails.protocol_errors.increment();
                        ctsConfig::Settings->ConnectionStatusDetails.protocol_error_count.increment();

                    } else {
                        ctsConfig::Settings->HistoricConnectionDetails.connection_errors.increment();
                        ctsConfig::Settings->ConnectionStatusDetails.connection_error_count.increment();
                    }
                } else {
                    // if this socket never started IO, it never created an io_pattern to track stats
                    // - in this case, directly track the failures in the global stats
                    ctsConfig::Settings->HistoricConnectionDetails.connection_errors.increment();
                    ctsConfig::Settings->ConnectionStatusDetails.connection_error_count.increment();
                }

                // close the socket first to snap the end-time in the stats
                // - then print from the pattern data before destroying the io_pattern
                context->socket->close_socket();
                context->socket->print_pattern_results();

                // update the state last, since ctsBroker looks for this state value
                // - to know when to delete the ctsSocketState instance
                ::EnterCriticalSection(&context->state_guard);
                context->state = Closed;
                ::LeaveCriticalSection(&context->state_guard);

                break;
            }

            default: {
                // the callback should never see any other states
                ctAlwaysFatalCondition(
                    L"ctsSocketState::ThreadPoolWorker - invalid socket state [%d]",
                    context->state);
                ::LeaveCriticalSection(&context->state_guard);
                break;
            }
        }
    }

} // namespace
