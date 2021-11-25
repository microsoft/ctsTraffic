/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <sdkddkver.h>
#include "CppUnitTest.h"

// cpp headers
#include <vector>
#include <memory>
// os headers
#include <Windows.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
// ctl headers
#include <ctString.hpp>
// project headers
#include "ctsSocketBroker.h"
#include "ctsSocketState.h"
#include "ctsConfig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{

    template<> inline std::wstring ToString<ctsTraffic::ctsSocketState::InternalState>(const ctsTraffic::ctsSocketState::InternalState& state)
    {
        switch (state)
        {
        case ctsTraffic::ctsSocketState::InternalState::Creating: return L"Creating";
        case ctsTraffic::ctsSocketState::InternalState::Created: return L"Created";
        case ctsTraffic::ctsSocketState::InternalState::Connecting: return L"Connecting";
        case ctsTraffic::ctsSocketState::InternalState::Connected: return L"Connected";
        case ctsTraffic::ctsSocketState::InternalState::InitiatingIo: return L"InitiatingIO";
        case ctsTraffic::ctsSocketState::InternalState::InitiatedIo: return L"InitiatedIO";
        case ctsTraffic::ctsSocketState::InternalState::Closing: return L"Closing";
        case ctsTraffic::ctsSocketState::InternalState::Closed: return L"Closed";
        }
        return wil::str_printf<std::wstring>(L"Unknown State (0x%x)", state);
    }
}


///
/// Fakes
///
namespace ctsTraffic::ctsConfig
{
    ctsConfigSettings* g_configSettings;

    void PrintDebug(PCWSTR text, ...) noexcept
    {
        va_list args;
        va_start(args, text);
        std::wstring outputString;
        wil::details::str_vprintf_nothrow<std::wstring>(outputString, text, args);
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"PrintDebug: %ws\n", outputString.c_str()).c_str());

        va_end(args);
    }
    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t) noexcept
    {
        Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(error)\n");
    }
    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsTcpStatistics&) noexcept
    {
        Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsTcpStatistics)\n");
    }
    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsUdpStatistics&) noexcept
    {
        Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsUdpStatistics)\n");
    }
    bool ShutdownCalled() noexcept
    {
        return false;
    }
    uint32_t ConsoleVerbosity() noexcept
    {
        return 0;
    }
}

///
/// End of Fakes
///


using namespace ctsTraffic;

///
/// Broker contains logic processing these ctsConfig global settings
/// to control the quotas involved in initiating new socket connections
///

// ctsConfig::Settings::AcceptFunction
// ctsConfig::Settings::ServerExitLimit
// ctsConfig::Settings::Iterations
// ctsConfig::Settings::ConnectionLimit
// ctsConfig::Settings::ConnectionThrottleLimit
// ctsConfig::Settings::CtrlCHandle


/// class used to communicate between the test and the created ctsSocketState objects
class SocketStatePool
{
public:
    SocketStatePool() = default;
    ~SocketStatePool() noexcept = default;

    /// Add/remove ctsSocketState objects
    void add_object(const std::shared_ptr<ctsSocketState>& state_object)
    {
        m_work.QueueAdd(state_object);
    }
    void remove_deleted_objects() noexcept
    {
        const auto hold_lock = m_lock.lock();

        std::erase_if(m_stateObjects, [&](const std::weak_ptr<ctsSocketState>& weak_ptr) { return weak_ptr.expired(); });
    }
    void reset() noexcept
    {
        const auto hold_lock = m_lock.lock();

        m_stateObjects.clear();
    }

    /// Interact with states of contained ctsSocketState objects
    void complete_state(DWORD error_code)
    {
        const auto hold_lock = m_lock.lock();

        for (auto& socket_state : m_stateObjects)
        {
            auto shared_state(socket_state.lock());
            Assert::IsNotNull(shared_state.get());
            if (shared_state)
            {
                shared_state->CompleteState(error_code);
            }
        }
    }
    void validate_expected_count(size_t count)
    {
        const auto hold_lock = m_lock.lock();

        Assert::AreEqual(count, m_stateObjects.size());
    }
    void validate_expected_count(size_t count, ctsSocketState::InternalState state)
    {
        const auto hold_lock = m_lock.lock();

        size_t matched_state = 0;
        for (auto& socket_state : m_stateObjects)
        {
            auto shared_state(socket_state.lock());
            Assert::IsNotNull(shared_state.get());
            if (shared_state)
            {
                if (shared_state->GetCurrentState() == state)
                {
                    ++matched_state;
                }
            }
        }

        Assert::AreEqual(count, matched_state);
    }

    void wait_for_start(size_t count)
    {
        bool matched = false;
        for (auto i = 0; i < 5000; ++i)
        {
            // wait outside the lock
            Sleep(25);
            const auto hold_lock = m_lock.lock();

            size_t matched_state = 0;
            for (auto& socket_state : m_stateObjects)
            {
                auto shared_state(socket_state.lock());
                Assert::IsNotNull(shared_state.get());
                if (shared_state->GetCurrentState() == ctsSocketState::InternalState::Creating)
                {
                    ++matched_state;
                }
            }

            if (count == matched_state)
            {
                matched = true;
                break;
            }

            Assert::IsTrue(matched_state < count);
        }

        Assert::IsTrue(matched);
    }

    // non-copyable
    SocketStatePool(const SocketStatePool&) = delete;
    SocketStatePool& operator=(const SocketStatePool&) = delete;

private:
    wil::critical_section m_lock{ ctsConfig::g_configSettings->c_CriticalSectionSpinlock };
    std::vector<std::weak_ptr<ctsSocketState>> m_stateObjects;

    struct AsyncAddObject
    {
        SocketStatePool& m_parent;
        wil::critical_section m_workLock{ ctsConfig::g_configSettings->c_CriticalSectionSpinlock };
        std::vector<std::shared_ptr<ctsSocketState>> m_stateObjectsToAdd;
        wil::shared_threadpool_work m_tpWork;

        AsyncAddObject(SocketStatePool& parent) : m_parent(parent)
        {
            m_tpWork.reset(CreateThreadpoolWork(WorkCallback, this, nullptr));
        }

        void QueueAdd(const std::shared_ptr<ctsSocketState>& new_object)
        {
            const auto lock = m_workLock.lock();
            m_stateObjectsToAdd.push_back(new_object);
            SubmitThreadpoolWork(m_tpWork.get());
        }

        static VOID CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK)
        {
            auto* pThis = static_cast<AsyncAddObject*>(context);

            std::shared_ptr<ctsSocketState> objectToAdd;
            {
                const auto hold_lock = pThis->m_workLock.lock();
                if (!pThis->m_stateObjectsToAdd.empty())
                {
                    objectToAdd = *pThis->m_stateObjectsToAdd.rbegin();
                    pThis->m_stateObjectsToAdd.pop_back();
                }
            }

            const auto parentLock = pThis->m_parent.m_lock.lock();
            pThis->m_parent.m_stateObjects.push_back(objectToAdd);
        }
    } m_work{ *this };
};

SocketStatePool* g_socketPool;


///
/// Faking the ctsSocketState that the broker instantiates based off of quota
/// - don't need to actually do any work - just need to control indications back to the broker
/// - but we do need to track all instances created so we can control each socketstate
///
ctsSocketState::ctsSocketState(std::weak_ptr<ctsSocketBroker> broker) :
    m_broker(std::move(broker))
{
}

ctsSocketState::~ctsSocketState() noexcept
{
    g_socketPool->remove_deleted_objects();
}

void ctsSocketState::Start() noexcept
{
    g_socketPool->add_object(this->shared_from_this());
}

void ctsSocketState::CompleteState(DWORD error_code) noexcept
{
    if (NO_ERROR == error_code)
    {
        // walk states from creating -> InitiatingIO -> Closed
        switch (m_state)
        {

            // Skipping Connecting, since that state doesn't affect ctsSocketBroker

        case InternalState::Creating:
        {
            const auto parent = m_broker.lock();
            parent->InitiatingIo();
            m_state = InternalState::InitiatingIo;
            break;
        }
        case InternalState::InitiatingIo:
        {
            const auto parent = m_broker.lock();
            parent->Closing(true);
            m_state = InternalState::Closed;
            break;
        }

        default:
            Assert::Fail(
                wil::str_printf<std::wstring>(L"Unexpected ctsSocketState: 0x%x\n", m_state).c_str());
        }
    }
    else
    {
        // move straight to Closed
        const auto parent = m_broker.lock();
        parent->Closing(InternalState::InitiatingIo == m_state);
        m_state = InternalState::Closed;
    }
}

ctsSocketState::InternalState ctsSocketState::GetCurrentState() const noexcept
{
    return m_state;
}


namespace ctsUnitTest
{
    TEST_CLASS(ctsSocketBrokerUnitTest)
    {
    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            g_socketPool = new SocketStatePool;

            ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;
            ctsConfig::g_configSettings->CtrlCHandle = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
            ctsConfig::g_configSettings->PrePostRecvs = 1;
            ctsConfig::g_configSettings->PrePostSends = 1;
            Assert::AreNotEqual(static_cast<HANDLE>(nullptr), ctsConfig::g_configSettings->CtrlCHandle);
        }
        TEST_CLASS_CLEANUP(Cleanup)
        {
            delete ctsConfig::g_configSettings;
            delete g_socketPool;
        }

        TEST_METHOD_INITIALIZE(MethodSetup)
        {
            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
        }
        TEST_METHOD_CLEANUP(MethodCleanup)
        {
            // drain the Timer
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2);
        }

        TEST_METHOD(OneSuccessfulClientConnection)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->ConnectionLimit = 1;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 1;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(1);

            Logger::WriteMessage(L"Starting IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Closing sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManySuccessfulClientConnection)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->ConnectionLimit = 100;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 100;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(100);

            Logger::WriteMessage(L"Starting IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Closing sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneSuccessfulServerConnectionWithExit)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = 1;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->AcceptLimit = 1;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(1);

            Logger::WriteMessage(L"Starting IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Closing sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManySuccessfulServerConnectionWithExit)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = 100;
            ctsConfig::g_configSettings->Iterations = 100;
            ctsConfig::g_configSettings->AcceptLimit = 100;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(100);

            Logger::WriteMessage(L"Starting IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Closing sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneSuccessfulServerConnectionWithoutExit)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = MAXULONGLONG;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->AcceptLimit = 1;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(1);

            Logger::WriteMessage(L"Starting IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Closing sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsFalse(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(1);
        }
        TEST_METHOD(ManySuccessfulServerConnectionWithoutExit)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = MAXULONGLONG;
            ctsConfig::g_configSettings->Iterations = 100;
            ctsConfig::g_configSettings->AcceptLimit = 100;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(100);

            Logger::WriteMessage(L"Starting IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Closing sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsFalse(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            // should create the next socket to accept on the next Timer callback
            g_socketPool->validate_expected_count(100);
        }

        TEST_METHOD(OneFailedClientConnection_FailedConnect)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->ConnectionLimit = 1;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 1;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(1);

            Logger::WriteMessage(L"Connecting sockets");
            g_socketPool->complete_state(WSAECONNREFUSED);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManyFailedClientConnection_FailedConnect)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->ConnectionLimit = 100;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 100;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(100);

            Logger::WriteMessage(L"Connecting sockets");
            g_socketPool->complete_state(WSAECONNREFUSED);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneFailedServerConnectionWithExit)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = 1;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->AcceptLimit = 1;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(1);

            Logger::WriteMessage(L"Connecting sockets");
            g_socketPool->complete_state(WSAECONNREFUSED);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManyFailedServerConnectionWithExit)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = 100;
            ctsConfig::g_configSettings->Iterations = 100;
            ctsConfig::g_configSettings->AcceptLimit = 100;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(100);

            Logger::WriteMessage(L"Connecting sockets");
            g_socketPool->complete_state(WSAECONNREFUSED);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneFailedClientConnection_FailedIO)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->ConnectionLimit = 1;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 1;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(1);

            Logger::WriteMessage(L"Starting IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Failing IO on sockets");
            g_socketPool->complete_state(WSAENOBUFS);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManyFailedClientConnection_FailedIO)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->ConnectionLimit = 100;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 100;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(100);

            Logger::WriteMessage(L"Starting IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Failing IO on sockets");
            g_socketPool->complete_state(WSAENOBUFS);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneFailedServerConnectionWithExit_FailedIO)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = 1;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->AcceptLimit = 1;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(1);

            Logger::WriteMessage(L"Initiating IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Failing IO on sockets");
            g_socketPool->complete_state(WSAENOBUFS);
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManyFailedServerConnectionWithExit_FailedIO)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = 100;
            ctsConfig::g_configSettings->Iterations = 100;
            ctsConfig::g_configSettings->AcceptLimit = 100;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::m_timerCallbackTimeoutMs = 100;
            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(100);

            Logger::WriteMessage(L"Initiating IO on sockets");
            g_socketPool->complete_state(NO_ERROR);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"Failing IO on sockets");
            g_socketPool->complete_state(WSAENOBUFS);
            g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(MoreSuccessfulClientConnectionsThanConnectionThrottleLimit)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->ConnectionLimit = 15;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 5;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(5);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 5 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"3. Expecting 5 creating, 5 initiating IO, 5 completed\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"4. Expecting 5 initiating IO, 10 completed\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"6. Expecting 15 completed\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(MoreFailedClientConnectionsThanConnectionThrottleLimit_FailedConnect)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->ConnectionLimit = 15;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 5;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(5);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 waiting, 5 closed\n");
            g_socketPool->complete_state(WSAECONNREFUSED); // fail connect
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"3. Expecting 5 creating, 10 closed\n");
            g_socketPool->complete_state(WSAECONNREFUSED); // fail connect
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"4. Expecting 15 closed\n");
            g_socketPool->complete_state(WSAECONNREFUSED); // fail connect
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(MoreFailedClientConnectionsThanConnectionThrottleLimit_FailedIO)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 1;
            ctsConfig::g_configSettings->ConnectionLimit = 15;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 5;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(5);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 5 waiting\n");
            g_socketPool->complete_state(NO_ERROR); // successful connect
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"3. Expecting 5 creating, 10 closed\n");
            g_socketPool->complete_state(WSAECONNREFUSED); // fail connect
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"4. Expecting 15 closed\n");
            g_socketPool->complete_state(WSAECONNREFUSED); // fail connect
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(MoreSuccessfulServerConnectionsThanAcceptLimit)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = 15;
            ctsConfig::g_configSettings->Iterations = 15;
            ctsConfig::g_configSettings->AcceptLimit = 5;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(5);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 5 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"3. Expecting 5 creating, 5 initiating IO, 5 completed\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"4. Expecting 5 initiating IO, 10 completed\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"6. Expecting 15 completed\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            g_socketPool->validate_expected_count(0);
        }

        TEST_METHOD(ServerExitLimitShouldOverrideIterations)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::g_configSettings->ServerExitLimit = 1;
            ctsConfig::g_configSettings->Iterations = 15;
            ctsConfig::g_configSettings->AcceptLimit = 5;
            // these are not applicable to server
            ctsConfig::g_configSettings->ConnectionLimit = 0;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();

            Logger::WriteMessage(L"1. Expecting 1 creating\n");
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(1);

            Logger::WriteMessage(L"2. Expecting 1 initiating IO\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"3. Expecting 1 completed\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs);
            g_socketPool->validate_expected_count(0);
        }


        TEST_METHOD(ManySuccessfulClientConnectionsMixingIterationsAndConnections)
        {
            g_socketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::g_configSettings->AcceptFunction = nullptr;
            ctsConfig::g_configSettings->Iterations = 10;
            ctsConfig::g_configSettings->ConnectionLimit = 10;
            ctsConfig::g_configSettings->ConnectionThrottleLimit = 5;
            // these are not applicable to client
            ctsConfig::g_configSettings->ServerExitLimit = 0;
            ctsConfig::g_configSettings->AcceptLimit = 0;

            const std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->Start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 95 waiting\n");
            // wait for all to be started as this is async
            g_socketPool->wait_for_start(5);
            g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 90 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"3. Expecting 5 creating, 5 initiating IO, 85 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"4. Expecting 5 creating, 5 initiating IO, 80 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"5. Failing all sockets: 5 creating, 75 waiting\n");
            g_socketPool->complete_state(WSAENOBUFS);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"6. Expecting 5 creating, 5 initiating IO, 70 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"7. Expecting 5 creating, 5 initiating IO, 65 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"8. Failing all sockets: 5 creating, 60 waiting\n");
            g_socketPool->complete_state(WSAENOBUFS);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"9. Expecting 10 creating, 10 initiating IO, 55 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"10. Expecting 10 creating, 10 initiating IO, 50 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"11. Expecting 10 creating, 10 initiating IO, 45 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"12. Failing all sockets: 5 creating, 40 waiting\n");
            g_socketPool->complete_state(WSAENOBUFS);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"13. Expecting 10 creating, 10 initiating IO, 35 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"14. Expecting 10 creating, 10 initiating IO, 30 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"15. Expecting 10 creating, 10 initiating IO, 25 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"16. Expecting 10 creating, 10 initiating IO, 20 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"17. Failing all sockets: 5 creating, 15 waiting\n");
            g_socketPool->complete_state(WSAENOBUFS);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"18. Expecting 10 creating, 10 initiating IO, 10 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"19. Expecting 10 creating, 10 initiating IO, 5 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"20. Expecting 5 creating, 5 initiating IO, 0 waiting\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"21. Expecting 5 initiating IO\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

            Logger::WriteMessage(L"22. Expecting all done\n");
            g_socketPool->complete_state(NO_ERROR);
            Sleep(ctsSocketBroker::m_timerCallbackTimeoutMs * 2); // allowing the timer to coalesce
            g_socketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->Wait(ctsSocketBroker::m_timerCallbackTimeoutMs * 2));
            // let the timer fire
            g_socketPool->validate_expected_count(0);
        }
    };
}