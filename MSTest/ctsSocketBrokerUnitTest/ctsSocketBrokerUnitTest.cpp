/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <SDKDDKVer.h>
#include "CppUnitTest.h"

// cpp headers
#include <vector>
#include <memory>
// os headers
#include <windows.h>
// wil headers
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

    template<> inline std::wstring ToString<ctsTraffic::ctsSocketState::InternalState>(const ctsTraffic::ctsSocketState::InternalState& _state)
    {
        switch (_state)
        {
            case ctsTraffic::ctsSocketState::InternalState::Creating: return L"Creating";
            case ctsTraffic::ctsSocketState::InternalState::Created: return L"Created";
            case ctsTraffic::ctsSocketState::InternalState::Connecting: return L"Connecting";
            case ctsTraffic::ctsSocketState::InternalState::Connected: return L"Connected";
            case ctsTraffic::ctsSocketState::InternalState::InitiatingIO: return L"InitiatingIO";
            case ctsTraffic::ctsSocketState::InternalState::InitiatedIO: return L"InitiatedIO";
            case ctsTraffic::ctsSocketState::InternalState::Closing: return L"Closing";
            case ctsTraffic::ctsSocketState::InternalState::Closed: return L"Closed";
        }
        return ctl::ctString::ctFormatString(L"Unknown State (0x%x)", _state);
    }
}


///
/// Fakes
///
namespace ctsTraffic::ctsConfig
{
    ctsConfigSettings* Settings;

    void PrintDebug(PCWSTR _text, ...) noexcept
    {
        va_list args;
        va_start(args, _text);

        const auto formatted(ctl::ctString::ctFormatStringVa(_text, args));
        Logger::WriteMessage(ctl::ctString::ctFormatString(L"PrintDebug: %ws\n", formatted.c_str()).c_str());

        va_end(args);
    }
    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long) noexcept
    {
        Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(error)\n");
    }
    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long, const ctsTcpStatistics&) noexcept
    {
        Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsTcpStatistics)\n");
    }
    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long, const ctsUdpStatistics&) noexcept
    {
        Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsUdpStatistics)\n");
    }
    bool ShutdownCalled() noexcept
    {
        return false;
    }
    unsigned long ConsoleVerbosity() noexcept
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
    void add_object(const std::shared_ptr<ctsSocketState>& _state_object)
    {
        const auto hold_lock = cs.lock();

        state_objects.push_back(_state_object);
    }
    void remove_deleted_objects() noexcept
    {
        const auto hold_lock = cs.lock();

        state_objects.erase(
            std::remove_if(
                std::begin(state_objects),
                std::end(state_objects),
                [&](const std::weak_ptr<ctsSocketState>& _weak_ptr) { return _weak_ptr.expired(); }),
            std::end(state_objects));
    }
    void reset() noexcept
    {
        const auto hold_lock = cs.lock();

        state_objects.clear();
    }

    /// Interact with states of contained ctsSocketState objects
    void complete_state(DWORD _error_code)
    {
        const auto hold_lock = cs.lock();

        for (auto& socket_state : state_objects)
        {
            auto shared_state(socket_state.lock());
            Assert::IsNotNull(shared_state.get());
            if (shared_state)
            {
                shared_state->complete_state(_error_code);
            }
        }
    }
    void validate_expected_count(size_t _count)
    {
        const auto hold_lock = cs.lock();

        Assert::AreEqual(_count, state_objects.size());
    }
    void validate_expected_count(size_t _count, ctsSocketState::InternalState _state)
    {
        const auto hold_lock = cs.lock();

        size_t matched_state = 0;
        for (auto& socket_state : state_objects)
        {
            auto shared_state(socket_state.lock());
            Assert::IsNotNull(shared_state.get());
            if (shared_state)
            {
                if (shared_state->current_state() == _state)
                {
                    ++matched_state;
                }
            }
        }

        Assert::AreEqual(_count, matched_state);
    }

    // non-copyable
    SocketStatePool(const SocketStatePool&) = delete;
    SocketStatePool& operator=(const SocketStatePool&) = delete;

private:
    wil::critical_section cs;
    std::vector<std::weak_ptr<ctsSocketState>> state_objects;
};

SocketStatePool* s_SocketPool;


///
/// Faking the ctsSocketState that the broker instantiates based off of quota
/// - don't need to actually do any work - just need to control indications back to the broker
/// - but we do need to track all instances created so we can control each socketstate
///
ctsSocketState::ctsSocketState(std::weak_ptr<ctsSocketBroker> _broker) :
    broker(std::move(_broker))
{
}

ctsSocketState::~ctsSocketState() noexcept
{
    s_SocketPool->remove_deleted_objects();
}

void ctsSocketState::start() noexcept
{
    s_SocketPool->add_object(this->shared_from_this());
}

void ctsSocketState::complete_state(DWORD _error_code) noexcept
{
    if (NO_ERROR == _error_code)
    {
// walk states from creating -> InitiatingIO -> Closed
        switch (this->state)
        {

// Skipping Connecting, since that state doesn't affect ctsSocketBroker

            case ctsSocketState::InternalState::Creating:
            {
                auto parent = this->broker.lock();
                parent->initiating_io();
                this->state = ctsSocketState::InternalState::InitiatingIO;
                break;
            }
            case ctsSocketState::InternalState::InitiatingIO:
            {
                auto parent = this->broker.lock();
                parent->closing(true);
                this->state = ctsSocketState::InternalState::Closed;
                break;
            }

            default:
                Assert::Fail(
                    ctl::ctString::ctFormatString(L"Unexpected ctsSocketState: 0x%x\n", this->state).c_str());
        }
    }
    else
    {
     // move straight to Closed
        auto parent = this->broker.lock();
        parent->closing(ctsSocketState::InternalState::InitiatingIO == this->state);
        this->state = ctsSocketState::InternalState::Closed;
    }
}

ctsSocketState::InternalState ctsSocketState::current_state() const noexcept
{
    return this->state;
}


namespace ctsUnitTest
{
    TEST_CLASS(ctsSocketBrokerUnitTest)
    {
    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            s_SocketPool = new SocketStatePool;

            ctsConfig::Settings = new ctsConfig::ctsConfigSettings;
            ctsConfig::Settings->CtrlCHandle = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            Assert::AreNotEqual(static_cast<HANDLE>(nullptr), ctsConfig::Settings->CtrlCHandle);
        }
        TEST_CLASS_CLEANUP(Cleanup)
        {
            delete ctsConfig::Settings;
            delete s_SocketPool;
        }

        TEST_METHOD_INITIALIZE(MethodSetup)
        {
            ctsSocketBroker::s_TimerCallbackTimeoutMs = 333;
        }
        TEST_METHOD_CLEANUP(MethodCleanup)
        {
            // drain the Timer
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2);
        }

        TEST_METHOD(OneSuccessfulClientConnection)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->ConnectionLimit = 1;
            ctsConfig::Settings->ConnectionThrottleLimit = 1;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 100;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Starting IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Closing sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManySuccessfulClientConnection)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->ConnectionLimit = 100;
            ctsConfig::Settings->ConnectionThrottleLimit = 100;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 750;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Starting IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Closing sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneSuccessfulServerConnectionWithExit)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = 1;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->AcceptLimit = 1;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 100;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Starting IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Closing sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManySuccessfulServerConnectionWithExit)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = 100;
            ctsConfig::Settings->Iterations = 100;
            ctsConfig::Settings->AcceptLimit = 100;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 750;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Starting IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Closing sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneSuccessfulServerConnectionWithoutExit)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = MAXULONGLONG;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->AcceptLimit = 1;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 100;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Starting IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Closing sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsFalse(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(1);
        }
        TEST_METHOD(ManySuccessfulServerConnectionWithoutExit)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = MAXULONGLONG;
            ctsConfig::Settings->Iterations = 100;
            ctsConfig::Settings->AcceptLimit = 100;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 750;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Starting IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Closing sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsFalse(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            // should create the next socket to accept on the next Timer callback
            s_SocketPool->validate_expected_count(100);
        }

        TEST_METHOD(OneFailedClientConnection_FailedConnect)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->ConnectionLimit = 1;
            ctsConfig::Settings->ConnectionThrottleLimit = 1;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 100;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Connecting sockets");
            s_SocketPool->complete_state(WSAECONNREFUSED);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManyFailedClientConnection_FailedConnect)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->ConnectionLimit = 100;
            ctsConfig::Settings->ConnectionThrottleLimit = 100;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 750;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Connecting sockets");
            s_SocketPool->complete_state(WSAECONNREFUSED);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneFailedServerConnectionWithExit)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = 1;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->AcceptLimit = 1;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 100;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Connecting sockets");
            s_SocketPool->complete_state(WSAECONNREFUSED);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManyFailedServerConnectionWithExit)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = 100;
            ctsConfig::Settings->Iterations = 100;
            ctsConfig::Settings->AcceptLimit = 100;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 750;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Connecting sockets");
            s_SocketPool->complete_state(WSAECONNREFUSED);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneFailedClientConnection_FailedIO)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->ConnectionLimit = 1;
            ctsConfig::Settings->ConnectionThrottleLimit = 1;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 100;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Starting IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Failing IO on sockets");
            s_SocketPool->complete_state(WSAENOBUFS);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManyFailedClientConnection_FailedIO)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->ConnectionLimit = 100;
            ctsConfig::Settings->ConnectionThrottleLimit = 100;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 750;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Starting IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Failing IO on sockets");
            s_SocketPool->complete_state(WSAENOBUFS);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(OneFailedServerConnectionWithExit_FailedIO)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = 1;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->AcceptLimit = 1;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 100;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"Initiating IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Failing IO on sockets");
            s_SocketPool->complete_state(WSAENOBUFS);
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }
        TEST_METHOD(ManyFailedServerConnectionWithExit_FailedIO)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = 100;
            ctsConfig::Settings->Iterations = 100;
            ctsConfig::Settings->AcceptLimit = 100;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            ctsSocketBroker::s_TimerCallbackTimeoutMs = 750;
            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            s_SocketPool->validate_expected_count(100);

            Logger::WriteMessage(L"Initiating IO on sockets");
            s_SocketPool->complete_state(NO_ERROR);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"Failing IO on sockets");
            s_SocketPool->complete_state(WSAENOBUFS);
            s_SocketPool->validate_expected_count(100, ctsSocketState::InternalState::Closed);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(MoreSuccessfulClientConnectionsThanConnectionThrottleLimit)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->ConnectionLimit = 15;
            ctsConfig::Settings->ConnectionThrottleLimit = 5;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
            s_SocketPool->validate_expected_count(5);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 5 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"3. Expecting 5 creating, 5 initiating IO, 5 completed\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"4. Expecting 5 initiating IO, 10 completed\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"6. Expecting 15 completed\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(MoreFailedClientConnectionsThanConnectionThrottleLimit_FailedConnect)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->ConnectionLimit = 15;
            ctsConfig::Settings->ConnectionThrottleLimit = 5;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 waiting, 5 closed\n");
            s_SocketPool->complete_state(WSAECONNREFUSED); // fail connect
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"3. Expecting 5 creating, 10 closed\n");
            s_SocketPool->complete_state(WSAECONNREFUSED); // fail connect
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"4. Expecting 15 closed\n");
            s_SocketPool->complete_state(WSAECONNREFUSED); // fail connect
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(MoreFailedClientConnectionsThanConnectionThrottleLimit_FailedIO)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 1;
            ctsConfig::Settings->ConnectionLimit = 15;
            ctsConfig::Settings->ConnectionThrottleLimit = 5;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 5 waiting\n");
            s_SocketPool->complete_state(NO_ERROR); // successful connect
            ::Sleep(1000); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"3. Expecting 5 creating, 10 closed\n");
            s_SocketPool->complete_state(WSAECONNREFUSED); // fail connect
            ::Sleep(1000); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"4. Expecting 15 closed\n");
            s_SocketPool->complete_state(WSAECONNREFUSED); // fail connect
            ::Sleep(1000); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(MoreSuccessfulServerConnectionsThanAcceptLimit)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = 15;
            ctsConfig::Settings->Iterations = 15;
            ctsConfig::Settings->AcceptLimit = 5;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 10 waiting\n");
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 5 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"3. Expecting 5 creating, 5 initiating IO, 5 completed\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"4. Expecting 5 initiating IO, 10 completed\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"6. Expecting 15 completed\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            s_SocketPool->validate_expected_count(0);
        }

        TEST_METHOD(ServerExitLimitShouldOverrideIterations)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // not a client (connecting), a server (accepting)
            ctsConfig::Settings->AcceptFunction = [](std::weak_ptr<ctsSocket>) {};
            ctsConfig::Settings->ServerExitLimit = 1;
            ctsConfig::Settings->Iterations = 15;
            ctsConfig::Settings->AcceptLimit = 5;
            // these are not applicable to server
            ctsConfig::Settings->ConnectionLimit = 0;
            ctsConfig::Settings->ConnectionThrottleLimit = 0;

            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            Logger::WriteMessage(L"1. Expecting 1 creating\n");
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::Creating);

            Logger::WriteMessage(L"2. Expecting 1 initiating IO\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(1, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"3. Expecting 1 completed\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            ::Sleep(ctsSocketBroker::s_TimerCallbackTimeoutMs);
            s_SocketPool->validate_expected_count(0);
        }


        TEST_METHOD(ManySuccessfulClientConnectionsMixingIterationsAndConnections)
        {
            s_SocketPool->reset();

            // Initialize config for this test
            // a client (connecting), not a server (accepting)
            ctsConfig::Settings->AcceptFunction = nullptr;
            ctsConfig::Settings->Iterations = 10;
            ctsConfig::Settings->ConnectionLimit = 10;
            ctsConfig::Settings->ConnectionThrottleLimit = 5;
            // these are not applicable to client
            ctsConfig::Settings->ServerExitLimit = 0;
            ctsConfig::Settings->AcceptLimit = 0;

            std::shared_ptr<ctsSocketBroker> test_broker(std::make_shared<ctsSocketBroker>());
            test_broker->start();

            Logger::WriteMessage(L"1. Expecting 5 creating, 95 waiting\n");
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"2. Expecting 5 creating, 5 initiating IO, 90 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"3. Expecting 5 creating, 5 initiating IO, 85 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"4. Expecting 5 creating, 5 initiating IO, 80 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"5. Failing all sockets: 5 creating, 75 waiting\n");
            s_SocketPool->complete_state(WSAENOBUFS);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"6. Expecting 5 creating, 5 initiating IO, 70 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"7. Expecting 5 creating, 5 initiating IO, 65 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"8. Failing all sockets: 5 creating, 60 waiting\n");
            s_SocketPool->complete_state(WSAENOBUFS);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"9. Expecting 10 creating, 10 initiating IO, 55 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"10. Expecting 10 creating, 10 initiating IO, 50 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"11. Expecting 10 creating, 10 initiating IO, 45 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"12. Failing all sockets: 5 creating, 40 waiting\n");
            s_SocketPool->complete_state(WSAENOBUFS);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"13. Expecting 10 creating, 10 initiating IO, 35 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"14. Expecting 10 creating, 10 initiating IO, 30 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"15. Expecting 10 creating, 10 initiating IO, 25 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"16. Expecting 10 creating, 10 initiating IO, 20 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"17. Failing all sockets: 5 creating, 15 waiting\n");
            s_SocketPool->complete_state(WSAENOBUFS);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(0, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"18. Expecting 10 creating, 10 initiating IO, 10 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"19. Expecting 10 creating, 10 initiating IO, 5 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"20. Expecting 5 creating, 5 initiating IO, 0 waiting\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::Creating);
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"21. Expecting 5 initiating IO\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(5, ctsSocketState::InternalState::InitiatingIO);

            Logger::WriteMessage(L"22. Expecting all done\n");
            s_SocketPool->complete_state(NO_ERROR);
            ::Sleep(500); // allowing the timer to coalesce
            s_SocketPool->validate_expected_count(0);

            Assert::IsTrue(test_broker->wait(ctsSocketBroker::s_TimerCallbackTimeoutMs * 2));
            // let the timer fire
            s_SocketPool->validate_expected_count(0);
        }
    };
}