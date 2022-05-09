/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <sdkddkver.h>
#include "CppUnitTest.h"

#include <memory>
#include <vector>
#include <atomic>

#include <ctString.hpp>
#include <ctSockaddr.hpp>

#include <ctsConfig.h>
#include "ctsSocket.h"
#include "ctsSocketState.h"

#include "ctsMediaStreamServer.h"
#include "ctsMediaStreamServerConnectedSocket.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{
template <>
inline std::wstring ToString<ctl::ctSockaddr>(const ctl::ctSockaddr& _value)
{
    return _value.writeCompleteAddress();
}
}

uint64_t g_transferSize = 0ULL;
bool g_isListening = false;

///
/// Fakes
///
namespace ctsTraffic
{
int64_t g_tcpBytesPerSecond = 0LL;

namespace ctsConfig
{
    ctsConfigSettings* g_configSettings;

    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t) noexcept
    {
    }

    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsTcpStatistics&) noexcept
    {
    }

    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsUdpStatistics&) noexcept
    {
    }

    void PrintDebug(_In_ _Printf_format_string_ PCWSTR, ...) noexcept
    {
    }

    void PrintException(const std::exception&) noexcept
    {
    }

    void PrintErrorInfo(_In_ _Printf_format_string_ PCWSTR, ...) noexcept
    {
    }

    bool IsListening() noexcept
    {
        return g_isListening;
    }

    uint64_t GetTransferSize() noexcept
    {
        return g_transferSize;
    }

    uint32_t GetMaxBufferSize() noexcept
    {
        return static_cast<uint32_t>(g_transferSize);
    }

    float GetStatusTimeStamp() noexcept
    {
        return 0.0f;
    }

    bool ShutdownCalled() noexcept
    {
        return false;
    }

    uint32_t ConsoleVerbosity() noexcept
    {
        return 0;
    }

    int64_t GetTcpBytesPerSecond() noexcept
    {
        return g_tcpBytesPerSecond;
    }
}

HANDLE g_RemovedSocketEvent = nullptr;
std::atomic<uint32_t> g_IOCount = 0;
std::atomic<uint32_t> g_IOPended = 0;
uint32_t g_IOStatusCode = ERROR_SUCCESS;
uint32_t g_IOTimeOffset = 0;
ctsTaskAction g_TaskAction = ctsTaskAction::None;
ctsIoStatus g_IOStatus = ctsIoStatus::ContinueIo;

ctsIoPattern::ctsIoPattern(uint32_t) :
    // (bytes/sec) * (1 sec/1000 ms) * (x ms/Quantum) == (bytes/quantum)
    m_bytesSendingPerQuantum(ctsConfig::GetTcpBytesPerSecond() * ctsConfig::g_configSettings->TcpBytesPerSecondPeriod / 1000LL),
    m_quantumStartTimeMs(ctl::ctTimer::snap_qpc_as_msec())
{
    Logger::WriteMessage(L"ctsIOPattern::ctsIOPattern\n");
}

ctsTask ctsIoPattern::InitiateIo() noexcept
{
    Logger::WriteMessage(L"ctsIOPattern::initiate_io\n");

    const uint32_t pended_io = g_IOPended.load();
    const uint32_t remaining_io = g_IOCount.load();

    ctsTask return_task;
    if (pended_io == 0 && remaining_io > 0)
    {
        return_task.m_ioAction = ctsTaskAction::Send;
        return_task.m_timeOffsetMilliseconds = g_IOTimeOffset;
        ++g_IOPended;
    }
    else
    {
        return_task.m_ioAction = ctsTaskAction::None;
        return_task.m_timeOffsetMilliseconds = 0;
    }
    return return_task;
}

ctsIoStatus ctsIoPattern::CompleteIo(const ctsTask&, uint32_t, uint32_t _status_code) noexcept
{
    Assert::AreEqual(g_IOStatusCode, _status_code);
    Logger::WriteMessage(L"ctsIOPattern::complete_io\n");
    --g_IOPended;
    --g_IOCount;

    return g_IOStatus;
}

[[nodiscard]] wil::cs_leave_scope_exit ctsIoPattern::AcquireIoPatternLock() const noexcept
{
    return {};
}

// test IO pattern for fakes for this test
class ctsMediaStreamServerUnitTestIOPattern : public ctsIoPattern
{
public:
    // default the base class 1 recv buffer
    ctsMediaStreamServerUnitTestIOPattern() :
        ctsIoPattern(1)
    {
        Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::ctsMediaStreamServerUnitTestIOPattern\n");
    }

    // none of these are called - required to be defined
    void PrintStatistics(const ctl::ctSockaddr&, const ctl::ctSockaddr&) noexcept override
    {
        Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::print_stats\n");
        Assert::IsFalse(true);
    }

    ctsTask GetNextTaskFromPattern() override
    {
        Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::next_task\n");
        Assert::IsFalse(true);
        return ctsTask();
    }

    ctsIoPatternError CompleteTaskBackToPattern(const ctsTask&, uint32_t) noexcept override
    {
        Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::completed_task\n");
        Assert::IsFalse(true);
        return ctsIoPatternError::NoError;
    }

    void StartStatistics() noexcept override
    {
        Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::start_stats\n");
        Assert::IsFalse(true);
    }

    void EndStatistics() noexcept override
    {
        Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::end_stats\n");
        Assert::IsFalse(true);
    }

    char* GetConnectionIdentifier() noexcept override
    {
        Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::connection_id\n");
        Assert::IsFalse(true);
        return nullptr;
    }

    void PrintTcpInfo(const ctl::ctSockaddr&, const ctl::ctSockaddr&, SOCKET) noexcept override
    {
        Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::PrintTcpInfo\n");
    }

};

// ctsSocketState fakes
ctsSocketState::ctsSocketState(std::weak_ptr<ctsSocketBroker>)
{
}

ctsSocketState::~ctsSocketState() noexcept
{
}

// ctsSocket fakes
ctsSocket::ctsSocket(std::weak_ptr<ctsSocketState>) noexcept
{
    m_pattern = std::make_shared<ctsMediaStreamServerUnitTestIOPattern>();
}

ctsSocket::~ctsSocket() noexcept
{
}

void ctsSocket::SetSocket(SOCKET _s) noexcept
{
    m_socket.reset(_s);
}

void ctsSocket::CompleteState(DWORD) noexcept
{
    SetEvent(g_RemovedSocketEvent);
}

ctsSocket::SocketReference ctsSocket::AcquireSocketLock() const noexcept
{
    return SocketReference({}, m_socket.get(), m_pattern);
}

// one callout fake to ctsMediaStreamServerImpl
void ctsMediaStreamServerImpl::RemoveSocket(const ctl::ctSockaddr&)
{
}
}

///
/// End of Fakes
///

using namespace ctsTraffic;

namespace ctsUnitTest
{
TEST_CLASS(ctsMediaStreamServerConnectedSocketUnitTest)
{
public:
    TEST_CLASS_INITIALIZE(Setup)
    {
        WSADATA wsadata;
        const auto startup = WSAStartup(WINSOCK_VERSION, &wsadata);
        Assert::AreEqual(0, startup);

        g_RemovedSocketEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Assert::IsNotNull(g_RemovedSocketEvent);

        ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
    }

    TEST_CLASS_CLEANUP(Cleanup)
    {
        CloseHandle(g_RemovedSocketEvent);
        WSACleanup();
        delete ctsConfig::g_configSettings;
    }

    TEST_METHOD(SingleIO)
    {
        g_IOCount = 1;
        g_IOStatus = ctsIoStatus::ContinueIo;
        g_IOStatusCode = ERROR_SUCCESS;
        g_TaskAction = ctsTaskAction::None;
        g_IOTimeOffset = 0;
        ResetEvent(g_RemovedSocketEvent);

        const std::vector test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
        Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

        auto socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
        const auto test_socket(std::make_shared<ctsSocket>(socket_state));
        test_socket->SetSocket(INVALID_SOCKET);

        uint32_t callback_invoked = 0;
        ctsMediaStreamServerConnectedSocket test_connected_socket(
            std::weak_ptr(test_socket),
            INVALID_SOCKET,
            test_addr[0],
            [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.GetSocket();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                g_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;
        // directly scheduling the first task
        g_IOPended = 1;
        test_connected_socket.ScheduleTask(test_task);
        // not 'done' yet, just stopped sending for the time-being
        Assert::AreEqual(static_cast<DWORD>(WAIT_TIMEOUT), WaitForSingleObject(g_RemovedSocketEvent, 0));
        const uint32_t ExpectedCallbacks = 1;
        Assert::AreEqual(ExpectedCallbacks, callback_invoked);
    }

    TEST_METHOD(MultipleIO)
    {
        g_IOCount = 10;
        g_IOStatus = ctsIoStatus::ContinueIo;
        g_IOStatusCode = ERROR_SUCCESS;
        g_TaskAction = ctsTaskAction::None;
        g_IOTimeOffset = 0;
        ResetEvent(g_RemovedSocketEvent);

        const std::vector test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
        Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

        auto socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
        const auto test_socket(std::make_shared<ctsSocket>(socket_state));
        test_socket->SetSocket(INVALID_SOCKET);

        uint32_t callback_invoked = 0;
        ctsMediaStreamServerConnectedSocket test_connected_socket(
            std::weak_ptr(test_socket),
            INVALID_SOCKET,
            test_addr[0],
            [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.GetSocket();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                g_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;
        // directly scheduling the first task
        g_IOPended = 1;
        test_connected_socket.ScheduleTask(test_task);
        // not 'done' yet, just stopped sending for the time-being
        Assert::AreEqual(static_cast<DWORD>(WAIT_TIMEOUT), WaitForSingleObject(g_RemovedSocketEvent, 0));
        const uint32_t ExpectedCallbacks = 10;
        Assert::AreEqual(ExpectedCallbacks, callback_invoked);
    }

    TEST_METHOD(MultipleScheduledIO)
    {
        g_IOCount = 10;
        g_IOStatus = ctsIoStatus::ContinueIo;
        g_IOStatusCode = ERROR_SUCCESS;
        g_TaskAction = ctsTaskAction::None;
        g_IOTimeOffset = 100; // 100ms apart
        ResetEvent(g_RemovedSocketEvent);

        const std::vector test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
        Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

        auto socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
        const auto test_socket(std::make_shared<ctsSocket>(socket_state));
        test_socket->SetSocket(INVALID_SOCKET);

        uint32_t callback_invoked = 0;
        ctsMediaStreamServerConnectedSocket test_connected_socket(
            std::weak_ptr(test_socket),
            INVALID_SOCKET,
            test_addr[0],
            [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.GetSocket();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                if (callback_invoked == 10)
                {
                    g_IOStatus = ctsIoStatus::CompletedIo;
                }
                g_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;
        // directly scheduling the first task
        g_IOPended = 1;
        test_connected_socket.ScheduleTask(test_task);
        // should complete within 1 second (a few ms after 900ms)
        Assert::AreEqual(WAIT_OBJECT_0, WaitForSingleObject(g_RemovedSocketEvent, 1250));
        const uint32_t ExpectedCallbacks = 10;
        Assert::AreEqual(ExpectedCallbacks, callback_invoked);
    }

    TEST_METHOD(FailSingleIO)
    {
        // should fail the first one
        g_IOCount = 2;
        g_IOStatus = ctsIoStatus::FailedIo;
        g_IOStatusCode = ERROR_SUCCESS;
        g_TaskAction = ctsTaskAction::None;
        ResetEvent(g_RemovedSocketEvent);

        const std::vector test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
        Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

        auto socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
        const auto test_socket(std::make_shared<ctsSocket>(socket_state));
        test_socket->SetSocket(INVALID_SOCKET);

        uint32_t callback_invoked = 0;
        ctsMediaStreamServerConnectedSocket test_connected_socket(
            std::weak_ptr(test_socket),
            INVALID_SOCKET,
            test_addr[0],
            [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.GetSocket();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                g_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;
        // directly scheduling the first task
        g_IOPended = 1;
        test_connected_socket.ScheduleTask(test_task);
        // 'done' since it failed
        Assert::AreEqual(WAIT_OBJECT_0, WaitForSingleObject(g_RemovedSocketEvent, 0));
        const uint32_t ExpectedCallbacks = 1;
        Assert::AreEqual(ExpectedCallbacks, callback_invoked);
    }

    TEST_METHOD(FailAfterMultipleIO)
    {
        // will fail after 5
        g_IOCount = 10;
        g_IOStatus = ctsIoStatus::ContinueIo;
        g_IOStatusCode = ERROR_SUCCESS;
        g_TaskAction = ctsTaskAction::None;
        g_IOTimeOffset = 100; // 100ms apart
        ResetEvent(g_RemovedSocketEvent);

        const std::vector test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
        Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

        auto socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
        const auto test_socket(std::make_shared<ctsSocket>(socket_state));
        test_socket->SetSocket(INVALID_SOCKET);

        uint32_t callback_invoked = 0;
        ctsMediaStreamServerConnectedSocket test_connected_socket(
            std::weak_ptr(test_socket),
            INVALID_SOCKET,
            test_addr[0],
            [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.GetSocket();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                if (callback_invoked == 5)
                {
                    g_IOStatus = ctsIoStatus::FailedIo;
                }
                g_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;
        // directly scheduling the first task
        g_IOPended = 1;
        test_connected_socket.ScheduleTask(test_task);
        // should complete within 500ms - failing after 5 IO
        Assert::AreEqual(WAIT_OBJECT_0, WaitForSingleObject(g_RemovedSocketEvent, 500));
        const uint32_t ExpectedCallbacks = 5;
        Assert::AreEqual(ExpectedCallbacks, callback_invoked);
    }
};
}
