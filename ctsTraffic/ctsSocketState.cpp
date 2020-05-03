/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// parent header
#include "ctsSocketState.h"
// cpp headers
#include <exception>
#include <memory>
// os headers
#include <Windows.h>
// ctl headers
#include <ctException.hpp>
// project headers
#include "ctsSocket.h"
#include "ctsSocketBroker.h"
#include "ctsConfig.h"
#include "ctsIOPattern.h"


namespace ctsTraffic
{

    using namespace ctl;
    using namespace std;

    ctsSocketState::ctsSocketState(std::weak_ptr<ctsSocketBroker> _broker) : broker(move(_broker))
    {
        thread_pool_worker.reset(CreateThreadpoolWork(ThreadPoolWorker, this, ctsConfig::Settings->PTPEnvironment));
        THROW_LAST_ERROR_IF_NULL(thread_pool_worker.get());
    }

    ctsSocketState::~ctsSocketState() noexcept
    {
        //
        // In order for a graceful shutdown without risking socket extension:
        //  - shutdown() must be invoked on ctsSocket to close the underlying socket
        //    and wait for all of its TP callback to complete
        // - then all pending ctsSocketState TP callbacks must be canceled
        //    - and must wait for any of those TP callbacks to complete
        // - then can close the TP
        //
        if (socket)
        {
            this->socket->shutdown();
        }
        thread_pool_worker.reset();
    }

    void ctsSocketState::start() noexcept
    {
        FAIL_FAST_IF_MSG(
            state != InternalState::Creating,
            "ctsSocketState::start must only be called once at the initial state of the object (this == %p)", this);
        SubmitThreadpoolWork(this->thread_pool_worker.get());
    }

    void ctsSocketState::complete_state(DWORD _error) noexcept
    {
        //
        // must guard the entire switch statement with a state guard
        //
        auto lock = this->state_guard.lock();
        if (NO_ERROR == _error)
        {
            switch (this->state)
            {
                case InternalState::Created:
                {
                    // if no connectFunction specified, go straight to IO
                    if (ctsConfig::Settings->ConnectFunction)
                    {
                        this->state = InternalState::Connecting;

                    }
                    else
                    {
                        this->state = InternalState::InitiatingIO;
                        ctsConfig::Settings->ConnectionStatusDetails.active_connection_count.increment();
                    }
                    break;
                }

                case InternalState::Connected:
                {
                    this->state = InternalState::InitiatingIO;
                    ctsConfig::Settings->ConnectionStatusDetails.active_connection_count.increment();
                    break;
                }

                case InternalState::InitiatedIO:
                    this->initiated_io = true;
                    this->state = InternalState::Closing;
                    break;

                case InternalState::Closing:
                case InternalState::Closed:
                    // these 2 states should generally not be "completed" by the functor that was invoked
                    // it's possible though, for example if the IO pattern had a functor that went off racing the state machine
                    // deliberately not changing any internal values these since the socket is already being close
                    PrintDebugInfo(
                        L"\t\tctsSocketState::complete_state called while closing (InternalState %u)\n",
                        static_cast<unsigned long>(this->state));
                    break;

                default:
                    //    
                    // these are transitory states - complete() should never see these
                    // case Creating:
                    // case Connecting:
                    // case InitiatingIO:
                    //
                    FAIL_FAST_MSG(
                        "ctsSocketState::complete_state - invalid internal state [%d]",
                        this->state);
            }

        }
        else
        {
            if (InternalState::InitiatedIO == this->state)
            {
                this->initiated_io = true;
            }
            this->last_error = _error;
            this->state = InternalState::Closing;
        }
        //
        // schedule the next functor to run when not closing down the socket
        //
        SubmitThreadpoolWork(this->thread_pool_worker.get());
    }

    ctsSocketState::InternalState ctsSocketState::current_state() const noexcept
    {
        const auto lock = this->state_guard.lock();
        return this->state;
    }

    VOID NTAPI ctsSocketState::ThreadPoolWorker(PTP_CALLBACK_INSTANCE, PVOID _context, PTP_WORK) noexcept
    {
        //
        // invoke the corresponding function object
        // - these cannot throw: they must complete
        //   if they do throw, it will go unhandled and break in the debugger
        // must track the state change before the functor is invoked
        // - since this could complete inline if it fails, and complete_state
        //   needs to know that we already tried to run the functor for this state
        //
        auto this_ptr = static_cast<ctsSocketState*>(_context);
        switch (this_ptr->state)
        {
            case InternalState::Creating:
            {
                unsigned long error = 0;
                try { this_ptr->socket = make_shared<ctsSocket>(this_ptr->shared_from_this()); }
                catch (const exception& e) { error = ctErrorCode(e); }

                if (error != 0)
                {
                    this_ptr->complete_state(error);

                }
                else
                {
                    auto lock = this_ptr->state_guard.lock();
                    this_ptr->state = InternalState::Created;
                    lock.reset();

                    ctsConfig::Settings->CreateFunction(this_ptr->socket);
                    PrintDebugInfo(L"\t\tctsSocketState Created\n");
                }
                break;
            }

            case InternalState::Connecting:
            {
                auto lock = this_ptr->state_guard.lock();
                this_ptr->state = InternalState::Connected;
                lock.reset();

                ctsConfig::Settings->ConnectFunction(this_ptr->socket);
                PrintDebugInfo(L"\t\tctsSocketState Connected\n");
                break;
            }

            case InternalState::InitiatingIO:
            {
                // notify the broker when initiating IO
                auto parent = this_ptr->broker.lock();
                if (parent)
                {
                    parent->initiating_io();
                }

                unsigned long error = 0;
                try { this_ptr->socket->set_io_pattern(ctsIOPattern::MakeIOPattern()); }
                catch (const exception& e) { error = ctErrorCode(e); }

                if (error != 0)
                {
                    this_ptr->complete_state(error);

                }
                else
                {
                    auto lock = this_ptr->state_guard.lock();
                    this_ptr->state = InternalState::InitiatedIO;
                    lock.reset();

                    ctsConfig::Settings->IoFunction(this_ptr->socket);
                    PrintDebugInfo(L"\t\tctsSocketState InitiatedIO\n");
                }
                break;
            }

            // Processing all closing tasks on a separate threadpool thread
            // - this guarantees no other locks are taken
            // - this guarantess ctsSocket won't hold the final reference to the ctsSocketState
            //   on a threadpool thread - in which case it would deadlock on itself
            case InternalState::Closing:
            {
                if (this_ptr->initiated_io)
                {
                    // Update the status counter if we previously tracked this connection as active
                    ctsConfig::Settings->ConnectionStatusDetails.active_connection_count.decrement();

                    // Update the historic stats for this connection
                    if (0 == this_ptr->last_error)
                    {
                        ctsConfig::Settings->ConnectionStatusDetails.successful_completion_count.increment();

                    }
                    else if (ctsIOPattern::IsProtocolError(this_ptr->last_error))
                    {
                        ctsConfig::Settings->ConnectionStatusDetails.protocol_error_count.increment();

                    }
                    else
                    {
                        ctsConfig::Settings->ConnectionStatusDetails.connection_error_count.increment();
                    }
                }
                else
                {
                    // if this socket never started IO, it never created an io_pattern to track stats
                    // - in this case, directly track the failures in the global stats
                    ctsConfig::Settings->ConnectionStatusDetails.connection_error_count.increment();
                }

                this_ptr->socket->close_socket(this_ptr->last_error);
                this_ptr->socket->print_pattern_results(this_ptr->last_error);

                if (ctsConfig::Settings->ClosingFunction)
                {
                    ctsConfig::Settings->ClosingFunction(this_ptr->socket);
                }

                // update the state last, since ctsBroker looks for this state value
                // - to know when to delete the ctsSocketState instance
                auto lock = this_ptr->state_guard.lock();
                this_ptr->state = InternalState::Closed;
                lock.reset();

                auto parent = this_ptr->broker.lock();
                if (parent)
                {
                    parent->closing(this_ptr->initiated_io);
                }

                PrintDebugInfo(L"\t\tctsSocketState Closed\n");
                break;
            }

            default:
            {
                // the callback should never see any other states
                FAIL_FAST_MSG(
                    "ctsSocketState::ThreadPoolWorker - invalid socket state [%d]",
                    this_ptr->state);
            }
        }
    }

} // namespace
