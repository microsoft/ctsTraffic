/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <sdkddkver.h>
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

    template <>
    inline std::wstring ToString<shared_ptr<ctl::ctThreadIocp>>(const shared_ptr<ctl::ctThreadIocp>& _tp)
    {
        return wil::str_printf<std::wstring>(L"ctl::ctThreadIocp -> 0x%p", _tp.get());
    }

    template <>
    inline std::wstring ToString<socket_address>(const socket_address& _addr)
    {
        return _addr.write_complete_address();
    }
}


///
/// Fakes
///
namespace ctsTraffic
{
    shared_ptr<ctsIoPattern> ctsIoPattern::MakeIoPattern()
    {
        Logger::WriteMessage(L"ctsIOPattern::MakeIOPattern\n");
        return nullptr;
    }

    [[nodiscard]] wil::cs_leave_scope_exit ctsIoPattern::AcquireIoPatternLock() const noexcept
    {
        return {};
    }

    void ctsSocketState::CompleteState(DWORD) noexcept
    {
        Logger::WriteMessage(L"ctsSocketState::complete_state\n");
    }

    wsIOResult ctsSetLingerToResetSocket(SOCKET) noexcept
    {
        return wsIOResult();
    }

    namespace ctsConfig
    {
        ctsConfigSettings* g_configSettings;

        void PrintDebug(PCWSTR _text, ...) noexcept
        {
            va_list args;
            va_start(args, _text);
            std::wstring outputString;
            wil::details::str_vprintf_nothrow<std::wstring>(outputString, _text, args);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"PrintDebug: %ws\n", outputString.c_str()).c_str());

            va_end(args);
        }

        void PrintConnectionResults(const socket_address&, const socket_address&, uint32_t) noexcept
        {
            Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(address, error)\n");
        }

        void PrintConnectionResults(const socket_address&, const socket_address&, uint32_t,
                                    const ctsTcpStatistics&) noexcept
        {
            Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsTcpStatistics)\n");
        }

        void PrintConnectionResults(const socket_address&, const socket_address&, uint32_t,
                                    const ctsUdpStatistics&) noexcept
        {
            Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsUdpStatistics)\n");
        }

        void PrintConnectionResults(uint32_t) noexcept
        {
            Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(error)\n");
        }

        void PrintErrorIfFailed(_In_ PCSTR _text, uint32_t _why) noexcept
        {
            Logger::WriteMessage(
                wil::str_printf<std::wstring>(L"ctsConfig::PrintErrorIfFailed(%hs, %u)", _text, _why).c_str());
        }

        DWORD PrintThrownException() noexcept
        {
            try
            {
                throw;
            }
            catch (const wil::ResultException& e)
            {
                Logger::WriteMessage(
                    wil::str_printf<std::wstring>(L"ctsConfig::PrintException(%hs)",
                                                  e.what()).c_str());
                return Win32FromHresult(e.GetErrorCode());
            }
            catch (const std::exception& e)
            {
                Logger::WriteMessage(
                    wil::str_printf<std::wstring>(L"ctsConfig::PrintException(%hs)",
                                                  e.what()).c_str());
                return WSAENOBUFS;
            }
            catch (...)
            {
                FAIL_FAST();
            }
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
            const int startup = WSAStartup(WINSOCK_VERSION, &wsa);
            Assert::AreEqual(0, startup);

            ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            delete ctsConfig::g_configSettings;
            WSACleanup();
        }

        TEST_METHOD(SocketGuardReturnsSocket)
        {
            const auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            const auto test(make_shared<ctsSocket>(default_socket_state_object));

            // set the socket
            test->SetSocket(socket_value);

            // get the socket under lock
            const auto socket_guard(test->AcquireSocketLock());
            Assert::AreEqual(socket_value, socket_guard.GetSocket());
        }

        TEST_METHOD(SocketGuardIsMovable)
        {
            const auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            const auto test(make_shared<ctsSocket>(default_socket_state_object));

            // set the socket
            test->SetSocket(socket_value);

            // validate the object guard
            const auto socket_guard(test->AcquireSocketLock());
            Assert::AreEqual(socket_value, socket_guard.GetSocket());
        }

        TEST_METHOD(CloseSocket)
        {
            const auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            const auto test(make_shared<ctsSocket>(default_socket_state_object));

            test->SetSocket(socket_value);
            {
                const auto socket_guard(test->AcquireSocketLock());
                Assert::AreEqual(socket_value, socket_guard.GetSocket());
            }

            test->CloseSocket();
            {
                const auto socket_guard(test->AcquireSocketLock());
                Assert::AreEqual(INVALID_SOCKET, socket_guard.GetSocket());
            }
        }

        TEST_METHOD(DtorClosesSocket)
        {
            const auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            auto test(make_shared<ctsSocket>(default_socket_state_object));

            test->SetSocket(socket_value);
            {
                const auto socket_guard(test->AcquireSocketLock());
                Assert::AreEqual(socket_value, socket_guard.GetSocket());
            }

            test.reset();

            // since can't directly tell if the socket was closed, as the ctsSocket object is now destroyed
            // - trying to use it should fail with an invalid socket error
            socket_address local_addr(AF_INET);
            local_addr.set_address_loopback();
            local_addr.set_port(55555);
            const auto error = ::bind(socket_value, local_addr.sockaddr(), socket_address::length);
            const auto gle = WSAGetLastError();
            Assert::AreEqual(SOCKET_ERROR, error);
            Assert::AreEqual(static_cast<int>(WSAENOTSOCK), gle);
        }

        TEST_METHOD(ThreadPool)
        {
            const auto socket_value(this->create_socket());

            shared_ptr<ctsSocketState> default_socket_state_object;
            const auto test(make_shared<ctsSocket>(default_socket_state_object));

            // when the socket is INVALID_SOCKET, should return a nullptr
            const auto tp1(test->GetIocpThreadpool());
            Assert::AreEqual(shared_ptr<ctl::ctThreadIocp>(nullptr), tp1);

            // once given a real socket, should return a valid TP handle
            test->SetSocket(socket_value);
            const auto tp2(test->GetIocpThreadpool());
            Assert::AreNotEqual(shared_ptr<ctl::ctThreadIocp>(nullptr), tp2);
        }

        TEST_METHOD(LocalAddrs)
        {
            shared_ptr<ctsSocketState> default_socket_state_object;
            const auto test(make_shared<ctsSocket>(default_socket_state_object));

            socket_address test_address(AF_INET);
            test_address.set_address_loopback();
            test_address.set_port(55555);

            test->SetLocalSockaddr(test_address);
            Assert::AreEqual(test_address, test->GetLocalSockaddr());
            Assert::AreNotEqual(test->GetRemoteSockaddr(), test->GetLocalSockaddr());
        }

        TEST_METHOD(TargetAddrs)
        {
            shared_ptr<ctsSocketState> default_socket_state_object;
            const auto test(make_shared<ctsSocket>(default_socket_state_object));

            socket_address test_address(AF_INET);
            test_address.set_address_loopback();
            test_address.set_port(55555);

            test->SetLocalSockaddr(test_address);
            Assert::AreEqual(test_address, test->GetLocalSockaddr());
            Assert::AreNotEqual(test->GetRemoteSockaddr(), test->GetLocalSockaddr());
        }

        TEST_METHOD(IOCounters)
        {
            shared_ptr<ctsSocketState> default_socket_state_object;
            const auto test(make_shared<ctsSocket>(default_socket_state_object));

            Logger::WriteMessage(L"Incrementing to 1\n");
            Assert::AreEqual(1, test->IncrementIo());
            Assert::AreEqual(1, test->GetPendedIoCount());

            Logger::WriteMessage(L"Incrementing to 2\n");
            Assert::AreEqual(2, test->IncrementIo());
            Assert::AreEqual(2, test->GetPendedIoCount());

            Logger::WriteMessage(L"Decrementing to 1\n");
            Assert::AreEqual(1, test->DecrementIo());
            Assert::AreEqual(1, test->GetPendedIoCount());

            Logger::WriteMessage(L"Decrementing to 0\n");
            Assert::AreEqual(0, test->DecrementIo());
            Assert::AreEqual(0, test->GetPendedIoCount());

            // todo: not sure how to validate going below 0 invokes fail-fast
        }

    private:
        [[nodiscard]] SOCKET create_socket() const
        {
            // create a valid UDP socket
            const SOCKET socket_value(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
            const auto gle = WSAGetLastError();
            Logger::WriteMessage(
                wil::str_printf<std::wstring>(L"Created SOCKET value 0x%x (gle %d)\n", socket_value, gle).c_str());
            Assert::AreNotEqual(INVALID_SOCKET, socket_value);

            return socket_value;
        }
    };
}
