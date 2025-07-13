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
#include <memory>
// os headers
#include <Windows.h>
// project headers
#include "ctsSocket.h"
#include "ctsSocketBroker.h"
#include "ctsConfig.h"
#include "ctsIOPattern.h"


namespace ctsTraffic
{
ctsSocketState::ctsSocketState(std::weak_ptr<ctsSocketBroker> pBroker) :
    m_broker(std::move(pBroker))
{
    m_threadPoolWorker.reset(CreateThreadpoolWork(ThreadPoolWorker, this, ctsConfig::g_configSettings->pTpEnvironment));
    THROW_LAST_ERROR_IF_NULL(m_threadPoolWorker.get());
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
    if (m_socket)
    {
        m_socket->Shutdown();
    }
    m_threadPoolWorker.reset();
}

void ctsSocketState::Start() noexcept
{
    FAIL_FAST_IF_MSG(
        m_state != InternalState::Creating,
        "ctsSocketState::start must only be called once at the initial state of the object (this == %p)", this);
    SubmitThreadpoolWork(m_threadPoolWorker.get());
}

void ctsSocketState::CompleteState(DWORD error) noexcept
{
    //
    // must guard the entire switch statement with a state guard
    //
    auto lock = m_stateGuard.lock();
    if (NO_ERROR == error)
    {
        switch (m_state)
        {
            case InternalState::Created:
            {
                // if no connectFunction specified, go straight to IO
                if (ctsConfig::g_configSettings->ConnectFunction)
                {
                    m_state = InternalState::Connecting;
                }
                else
                {
                    m_state = InternalState::InitiatingIo;
                    ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.Increment();
                }
                break;
            }

            case InternalState::Connected:
            {
                m_state = InternalState::InitiatingIo;
                ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.Increment();
                break;
            }

            case InternalState::InitiatedIo:
                m_initiatedIo = true;
                m_state = InternalState::Closing;
                break;

            case InternalState::Closing:
            case InternalState::Closed:
                // these 2 states should generally not be "completed" by the functor that was invoked
                // it's possible though, for example if the IO pattern had a functor that went off racing the state machine
                // deliberately not changing any internal values these since the socket is already being close
                PRINT_DEBUG_INFO(
                    L"\t\tctsSocketState::complete_state called while closing (InternalState %d)\n", m_state);
                break;

            default:
                //    
                // these are transitory states - complete() should never see these
                // case Creating:
                // case Connecting:
                // case InitiatingIO:
                //
                FAIL_FAST_MSG(
                    "ctsSocketState::complete_state - invalid internal state [%d]", m_state);
        }
    }
    else
    {
        m_lastError = error;
        if (m_state > InternalState::Connected)
        {
            m_initiatedIo = true;
        }
        m_state = InternalState::Closing;
    }

    SubmitThreadpoolWork(m_threadPoolWorker.get());
}

ctsSocketState::InternalState ctsSocketState::GetCurrentState() const noexcept
{
    const auto lock = m_stateGuard.lock();
    return m_state;
}

VOID NTAPI ctsSocketState::ThreadPoolWorker(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK) noexcept
{
    //
    // invoke the corresponding function object
    // - these cannot throw: they must complete
    //   if they do throw, it will go unhandled and break in the debugger
    // must track the state change before the functor is invoked
    // - since this could complete inline if it fails, and complete_state
    //   needs to know that we already tried to run the functor for this state
    //
    auto* thisPtr = static_cast<ctsSocketState*>(context);
    switch (thisPtr->m_state)
    {
        case InternalState::Creating:
        {
            try
            {
                thisPtr->m_socket = std::make_shared<ctsSocket>(thisPtr->shared_from_this());

                auto lock = thisPtr->m_stateGuard.lock();
                thisPtr->m_state = InternalState::Created;
                lock.reset();

                ctsConfig::g_configSettings->CreateFunction(thisPtr->m_socket);
                PRINT_DEBUG_INFO(L"\t\tctsSocketState Created\n");
            }
            catch (...)
            {
                thisPtr->CompleteState(wil::ResultFromCaughtException());
            }
            break;
        }

        case InternalState::Connecting:
        {
            auto lock = thisPtr->m_stateGuard.lock();
            thisPtr->m_state = InternalState::Connected;
            lock.reset();

            ctsConfig::g_configSettings->ConnectFunction(thisPtr->m_socket);
            PRINT_DEBUG_INFO(L"\t\tctsSocketState Connected\n");
            break;
        }

        case InternalState::InitiatingIo:
        {
            // notify the broker when initiating IO
            if (const auto parent = thisPtr->m_broker.lock())
            {
                parent->InitiatingIo();
            }

            try
            {
                thisPtr->m_socket->SetIoPattern();

                auto lock = thisPtr->m_stateGuard.lock();
                thisPtr->m_state = InternalState::InitiatedIo;
                lock.reset();

                ctsConfig::g_configSettings->IoFunction(thisPtr->m_socket);
                PRINT_DEBUG_INFO(L"\t\tctsSocketState InitiatedIO\n");
            }
            catch (...)
            {
                PRINT_DEBUG_INFO(L"\t\tctsSocketState InitiatingIo failed\n");
                thisPtr->CompleteState(wil::ResultFromCaughtException());
            }
            break;
        }

        // Processing all closing tasks on a separate threadpool thread
        // - this guarantees no other locks are taken
        // - this guarantess ctsSocket won't hold the final reference to the ctsSocketState
        //   on a threadpool thread - in which case it would deadlock on itself
        case InternalState::Closing:
        {
            if (thisPtr->m_initiatedIo)
            {
                // Update the status counter if we previously tracked this connection as active
                ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.Decrement();

                // Update the historic stats for this connection
                if (0 == thisPtr->m_lastError)
                {
                    ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.Increment();
                }
                else if (ctsIoPattern::IsProtocolError(thisPtr->m_lastError))
                {
                    ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.Increment();
                }
                else
                {
                    ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.Increment();
                }
            }
            else
            {
                // if this socket never started IO, it never created an io_pattern to track stats
                // - in this case, directly track the failures in the global stats
                ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.Increment();
            }

            if (thisPtr->m_socket)
            {
                thisPtr->m_socket->CloseSocket(thisPtr->m_lastError);
                thisPtr->m_socket->PrintPatternResults(thisPtr->m_lastError);

                if (ctsConfig::g_configSettings->ClosingFunction)
                {
                    ctsConfig::g_configSettings->ClosingFunction(thisPtr->m_socket);
                }
            }

            // update the state last, since ctsBroker looks for this state value
            // - to know when to delete the ctsSocketState instance
            auto lock = thisPtr->m_stateGuard.lock();
            thisPtr->m_state = InternalState::Closed;
            lock.reset();

            if (const auto parent = thisPtr->m_broker.lock())
            {
                parent->Closing(thisPtr->m_initiatedIo);
            }

            PRINT_DEBUG_INFO(L"\t\tctsSocketState Closed\n");
            break;
        }

        default:
        {
            // the callback should never see any other states
            FAIL_FAST_MSG(
                "ctsSocketState::ThreadPoolWorker - invalid socket state [%d]",
                thisPtr->m_state);
        }
    }
}
} // namespace
