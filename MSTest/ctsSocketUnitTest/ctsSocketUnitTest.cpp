/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <SDKDDKVer.h>
#include "CppUnitTest.h"

#include <ctString.hpp>

#include "ctsSocket.h"
#include "ctsSocketState.h"
#include "ctsIOPattern.h"
#include "ctsWinsockLayer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{

    // Test writer must define specialization of ToString<const Q& q> types used in Assert

    template<> inline std::wstring ToString<shared_ptr<ctl::ctThreadIocp>>(const shared_ptr<ctl::ctThreadIocp>& _tp)
    {
        return ctl::ctString::ctFormatString(L"ctl::ctThreadIocp -> 0x%p", _tp.get());
    }

    template<> inline std::wstring ToString<ctl::ctSockaddr >(const ctl::ctSockaddr& _addr)
    {
        return _addr.WriteCompleteAddress();
    }
}


///
/// Fakes
///
namespace ctsTraffic
{
    shared_ptr<ctsIOPattern> ctsIOPattern::MakeIOPattern()
    {
        Logger::WriteMessage(L"ctsIOPattern::MakeIOPattern\n");
        return nullptr;
    }

    void ctsSocketState::complete_state(DWORD) noexcept
    {
        Logger::WriteMessage(L"ctsSocketState::complete_state\n");
    }

    wsIOResult ctsSetLingertoRSTSocket(SOCKET) noexcept
    {
        return wsIOResult();
    }

    namespace ctsConfig
    {
        ctsConfigSettings* Settings;

        void PrintDebug(PCWSTR _text, ...) noexcept
        {
            va_list args;
            va_start(args, _text);

            auto formatted(ctl::ctString::ctFormatStringVa(_text, args));
            Logger::WriteMessage(ctl::ctString::ctFormatString(L"PrintDebug: %ws\n", formatted.c_str()).c_str());

            va_end(args);
        }
        void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long) noexcept
        {
            Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(address, error)\n");
        }
        void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long, const ctsTcpStatistics&) noexcept
        {
            Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsTcpStatistics)\n");
        }
        void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long, const ctsUdpStatistics&) noexcept
        {
            Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsUdpStatistics)\n");
        }
        void PrintConnectionResults(unsigned long) noexcept
        {
            Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(error)\n");
        }
        void PrintErrorIfFailed(const wchar_t*, unsigned long _value) noexcept
        {
            Logger::WriteMessage(
                ctl::ctString::ctFormatString(L"ctsConfig::PrintErrorIfFailed(%u)", _value).c_str());
        }
        void PrintException(const std::exception& e) noexcept
        {
            Logger::WriteMessage(
                ctl::ctString::ctFormatString(L"ctsConfig::PrintException(%ws)",
                    ctl::ctString::ctFormatException(e).c_str()).c_str());
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
}
///
/// End of Fakes
///


using namespace ctsTraffic;

namespace ctsUnitTest
{
    TEST_CLASS(ctsSocketUnitTest)
    {
    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            WSADATA wsa;
            int startup = ::WSAStartup(WINSOCK_VERSION, &wsa);
            Assert::AreEqual(0, startup);

            ctsConfig::Settings = new ctsConfig::ctsConfigSettings;
        }
        TEST_CLASS_CLEANUP(Cleanup)
        {
            delete ctsConfig::Settings;
            ::WSACleanup();
        }

        TEST_METHOD(SocketGuardReturnsSocket)
        {
            auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            shared_ptr<ctsSocket> test(make_shared<ctsSocket>(default_socket_state_object));

            // set the socket
            test->set_socket(socket_value);

            // get the socket under lock
            auto socket_guard(test->socket_reference());
            Assert::AreEqual(socket_value, socket_guard.socket());
        }

        TEST_METHOD(SocketGuardIsMovable)
        {
            auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            shared_ptr<ctsSocket> test(make_shared<ctsSocket>(default_socket_state_object));

            // set the socket
            test->set_socket(socket_value);

            // validate the object guard
            auto socket_guard(test->socket_reference());
            Assert::AreEqual(socket_value, socket_guard.socket());
        }

        TEST_METHOD(CloseSocket)
        {
            auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            shared_ptr<ctsSocket> test(make_shared<ctsSocket>(default_socket_state_object));

            test->set_socket(socket_value);
            {
                auto socket_guard(test->socket_reference());
                Assert::AreEqual(socket_value, socket_guard.socket());
            }

            test->close_socket();
            {
                auto socket_guard(test->socket_reference());
                Assert::AreEqual(INVALID_SOCKET, socket_guard.socket());
            }
        }

        TEST_METHOD(DtorClosesSocket)
        {
            auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            shared_ptr<ctsSocket> test(make_shared<ctsSocket>(default_socket_state_object));

            test->set_socket(socket_value);
            {
                auto socket_guard(test->socket_reference());
                Assert::AreEqual(socket_value, socket_guard.socket());
            }

            test.reset();

            // since can't directly tell if the socket was closed, as the ctsSocket object is now destroyed
            // - trying to use it should fail with an invalid socket error
            ctl::ctSockaddr local_addr(AF_INET, ctl::ctSockaddr::AddressType::Loopback);
            local_addr.SetPort(55555);
            auto error = ::bind(socket_value, local_addr.sockaddr(), local_addr.length());
            auto gle = ::WSAGetLastError();
            Assert::AreEqual(SOCKET_ERROR, error);
            Assert::AreEqual(static_cast<int>(WSAENOTSOCK), gle);
        }

        TEST_METHOD(ThreadPool)
        {
            auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            shared_ptr<ctsSocket> test(make_shared<ctsSocket>(default_socket_state_object));

            // when the socket is INVALID_SOCKET, should return a nullptr
            auto tp1(test->thread_pool());
            Assert::AreEqual(shared_ptr<ctl::ctThreadIocp>(nullptr), tp1);

            // once given a real socket, should return a valid TP handle
            test->set_socket(socket_value);
            auto tp2(test->thread_pool());
            Assert::AreNotEqual(shared_ptr<ctl::ctThreadIocp>(nullptr), tp2);
        }

        TEST_METHOD(LocalAddrs)
        {
            shared_ptr<ctsSocketState> default_socket_state_object;
            shared_ptr<ctsSocket> test(make_shared<ctsSocket>(default_socket_state_object));

            ctl::ctSockaddr test_address(AF_INET, ctl::ctSockaddr::AddressType::Loopback);
            test_address.SetPort(55555);

            test->set_local_address(test_address);
            Assert::AreEqual(test_address, test->local_address());
            Assert::AreNotEqual(test->local_address(), test->target_address());
        }

        TEST_METHOD(TargetAddrs)
        {
            shared_ptr<ctsSocketState> default_socket_state_object;
            shared_ptr<ctsSocket> test(make_shared<ctsSocket>(default_socket_state_object));

            ctl::ctSockaddr test_address(AF_INET, ctl::ctSockaddr::AddressType::Loopback);
            test_address.SetPort(55555);

            test->set_target_address(test_address);
            Assert::AreEqual(test_address, test->target_address());
            Assert::AreNotEqual(test->target_address(), test->local_address());
        }

        TEST_METHOD(IOCounters)
        {
            shared_ptr<ctsSocketState> default_socket_state_object;
            shared_ptr<ctsSocket> test(make_shared<ctsSocket>(default_socket_state_object));

            Logger::WriteMessage(L"Incrementing to 1\n");
            Assert::AreEqual(1L, test->increment_io());
            Assert::AreEqual(1L, test->pended_io());

            Logger::WriteMessage(L"Incrementing to 2\n");
            Assert::AreEqual(2L, test->increment_io());
            Assert::AreEqual(2L, test->pended_io());

            Logger::WriteMessage(L"Decrementing to 1\n");
            Assert::AreEqual(1L, test->decrement_io());
            Assert::AreEqual(1L, test->pended_io());

            Logger::WriteMessage(L"Decrementing to 0\n");
            Assert::AreEqual(0L, test->decrement_io());
            Assert::AreEqual(0L, test->pended_io());

            // todo: not sure how to validate going below 0 invokes fail-fast
        }

    private:
        SOCKET create_socket() const
        {
            // create a valid UDP socket
            SOCKET socket_value(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
            auto gle = ::WSAGetLastError();
            Logger::WriteMessage(ctl::ctString::ctFormatString(L"Created SOCKET value 0x%x (gle %d)\n", socket_value, gle).c_str());
            Assert::AreNotEqual(INVALID_SOCKET, socket_value);

            return socket_value;
        }
    };
}