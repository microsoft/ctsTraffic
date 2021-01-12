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

#include "ctsSafeInt.hpp"
#include "ctsSocket.h"
#include "ctsSocketState.h"

#include "ctsMediaStreamServer.h"
#include "ctsMediaStreamServerConnectedSocket.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{
    template<> inline std::wstring ToString<ctsTraffic::ctsUnsignedLongLong>(const ctsTraffic::ctsUnsignedLongLong& value)
    {
        return std::to_wstring(static_cast<unsigned long long>(value));
    }

    template<> inline std::wstring ToString<ctl::ctSockaddr>(const ctl::ctSockaddr& _value)
    {
        return _value.WriteCompleteAddress();
    }
}

ctsTraffic::ctsUnsignedLongLong g_transferSize = 0ULL;
bool g_isListening = false;
///
/// Fakes
///
namespace ctsTraffic
{
    namespace ctsConfig
    {
        ctsConfigSettings* g_configSettings;

        void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long) noexcept
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long, const ctsTcpStatistics&) noexcept
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long, const ctsUdpStatistics&) noexcept
        {
        }
        void PrintDebug(_In_z_ _Printf_format_string_ PCWSTR, ...) noexcept
        {
        }
        void PrintException(const std::exception&) noexcept
        {
        }
        void PrintErrorInfo(_In_z_ _Printf_format_string_ PCWSTR, ...) noexcept
        {
        }

        bool IsListening() noexcept
        {
            return g_isListening;
        }

        ctsUnsignedLongLong GetTransferSize() noexcept
        {
            return g_transferSize;
        }

        ctsUnsignedLong GetMaxBufferSize() noexcept
        {
            return g_transferSize;
        }

        float GetStatusTimeStamp() noexcept
        {
            return 0.0f;
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

    HANDLE s_RemovedSocketEvent = NULL;
    std::atomic<unsigned long> s_IOCount = 0;
    std::atomic<unsigned long> s_IOPended = 0;
    unsigned long s_IOStatusCode = ERROR_SUCCESS;
    unsigned long s_IOTimeOffset = 0;
    ctsTaskAction s_TaskAction = ctsTaskAction::None;
    ctsIoStatus s_IOStatus = ctsIoStatus::ContinueIo;

    ctsIoPattern::ctsIoPattern(unsigned long)
    {
        Logger::WriteMessage(L"ctsIOPattern::ctsIOPattern\n");
    }

    ctsTask ctsIoPattern::InitiateIo() noexcept
    {
        Logger::WriteMessage(L"ctsIOPattern::initiate_io\n");

        const unsigned long pended_io = s_IOPended.load();
        const unsigned long remaining_io = s_IOCount.load();

        ctsTask return_task;
        if (pended_io == 0 && remaining_io > 0)
        {
            return_task.m_ioAction = ctsTaskAction::Send;
            return_task.m_timeOffsetMilliseconds = s_IOTimeOffset;
            ++s_IOPended;
        }
        else
        {
            return_task.m_ioAction = ctsTaskAction::None;
            return_task.m_timeOffsetMilliseconds = 0;
        }
        return return_task;
    }

    ctsIoStatus ctsIoPattern::CompleteIo(const ctsTask&, unsigned long, unsigned long _status_code) noexcept
    {
        Assert::AreEqual(s_IOStatusCode, _status_code);
        Logger::WriteMessage(L"ctsIOPattern::complete_io\n");
        --s_IOPended;
        --s_IOCount;

        return s_IOStatus;
    }

    // test IO pattern for fakes for this test
    class ctsMediaStreamServerUnitTestIOPattern : public ctsIoPattern
    {
    public:
        // default the base class 1 recv buffer
        ctsMediaStreamServerUnitTestIOPattern() : ctsIoPattern(1)
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::ctsMediaStreamServerUnitTestIOPattern\n");
        }

        // none of these are called - required to be defined
        virtual void PrintStatistics(const ctl::ctSockaddr&, const ctl::ctSockaddr&) noexcept
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

        ctsIoPatternError CompleteTaskBackToPattern(const ctsTask&, unsigned long) noexcept override
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

    void ctsSocket::CompleteState(unsigned long) noexcept
    {
        ::SetEvent(s_RemovedSocketEvent);
    }

    ctsIoPattern::LockedIoPattern ctsSocket::LockIoPattern() const noexcept
    {
        return ctsIoPattern::LockedIoPattern(m_pattern);
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
            const auto startup = ::WSAStartup(WINSOCK_VERSION, &wsadata);
            Assert::AreEqual(0, startup);

            s_RemovedSocketEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            Assert::IsNotNull(s_RemovedSocketEvent);

            ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;
            ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            ::CloseHandle(s_RemovedSocketEvent);
            ::WSACleanup();
            delete ctsConfig::g_configSettings;
        }

        TEST_METHOD(SingleIO)
        {
            s_IOCount = 1;
            s_IOStatus = ctsIoStatus::ContinueIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = ctsTaskAction::None;
            s_IOTimeOffset = 0;
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->SetSocket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult
            {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.Get();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsTask test_task;
            test_task.m_ioAction = ctsTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.ScheduleTask(test_task);
            // not 'done' yet, just stopped sending for the time-being
            Assert::AreEqual(static_cast<DWORD>(WAIT_TIMEOUT), ::WaitForSingleObject(s_RemovedSocketEvent, 0));
            Assert::AreEqual(1UL, callback_invoked);
        }

        TEST_METHOD(MultipleIO)
        {
            s_IOCount = 10;
            s_IOStatus = ctsIoStatus::ContinueIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = ctsTaskAction::None;
            s_IOTimeOffset = 0;
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->SetSocket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult
            {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.Get();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsTask test_task;
            test_task.m_ioAction = ctsTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.ScheduleTask(test_task);
            // not 'done' yet, just stopped sending for the time-being
            Assert::AreEqual(static_cast<DWORD>(WAIT_TIMEOUT), ::WaitForSingleObject(s_RemovedSocketEvent, 0));
            Assert::AreEqual(10UL, callback_invoked);
        }

        TEST_METHOD(MultipleScheduledIO)
        {
            s_IOCount = 10;
            s_IOStatus = ctsIoStatus::ContinueIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = ctsTaskAction::None;
            s_IOTimeOffset = 100; // 100ms apart
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->SetSocket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.Get();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                if (callback_invoked == 10)
                {
                    s_IOStatus = ctsIoStatus::CompletedIo;
                }
                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsTask test_task;
            test_task.m_ioAction = ctsTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.ScheduleTask(test_task);
            // should complete within 1 second (a few ms after 900ms)
            Assert::AreEqual(WAIT_OBJECT_0, ::WaitForSingleObject(s_RemovedSocketEvent, 1250));
            Assert::AreEqual(10UL, callback_invoked);
        }

        TEST_METHOD(FailSingleIO)
        {
            // should fail the first one
            s_IOCount = 2;
            s_IOStatus = ctsIoStatus::FailedIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = ctsTaskAction::None;
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->SetSocket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult
            {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.Get();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsTask test_task;
            test_task.m_ioAction = ctsTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.ScheduleTask(test_task);
            // 'done' since it failed
            Assert::AreEqual(WAIT_OBJECT_0, ::WaitForSingleObject(s_RemovedSocketEvent, 0));
            Assert::AreEqual(1UL, callback_invoked);
        }

        TEST_METHOD(FailAfterMultipleIO)
        {
            // will fail after 5
            s_IOCount = 10;
            s_IOStatus = ctsIoStatus::ContinueIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = ctsTaskAction::None;
            s_IOTimeOffset = 100; // 100ms apart
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->SetSocket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&](ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult {
                ++callback_invoked;

                const auto socket_guard(test_socket->AcquireSocketLock());
                const SOCKET cts_socket = socket_guard.Get();
                const SOCKET connected_socket = _socket_object->GetSendingSocket();

                Assert::AreEqual(test_addr[0], _socket_object->GetRemoteAddress());
                Assert::AreEqual(cts_socket, connected_socket);

                if (callback_invoked == 5)
                {
                    s_IOStatus = ctsIoStatus::FailedIo;
                }
                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsTask test_task;
            test_task.m_ioAction = ctsTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.ScheduleTask(test_task);
            // should complete within 500ms - failing after 5 IO
            Assert::AreEqual(WAIT_OBJECT_0, ::WaitForSingleObject(s_RemovedSocketEvent, 500));
            Assert::AreEqual(5UL, callback_invoked);
        }
    };
}