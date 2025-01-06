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
// project headers
#include <algorithm>

#include "ctsSocketBroker.h"
#include "ctsSocketState.h"
#include "ctsConfig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

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

unsigned long PrintThrownException() noexcept
{
    Logger::WriteMessage(L"ctsConfig::PrintThrownException\n");
    return 0;
}

void PrintConnectionResults(const wil::networking::socket_address&, const wil::networking::socket_address&, uint32_t) noexcept
{
    Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(error)\n");
}

void PrintConnectionResults(const wil::networking::socket_address&, const wil::networking::socket_address&, uint32_t, const ctsTcpStatistics&) noexcept
{
    Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsTcpStatistics)\n");
}

void PrintConnectionResults(const wil::networking::socket_address&, const wil::networking::socket_address&, uint32_t, const ctsUdpStatistics&) noexcept
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
    void add_object(const std::shared_ptr<ctsSocketState>& stateObject)
    {
        m_work.QueueAdd(stateObject);
    }

    void remove_deleted_objects() noexcept
    {
        const auto holdLock = m_lock.lock();

        std::erase_if(m_stateObjects, [&](const std::weak_ptr<ctsSocketState>& weakPtr) {
            return weakPtr.expired();
        });
    }

    void print_objects() noexcept
    {
        const auto holdLock = m_lock.lock();

        const auto creatingObjects = std::ranges::count_if(m_stateObjects, [](const auto& object) {
            if (const auto sharedState = object.lock(); sharedState)
            {
                return sharedState->GetCurrentState() == ctsSocketState::InternalState::Creating;
            }
            return false;
        });
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"\tSocketStatePool Creating objects : %ld\n", creatingObjects).c_str());

        const auto initiatingIoObjects = std::ranges::count_if(m_stateObjects, [](const auto& object) {
            if (const auto sharedState = object.lock(); sharedState)
            {
                return sharedState->GetCurrentState() == ctsSocketState::InternalState::InitiatingIo;
            }
            return false;
        });
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"\tSocketStatePool InitiatingIo objects : %ld\n", initiatingIoObjects).c_str());

        const auto closedObjects = std::ranges::count_if(m_stateObjects, [](const auto& object) {
            if (const auto sharedState = object.lock(); sharedState)
            {
                return sharedState->GetCurrentState() == ctsSocketState::InternalState::Closed;
            }
            return false;
        });
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"\tSocketStatePool Closed objects : %ld\n", closedObjects).c_str());

        const auto nullObjects = std::ranges::count_if(m_stateObjects, [](const auto& object) {
            return object.expired();
        });
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"\tSocketStatePool null objects : %ld\n", nullObjects).c_str());
    }

    void reset() noexcept
    {
        const auto holdLock = m_lock.lock();
        m_stateObjects.clear();
    }

    /// Interact with states of contained ctsSocketState objects
    void complete_state(DWORD errorCode)
    {
        const auto holdLock = m_lock.lock();
        for (auto& socketState : m_stateObjects)
        {
            if (const auto sharedState = socketState.lock())
            {
                sharedState->CompleteState(errorCode);
            }
        }
    }

    void validate_expected_count(size_t count)
    {
        size_t matchedState = 0;

        for (auto i = 0; i < 250; ++i)
        {
            // wait outside the lock
            Sleep(25);
            const auto holdLock = m_lock.lock();

            matchedState = m_stateObjects.size();
            if (count == matchedState)
            {
                break;
            }

            if (matchedState > count)
            {
                print_objects();
            }
            Assert::IsTrue(matchedState < count);
        }

        if (count != m_stateObjects.size())
        {
            print_objects();
        }
        Assert::AreEqual(count, m_stateObjects.size());
    }

    void validate_expected_count(size_t count, ctsSocketState::InternalState state)
    {
        size_t matchedState = 0;

        for (auto i = 0; i < 250; ++i)
        {
            // wait outside the lock
            Sleep(25);
            const auto holdLock = m_lock.lock();

            matchedState = 0;
            for (auto& socketState : m_stateObjects)
            {
                if (const auto sharedState = socketState.lock())
                {
                    if (state == sharedState->GetCurrentState())
                    {
                        ++matchedState;
                    }
                }
                    else
                    {
                        if (state == ctsSocketState::InternalState::Closed)
                        {
                            // closed objects get removed in the threadpool thread - treat them equivalent
                            ++matchedState;
                        }
                    }
            }

            if (count == matchedState)
            {
                break;
            }

            if (matchedState > count)
            {
                print_objects();
            }
            Assert::IsTrue(matchedState < count);
        }

        if (count != matchedState)
        {
            print_objects();
        }
        Assert::AreEqual(count, matchedState);
    }

    void wait_for_start(size_t count)
    {
        size_t matchedState = 0;
        for (auto i = 0; i < 250; ++i)
        {
            // wait outside the lock
            Sleep(25);
            const auto holdLock = m_lock.lock();

            matchedState = 0;
            for (auto& socketState : m_stateObjects)
            {
                auto sharedState(socketState.lock());
                Assert::IsNotNull(sharedState.get());
                if (sharedState->GetCurrentState() == ctsSocketState::InternalState::Creating)
                {
                    ++matchedState;
                }
            }

            if (count == matchedState)
            {
                break;
            }

            if (matchedState > count)
            {
                print_objects();
            }
            Assert::IsTrue(matchedState < count);
        }

        if (count != matchedState)
        {
            print_objects();
        }
        Assert::AreEqual(count, matchedState);
    }

    // non-copyable
    SocketStatePool(const SocketStatePool&) = delete;
    SocketStatePool& operator=(const SocketStatePool&) = delete;
    SocketStatePool(SocketStatePool&&) = delete;
    SocketStatePool& operator=(SocketStatePool&&) = delete;

private:
    wil::critical_section m_lock{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
    std::vector<std::weak_ptr<ctsSocketState>> m_stateObjects;

    struct AsyncAddObject
    {
        SocketStatePool& m_parent;
        wil::critical_section m_workLock{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
        std::vector<std::shared_ptr<ctsSocketState>> m_stateObjectsToAdd;
        wil::shared_threadpool_work m_tpWork;

        explicit AsyncAddObject(SocketStatePool& parent) :
            m_parent(parent)
        {
            m_tpWork.reset(CreateThreadpoolWork(WorkCallback, this, nullptr));
        }

        void QueueAdd(const std::shared_ptr<ctsSocketState>& newObject)
        {
            const auto lock = m_workLock.lock();
            m_stateObjectsToAdd.push_back(newObject);
            SubmitThreadpoolWork(m_tpWork.get());
        }

        static VOID CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK) noexcept
        {
            auto* pThis = static_cast<AsyncAddObject*>(context);

            std::shared_ptr<ctsSocketState> objectToAdd;
            {
                const auto holdLock = pThis->m_workLock.lock();
                if (!pThis->m_stateObjectsToAdd.empty())
                {
                    objectToAdd = *pThis->m_stateObjectsToAdd.rbegin();
                    pThis->m_stateObjectsToAdd.pop_back();
                }
            }

            const auto parentLock = pThis->m_parent.m_lock.lock();
            pThis->m_parent.m_stateObjects.push_back(objectToAdd);
        }
    } m_work{*this};
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
}

void ctsSocketState::Start() noexcept
{
    g_socketPool->add_object(this->shared_from_this());
}

void ctsSocketState::CompleteState(DWORD error) noexcept
{
    if (NO_ERROR == error)
    {
        // walk states from Creating -> InitiatingIO -> Closed
        switch (m_state)
        {
            // Skipping Connecting, since that state doesn't affect ctsSocketBroker

            case InternalState::Creating:
            {
                m_state = InternalState::InitiatingIo;
                const auto parent = m_broker.lock();
                parent->InitiatingIo();
                break;
            }
            case InternalState::InitiatingIo:
            {
                m_state = InternalState::Closed;
                const auto parent = m_broker.lock();
                parent->Closing(true);
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
        const auto wasActive = InternalState::InitiatingIo == m_state;
        m_state = InternalState::Closed;

        const auto parent = m_broker.lock();
        parent->Closing(wasActive);
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
    }

    TEST_METHOD_CLEANUP(MethodCleanup)
    {
        // drain the Timer
        Sleep(250);
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


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(1);

        Logger::WriteMessage(L"Starting IO on sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"Closing sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
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


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(100);

        Logger::WriteMessage(L"Starting IO on sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

        g_socketPool->print_objects();

        Logger::WriteMessage(L"Closing sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }

    TEST_METHOD(OneSuccessfulServerConnectionWithExit)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = 1;
        ctsConfig::g_configSettings->Iterations = 1;
        ctsConfig::g_configSettings->AcceptLimit = 1;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(1);

        Logger::WriteMessage(L"Starting IO on sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"Closing sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }

    TEST_METHOD(ManySuccessfulServerConnectionWithExit)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = 100;
        ctsConfig::g_configSettings->Iterations = 100;
        ctsConfig::g_configSettings->AcceptLimit = 100;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(100);

        Logger::WriteMessage(L"Starting IO on sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"Closing sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }

    TEST_METHOD(OneSuccessfulServerConnectionWithoutExit)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = MAXULONGLONG;
        ctsConfig::g_configSettings->Iterations = 1;
        ctsConfig::g_configSettings->AcceptLimit = 1;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(1);

        Logger::WriteMessage(L"Starting IO on sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"Closing sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

        Assert::IsFalse(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(2);
    }

    TEST_METHOD(ManySuccessfulServerConnectionWithoutExit)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = MAXULONGLONG;
        ctsConfig::g_configSettings->Iterations = 100;
        ctsConfig::g_configSettings->AcceptLimit = 100;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

        auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(100);
        g_socketPool->print_objects();

        Logger::WriteMessage(L"Starting IO on 100 sockets - letting 100 more be created\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

        // more are being accepted while we complete these - wait for that to be done
        g_socketPool->wait_for_start(100);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Creating);

        g_socketPool->print_objects();

        Logger::WriteMessage(L"Closing 100 sockets - letting 100 move to initatingIo\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"Sleeping to let the callbacks cleanup closed sockets\n");
        Sleep(500);

        g_socketPool->print_objects();

        Logger::WriteMessage(L"Removing deleted objects\n");
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

        g_socketPool->print_objects();
        g_socketPool->validate_expected_count(200);

        testBroker.reset();

        // should create the next socket to accept on the next Timer callback
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
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


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(1);

        Logger::WriteMessage(L"Connecting sockets\n");
        g_socketPool->complete_state(WSAECONNREFUSED);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
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


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(100);

        Logger::WriteMessage(L"Connecting sockets\n");
        g_socketPool->complete_state(WSAECONNREFUSED);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }

    TEST_METHOD(OneFailedServerConnectionWithExit)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = 1;
        ctsConfig::g_configSettings->Iterations = 1;
        ctsConfig::g_configSettings->AcceptLimit = 1;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(1);

        Logger::WriteMessage(L"Connecting sockets\n");
        g_socketPool->complete_state(WSAECONNREFUSED);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }

    TEST_METHOD(ManyFailedServerConnectionWithExit)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = 100;
        ctsConfig::g_configSettings->Iterations = 100;
        ctsConfig::g_configSettings->AcceptLimit = 100;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(100);

        Logger::WriteMessage(L"Connecting sockets\n");
        g_socketPool->complete_state(WSAECONNREFUSED);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
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


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(1);

        Logger::WriteMessage(L"Starting IO on sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"Failing IO on sockets\n");
        g_socketPool->complete_state(WSAENOBUFS);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
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


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(100);

        Logger::WriteMessage(L"Starting IO on sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"Failing IO on sockets\n");
        g_socketPool->complete_state(WSAENOBUFS);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }

    TEST_METHOD(OneFailedServerConnectionWithExit_FailedIO)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = 1;
        ctsConfig::g_configSettings->Iterations = 1;
        ctsConfig::g_configSettings->AcceptLimit = 1;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(1);

        Logger::WriteMessage(L"Initiating IO on sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"Failing IO on sockets\n");
        g_socketPool->complete_state(WSAENOBUFS);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }

    TEST_METHOD(ManyFailedServerConnectionWithExit_FailedIO)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = 100;
        ctsConfig::g_configSettings->Iterations = 100;
        ctsConfig::g_configSettings->AcceptLimit = 100;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;


        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(100);

        Logger::WriteMessage(L"Initiating IO on sockets\n");
        g_socketPool->complete_state(NO_ERROR);
        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"Failing IO on sockets\n");
        g_socketPool->complete_state(WSAENOBUFS);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
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

        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();

        Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(5);

        Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 5 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"3. Expecting 5 creating, 5 initiating IO, 5 completed\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"4. Expecting 5 initiating IO, 10 completed\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"6. Expecting 15 completed\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(15, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
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

        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();

        Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(5);

        Logger::WriteMessage(L"2. Expecting 5 creating, 5 waiting, 5 closed\n");
        g_socketPool->complete_state(WSAECONNREFUSED); // fail connect

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

        Logger::WriteMessage(L"3. Expecting 5 creating, 10 closed\n");
        g_socketPool->complete_state(WSAECONNREFUSED); // fail connect

        g_socketPool->validate_expected_count(10, ctsSocketState::InternalState::Closed);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

        Logger::WriteMessage(L"4. Expecting 15 closed\n");
        g_socketPool->complete_state(WSAECONNREFUSED); // fail connect

        g_socketPool->validate_expected_count(15, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
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

        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();

        Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(5);

        Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 5 waiting\n");
        g_socketPool->complete_state(NO_ERROR); // successful connect

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"3. Expecting 5 creating, 10 closed\n");
        g_socketPool->complete_state(WSAECONNREFUSED); // fail connect

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

        Logger::WriteMessage(L"4. Expecting 15 closed\n");
        g_socketPool->complete_state(WSAECONNREFUSED); // fail connect

        g_socketPool->validate_expected_count(15, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }

    TEST_METHOD(MoreSuccessfulServerConnectionsThanAcceptLimit)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = 15;
        ctsConfig::g_configSettings->Iterations = 15;
        ctsConfig::g_configSettings->AcceptLimit = 5;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();

        Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(5);

        Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 5 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"3. Expecting 5 creating, 5 initiating IO, 5 completed\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"4. Expecting 5 initiating IO, 10 completed\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"6. Expecting 15 completed\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(15, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }

    TEST_METHOD(ServerExitLimitShouldOverrideIterations)
    {
        g_socketPool->reset();

        // Initialize config for this test
        // not a client (connecting), a server (accepting)
        ctsConfig::g_configSettings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {
        };
        ctsConfig::g_configSettings->ServerExitLimit = 1;
        ctsConfig::g_configSettings->Iterations = 15;
        ctsConfig::g_configSettings->AcceptLimit = 5;
        // these are not applicable to server
        ctsConfig::g_configSettings->ConnectionLimit = 0;
        ctsConfig::g_configSettings->ConnectionThrottleLimit = 0;

        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();

        Logger::WriteMessage(L"1. Expecting 1 creating\n");
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(1);

        Logger::WriteMessage(L"2. Expecting 1 initiating IO\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"3. Expecting 1 completed\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
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

        const auto testBroker(std::make_shared<ctsSocketBroker>());
        testBroker->Start();

        Logger::WriteMessage(L"1. Expecting 5 creating, 95 waiting\n");
        // wait for all to be started as this is async
        g_socketPool->wait_for_start(5);
        g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 90 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"3. Expecting 5 creating, 5 initiating IO, 85 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"4. Expecting 5 creating, 5 initiating IO, 80 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"5. Failing all sockets: 5 creating, 75 waiting\n");
        g_socketPool->complete_state(WSAENOBUFS);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"6. Expecting 5 creating, 5 initiating IO, 70 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"7. Expecting 5 creating, 5 initiating IO, 65 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"8. Failing all sockets: 5 creating, 60 waiting\n");
        g_socketPool->complete_state(WSAENOBUFS);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"9. Expecting 10 creating, 10 initiating IO, 55 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"10. Expecting 10 creating, 10 initiating IO, 50 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"11. Expecting 10 creating, 10 initiating IO, 45 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"12. Failing all sockets: 5 creating, 40 waiting\n");
        g_socketPool->complete_state(WSAENOBUFS);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"13. Expecting 10 creating, 10 initiating IO, 35 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"14. Expecting 10 creating, 10 initiating IO, 30 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"15. Expecting 10 creating, 10 initiating IO, 25 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"16. Expecting 10 creating, 10 initiating IO, 20 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"17. Failing all sockets: 5 creating, 15 waiting\n");
        g_socketPool->complete_state(WSAENOBUFS);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"18. Expecting 10 creating, 10 initiating IO, 10 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"19. Expecting 10 creating, 10 initiating IO, 5 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"20. Expecting 5 creating, 5 initiating IO, 0 waiting\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"21. Expecting 5 initiating IO\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIo);

        Logger::WriteMessage(L"22. Expecting all done\n");
        g_socketPool->complete_state(NO_ERROR);

        g_socketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

        Assert::IsTrue(testBroker->Wait(250));
        g_socketPool->remove_deleted_objects();
        g_socketPool->validate_expected_count(0);
    }
};
}
