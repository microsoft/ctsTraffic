/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <SDKDDKVer.h>
#include "CppUnitTest.h"

#include <memory>
#include <vector>
#include <atomic>

#include <ctString.hpp>
#include <ctSockaddr.hpp>
#include <ctVersionConversion.hpp>

#include "ctsSafeInt.hpp"
#include "ctsSocket.h"
#include "ctsSocketState.h"
#include "ctsSocketGuard.hpp"

#include "ctsMediaStreamServer.h"
#include "ctsMediaStreamServerConnectedSocket.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft {
    namespace VisualStudio {
        namespace CppUnitTestFramework {
            template<> static std::wstring ToString<ctsTraffic::ctsUnsignedLongLong>(const ctsTraffic::ctsUnsignedLongLong& _value)
            {
                return std::to_wstring(static_cast<unsigned long long>(_value));
            }

            template<> static std::wstring ToString<ctl::ctSockaddr>(const ctl::ctSockaddr& _value)
            {
                return _value.writeCompleteAddress();
            }
        }
    }
}

ctsTraffic::ctsUnsignedLongLong s_TransferSize = 0ULL;
bool s_Listening = false;
///
/// Fakes
///
namespace ctsTraffic {
    namespace ctsConfig {
        ctsConfigSettings* Settings;

        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error) noexcept
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsTcpStatistics& _stats) noexcept
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsUdpStatistics& _stats) noexcept
        {
        }
        void PrintDebug(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) noexcept
        {
        }
        void PrintException(const std::exception& e) noexcept
        {
        }
        void PrintJitterUpdate(long long _sequence_number, long long _sender_qpc, long long _sender_qpf, long long _recevier_qpc, long long _receiver_qpf) noexcept
        {
        }
        void PrintErrorInfo(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) noexcept
        {
        }

        bool IsListening() noexcept
        {
            return s_Listening;
        }

        ctsUnsignedLongLong GetTransferSize() noexcept
        {
            return s_TransferSize;
        }

        ctsUnsignedLong GetMaxBufferSize() noexcept
        {
            return s_TransferSize;
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
    IOTaskAction s_TaskAction = IOTaskAction::None;
    ctsIOStatus s_IOStatus = ctsIOStatus::ContinueIo;

    ctsIOPattern::ctsIOPattern(unsigned long)
    {
        Logger::WriteMessage(L"ctsIOPattern::ctsIOPattern\n");
    }

    ctsIOPattern::~ctsIOPattern() noexcept
    {
        Logger::WriteMessage(L"ctsIOPattern::~ctsIOPattern\n");
    }

    ctsIOTask ctsIOPattern::initiate_io() noexcept
    {
        Logger::WriteMessage(L"ctsIOPattern::initiate_io\n");

        unsigned long pended_io = s_IOPended.load();
        unsigned long remaining_io = s_IOCount.load();

        ctsIOTask return_task;
        if (pended_io == 0 && remaining_io > 0) {
            return_task.ioAction = IOTaskAction::Send;
            return_task.time_offset_milliseconds = s_IOTimeOffset;
            ++s_IOPended;
        } else {
            return_task.ioAction = IOTaskAction::None;
            return_task.time_offset_milliseconds = 0;
        }
        return return_task;
    }

    ctsIOStatus ctsIOPattern::complete_io(const ctsIOTask& _task, unsigned long _bytes_transferred, unsigned long _status_code) noexcept
    {
        Assert::AreEqual(s_IOStatusCode, _status_code);
        Logger::WriteMessage(L"ctsIOPattern::complete_io\n");
        --s_IOPended;
        --s_IOCount;

        return s_IOStatus;
    }

    // test IO pattern for fakes for this test
    class ctsMediaStreamServerUnitTestIOPattern : public ctsIOPattern
    {
    public:
        // default the base class 1 recv buffer
        ctsMediaStreamServerUnitTestIOPattern() : ctsIOPattern(1)
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::ctsMediaStreamServerUnitTestIOPattern\n");
        }

        // none of these are called - required to be defined
        virtual void print_stats(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr) noexcept
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::print_stats\n");
            Assert::IsFalse(true);
        }
        virtual ctsIOTask next_task()
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::next_task\n");
            Assert::IsFalse(true);
            return ctsIOTask();
        }
        virtual ctsIOPatternProtocolError completed_task(const ctsIOTask&, unsigned long _current_transfer) noexcept
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::completed_task\n");
            Assert::IsFalse(true);
            return ctsIOPatternProtocolError::NoError;
        }
        virtual void start_stats() noexcept
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::start_stats\n");
            Assert::IsFalse(true);
        }
        virtual void end_stats() noexcept
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::end_stats\n");
            Assert::IsFalse(true);
        }
        virtual char* connection_id() noexcept
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
    ctsSocketState::~ctsSocketState()
    {
    }
    // ctsSocket fakes
    ctsSocket::ctsSocket(std::weak_ptr<ctsSocketState>)
    {
        this->pattern = std::make_shared<ctsMediaStreamServerUnitTestIOPattern>();
    }
    ctsSocket::~ctsSocket()
    {

    }
    void ctsSocket::set_socket(SOCKET _s) noexcept
    {
        this->socket = _s;
    }
    void ctsSocket::lock_socket() const noexcept
    {
    }
    void ctsSocket::unlock_socket() const noexcept
    {
    }
    void ctsSocket::complete_state(unsigned long) noexcept
    {
        ::SetEvent(s_RemovedSocketEvent);
    }

    std::shared_ptr<ctsIOPattern> ctsSocket::io_pattern() const noexcept
    {
        return this->pattern;
    }

    // one callout fake to ctsMediaStreamServerImpl
    void ctsMediaStreamServerImpl::remove_socket(const ctl::ctSockaddr&)
    {
    }
}
///
/// End of Fakes
///

using namespace ctsTraffic;
namespace ctsUnitTest {
    TEST_CLASS(ctsMediaStreamServerConnectedSocketUnitTest)
    {
    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            WSADATA wsadata;
            auto startup = ::WSAStartup(WINSOCK_VERSION, &wsadata);
            Assert::AreEqual(0, startup);

            s_RemovedSocketEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            Assert::IsNotNull(s_RemovedSocketEvent);

            ctsConfig::Settings = new ctsConfig::ctsConfigSettings;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            ::CloseHandle(s_RemovedSocketEvent);
            ::WSACleanup();
            delete ctsConfig::Settings;
        }

        TEST_METHOD(SingleIO)
        {
            s_IOCount = 1;
            s_IOStatus = ctsIOStatus::ContinueIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = IOTaskAction::None;
            s_IOTimeOffset = 0;
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->set_socket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&] (ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult 
            {
                ++callback_invoked;

                auto socket_guard(ctsGuardSocket(test_socket));
                SOCKET cts_socket = socket_guard.get();

                auto connected_socket_guard(ctsGuardSocket(_socket_object));
                SOCKET connected_socket = connected_socket_guard.get();

                Assert::AreEqual(test_addr[0], _socket_object->get_address());
                Assert::AreEqual(cts_socket, connected_socket);

                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.schedule_task(test_task);
            // not 'done' yet, just stopped sending for the time-being
            Assert::AreEqual(static_cast<DWORD>(WAIT_TIMEOUT), ::WaitForSingleObject(s_RemovedSocketEvent, 0));
            Assert::AreEqual(1UL, callback_invoked);
        }

        TEST_METHOD(MultipleIO)
        {
            s_IOCount = 10;
            s_IOStatus = ctsIOStatus::ContinueIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = IOTaskAction::None;
            s_IOTimeOffset = 0;
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->set_socket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&] (ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult 
            {
                ++callback_invoked;

                auto socket_guard(ctsGuardSocket(test_socket));
                SOCKET cts_socket = socket_guard.get();

                auto connected_socket_guard(ctsGuardSocket(_socket_object));
                SOCKET connected_socket = connected_socket_guard.get();

                Assert::AreEqual(test_addr[0], _socket_object->get_address());
                Assert::AreEqual(cts_socket, connected_socket);

                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.schedule_task(test_task);
            // not 'done' yet, just stopped sending for the time-being
            Assert::AreEqual(static_cast<DWORD>(WAIT_TIMEOUT), ::WaitForSingleObject(s_RemovedSocketEvent, 0));
            Assert::AreEqual(10UL, callback_invoked);
        }

        TEST_METHOD(MultipleScheduledIO)
        {
            s_IOCount = 10;
            s_IOStatus = ctsIOStatus::ContinueIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = IOTaskAction::None;
            s_IOTimeOffset = 100; // 100ms apart
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->set_socket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&] (ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult {
                ++callback_invoked;

                auto socket_guard(ctsGuardSocket(test_socket));
                SOCKET cts_socket = socket_guard.get();

                auto connected_socket_guard(ctsGuardSocket(_socket_object));
                SOCKET connected_socket = connected_socket_guard.get();

                Assert::AreEqual(test_addr[0], _socket_object->get_address());
                Assert::AreEqual(cts_socket, connected_socket);

                if (callback_invoked == 10) {
                    s_IOStatus = ctsIOStatus::CompletedIo;
                }
                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.schedule_task(test_task);
            // should complete within 1 second (a few ms after 900ms)
            Assert::AreEqual(WAIT_OBJECT_0, ::WaitForSingleObject(s_RemovedSocketEvent, 1000));
            Assert::AreEqual(10UL, callback_invoked);
        }

        TEST_METHOD(FailSingleIO)
        {
            // should fail the first one
            s_IOCount = 2;
            s_IOStatus = ctsIOStatus::FailedIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = IOTaskAction::None;
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->set_socket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&] (ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult 
            {
                ++callback_invoked;

                auto socket_guard(ctsGuardSocket(test_socket));
                SOCKET cts_socket = socket_guard.get();

                auto connected_socket_guard(ctsGuardSocket(_socket_object));
                SOCKET connected_socket = connected_socket_guard.get();

                Assert::AreEqual(test_addr[0], _socket_object->get_address());
                Assert::AreEqual(cts_socket, connected_socket);

                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.schedule_task(test_task);
            // 'done' since it failed
            Assert::AreEqual(WAIT_OBJECT_0, ::WaitForSingleObject(s_RemovedSocketEvent, 0));
            Assert::AreEqual(1UL, callback_invoked);
        }

        TEST_METHOD(FailAfterMultipleIO)
        {
            // will fail after 5
            s_IOCount = 10;
            s_IOStatus = ctsIOStatus::ContinueIo;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = IOTaskAction::None;
            s_IOTimeOffset = 100; // 100ms apart
            ::ResetEvent(s_RemovedSocketEvent);

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->set_socket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&] (ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult {
                ++callback_invoked;

                auto socket_guard(ctsGuardSocket(test_socket));
                SOCKET cts_socket = socket_guard.get();

                auto connected_socket_guard(ctsGuardSocket(_socket_object));
                SOCKET connected_socket = connected_socket_guard.get();

                Assert::AreEqual(test_addr[0], _socket_object->get_address());
                Assert::AreEqual(cts_socket, connected_socket);

                if (callback_invoked == 5) {
                    s_IOStatus = ctsIOStatus::FailedIo;
                }
                s_IOStatusCode = WSAENOBUFS;
                return wsIOResult(WSAENOBUFS);
            });

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            // directly scheduling the first task
            s_IOPended = 1;
            test_connected_socket.schedule_task(test_task);
            // should complete within 500ms - failing after 5 IO
            Assert::AreEqual(WAIT_OBJECT_0, ::WaitForSingleObject(s_RemovedSocketEvent, 500));
            Assert::AreEqual(5UL, callback_invoked);
        }
    };
}