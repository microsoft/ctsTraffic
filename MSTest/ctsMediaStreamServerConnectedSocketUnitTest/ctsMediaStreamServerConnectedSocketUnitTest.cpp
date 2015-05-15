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

#include <ctString.hpp>
#include <ctSockaddr.hpp>

#include "ctsSafeInt.hpp"
#include "ctsSocket.h"
#include "ctsSocketState.h"

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

        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error) throw()
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsTcpStatistics& _stats) throw()
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsUdpStatistics& _stats) throw()
        {
        }
        void PrintDebug(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) throw()
        {
        }
        void PrintException(const std::exception& e) throw()
        {
        }
        void PrintJitterUpdate(long long _sequence_number, long long _sender_qpc, long long _sender_qpf, long long _recevier_qpc, long long _receiver_qpf) throw()
        {
        }
        void PrintErrorInfo(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) throw()
        {
        }

        bool IsListening() throw()
        {
            return s_Listening;
        }

        ctsUnsignedLongLong GetTransferSize() throw()
        {
            return s_TransferSize;
        }

        float GetStatusTimeStamp() NOEXCEPT
        {
            return 0.0f;
        }
    }

    bool s_RemovedSocket = false;
    unsigned long s_IOStatusCode = ERROR_SUCCESS;
    unsigned long s_IOCount = 0;
    IOTaskAction s_TaskAction = IOTaskAction::None;
    ctsIOStatus s_IOStatus = ctsIOStatus::ContinueIo;

    ctsIOPattern::ctsIOPattern(unsigned long)
    {
        Logger::WriteMessage(L"ctsIOPattern::ctsIOPattern\n");
    }

    ctsIOPattern::~ctsIOPattern()
    {
        Logger::WriteMessage(L"ctsIOPattern::~ctsIOPattern\n");
    }

    ctsIOTask ctsIOPattern::initiate_io() NOEXCEPT
    {
        Logger::WriteMessage(L"ctsIOPattern::initiate_io\n");

        ctsIOTask return_task;
        if (s_IOCount > 0) {
            return_task.ioAction = IOTaskAction::Send;
        } else {
            return_task.ioAction = IOTaskAction::None;
        }
        return return_task;
    }

    ctsIOStatus ctsIOPattern::complete_io(const ctsIOTask& _task, unsigned long _bytes_transferred, unsigned long _status_code) NOEXCEPT
    {
        Assert::AreEqual(s_IOStatusCode, _status_code);
        Logger::WriteMessage(L"ctsIOPattern::complete_io\n");
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
        virtual void print_stats(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr) NOEXCEPT
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
        virtual ctsIOPatternProtocolError completed_task(const ctsIOTask&, unsigned long _current_transfer) NOEXCEPT
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::completed_task\n");
            Assert::IsFalse(true);
            return ctsIOPatternProtocolError::NoError;
        }
        virtual void start_stats() NOEXCEPT
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::start_stats\n");
            Assert::IsFalse(true);
        }
        virtual void end_stats() NOEXCEPT
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::end_stats\n");
            Assert::IsFalse(true);
        }
        virtual char* connection_id() NOEXCEPT
        {
            Logger::WriteMessage(L"ctsMediaStreamServerUnitTestIOPattern::connection_id\n");
            Assert::IsFalse(true);
            return nullptr;
        }

    };

    // ctsSocketState fakes
    ctsSocketState::ctsSocketState(ctsSocketBroker*)
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
    void ctsSocket::set_socket(SOCKET _s)
    {
        this->socket = _s;
    }
    void ctsSocket::lock_socket() const
    {
    }
    void ctsSocket::unlock_socket() const
    {
    }
    void ctsSocket::complete_state(unsigned long)
    {
    }

    std::shared_ptr<ctsIOPattern> ctsSocket::io_pattern() const
    {
        return this->pattern;
    }

    // one callout fake to ctsMediaStreamServerImpl
    void ctsMediaStreamServerImpl::remove_socket(const ctl::ctSockaddr& _target_addr, unsigned long _error_code)
    {
        s_RemovedSocket = true;
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

            ctsConfig::Settings = new ctsConfig::ctsConfigSettings;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            ::WSACleanup();
            delete ctsConfig::Settings;
        }

        TEST_METHOD(SingleIO)
        {
            s_IOCount = 1;
            s_IOStatus = ctsIOStatus::ContinueIo;
            s_RemovedSocket = false;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = IOTaskAction::None;

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(nullptr));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->set_socket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&] (ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult 
            {
                --s_IOCount;
                ++callback_invoked;

                ctsSocketGuard socket_guard(ctsSocket::LockSocket(test_socket));
                SOCKET cts_socket = socket_guard.get();

                SOCKET connected_socket = _socket_object->socket_lock();
                _socket_object->socket_release();

                Assert::AreEqual(test_addr[0], _socket_object->get_address());
                Assert::AreEqual(cts_socket, connected_socket);

                s_IOStatusCode = WSAENOBUFS;
                return WSAENOBUFS;
            });

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            test_connected_socket.schedule_task(test_task);
            // should invoke immediately
            Assert::AreEqual(1UL, callback_invoked);
        }

        TEST_METHOD(MultipleIO)
        {
            s_IOCount = 10;
            s_IOStatus = ctsIOStatus::ContinueIo;
            s_RemovedSocket = false;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = IOTaskAction::None;

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(nullptr));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->set_socket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&] (ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult 
            {
                --s_IOCount;
                ++callback_invoked;

                ctsSocketGuard socket_guard(ctsSocket::LockSocket(test_socket));
                SOCKET cts_socket = socket_guard.get();

                SOCKET connected_socket = _socket_object->socket_lock();
                _socket_object->socket_release();

                Assert::AreEqual(test_addr[0], _socket_object->get_address());
                Assert::AreEqual(cts_socket, connected_socket);

                s_IOStatusCode = WSAENOBUFS;
                return WSAENOBUFS;
            });

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            test_connected_socket.schedule_task(test_task);
            // should invoke immediately
            Assert::AreEqual(10UL, callback_invoked);
        }

        TEST_METHOD(FailSingleIO)
        {
            // should fail the first one
            s_IOCount = 2;
            s_IOStatus = ctsIOStatus::FailedIo;
            s_RemovedSocket = false;
            s_IOStatusCode = ERROR_SUCCESS;
            s_TaskAction = IOTaskAction::None;

            std::vector<ctl::ctSockaddr> test_addr(ctl::ctSockaddr::ResolveName(L"1.1.1.1"));
            Assert::AreEqual(static_cast<size_t>(1), test_addr.size());

            std::shared_ptr<ctsSocketState> socket_state(std::make_shared<ctsSocketState>(nullptr));
            std::shared_ptr<ctsSocket> test_socket(std::make_shared<ctsSocket>(socket_state));
            test_socket->set_socket(INVALID_SOCKET);

            unsigned long callback_invoked = 0;
            ctsMediaStreamServerConnectedSocket test_connected_socket(
                std::weak_ptr<ctsSocket>(test_socket),
                INVALID_SOCKET,
                test_addr[0],
                [&] (ctsMediaStreamServerConnectedSocket* _socket_object) -> wsIOResult 
            {
                --s_IOCount;
                ++callback_invoked;

                ctsSocketGuard socket_guard(ctsSocket::LockSocket(test_socket));
                SOCKET cts_socket = socket_guard.get();

                SOCKET connected_socket = _socket_object->socket_lock();
                _socket_object->socket_release();

                Assert::AreEqual(test_addr[0], _socket_object->get_address());
                Assert::AreEqual(cts_socket, connected_socket);

                s_IOStatusCode = WSAENOBUFS;
                return WSAENOBUFS;
            });

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            test_connected_socket.schedule_task(test_task);
            // should invoke immediately
            Assert::AreEqual(1UL, callback_invoked);
        }
    };
}