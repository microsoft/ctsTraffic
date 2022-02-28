/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <sdkddkver.h>
#include "CppUnitTest.h"

#include <Windows.h>
#include <ctString.hpp>
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsSocketState.h"
#include "ctsSocketBroker.h"
#include "ctsWinsockLayer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;

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

wsIOResult ctsSetLingertoResetSocket(SOCKET) noexcept
{
    return wsIOResult();
}

namespace ctsConfig
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

    void PrintConnectionResults(const ctl::ctSockaddr&, uint32_t) noexcept
    {
        Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(address, error)\n");
    }

    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsTcpStatistics&) noexcept
    {
        Logger::WriteMessage(L"ctsConfig::PrintConnectionResults(ctsTcpStatistics)\n");
    }

    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsUdpStatistics&) noexcept
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

    bool IsListening() noexcept
    {
        return false;
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

/// ctsSocketBroker stubs - when ctsSocketState calls out to update the broker
void ctsSocketBroker::InitiatingIo() noexcept
{
}

void ctsSocketBroker::Closing(bool) noexcept
{
}

[[nodiscard]] wil::cs_leave_scope_exit ctsIoPattern::AcquireIoPatternLock() const noexcept
{
    return {};
}
}

///
/// End of Fakes
///


using namespace ctsTraffic;

static long g_CallbackCount = 0L;

static DWORD g_CreateReturnCode = 0UL;
static DWORD g_ConnectReturnCode = 0UL;
static DWORD g_IOReturnCode = 0UL;
static DWORD g_ShouldNeverHitErrorCode = 0xffffffffUL;

void ResetStatics(DWORD _create = g_ShouldNeverHitErrorCode, DWORD _connect = g_ShouldNeverHitErrorCode, DWORD _io = g_ShouldNeverHitErrorCode)
{
    g_CallbackCount = 0L;
    g_CreateReturnCode = _create;
    g_ConnectReturnCode = _connect;
    g_IOReturnCode = _io;
}

void CreateFunctionHook(std::weak_ptr<ctsSocket> socket) noexcept
{
    const auto shared_socket(socket.lock());
    Assert::IsNotNull(shared_socket.get());

    Assert::AreNotEqual(g_ShouldNeverHitErrorCode, g_CreateReturnCode);

    ctl::ctMemoryGuardIncrement(&g_CallbackCount);
    if (shared_socket)
    {
        shared_socket->CompleteState(g_CreateReturnCode);
    }
}

void ConnectFunctionHook(std::weak_ptr<ctsSocket> socket) noexcept
{
    const auto shared_socket(socket.lock());
    Assert::IsNotNull(shared_socket.get());

    Assert::AreNotEqual(g_ShouldNeverHitErrorCode, g_ConnectReturnCode);

    const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Assert::AreNotEqual(INVALID_SOCKET, s);
    shared_socket->SetSocket(s);

    ctl::ctMemoryGuardIncrement(&g_CallbackCount);
    if (shared_socket)
    {
        shared_socket->CompleteState(g_ConnectReturnCode);
    }
}

void IoFunctionHook(std::weak_ptr<ctsSocket> socket) noexcept
{
    const auto shared_socket(socket.lock());
    Assert::IsNotNull(shared_socket.get());

    Assert::AreNotEqual(g_ShouldNeverHitErrorCode, g_IOReturnCode);

    ctl::ctMemoryGuardIncrement(&g_CallbackCount);
    if (shared_socket)
    {
        shared_socket->CompleteState(g_IOReturnCode);
    }
}


namespace ctsUnitTest
{
TEST_CLASS(ctsSocketStateUnitTest)
{
public:
    TEST_CLASS_INITIALIZE(Setup)
    {
        WSADATA wsadata;
        const int wsError = WSAStartup(WINSOCK_VERSION, &wsadata);
        Assert::AreEqual(0, wsError);

        ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;
        ctsConfig::g_configSettings->CreateFunction = CreateFunctionHook;
        ctsConfig::g_configSettings->ConnectFunction = ConnectFunctionHook;
        ctsConfig::g_configSettings->IoFunction = IoFunctionHook;
    }

    TEST_CLASS_CLEANUP(Cleanup)
    {
        delete ctsConfig::g_configSettings;
        WSACleanup();
    }

    TEST_METHOD(AllIOSucceed)
    {
        // expect all to pass
        ResetStatics(0, 0, 0);

        const auto test(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
        test->Start();

        do
        {
            Sleep(100);
        }
        while (ctsSocketState::InternalState::Closed != test->GetCurrentState());

        Assert::AreEqual(3L, ctl::ctMemoryGuardRead(&g_CallbackCount));
    }

    TEST_METHOD(CreateFails)
    {
        // create should fail, the others never invoked
        ResetStatics(1);

        const auto test(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
        test->Start();

        do
        {
            Sleep(100);
        }
        while (ctsSocketState::InternalState::Closed != test->GetCurrentState());

        Assert::AreEqual(1L, ctl::ctMemoryGuardRead(&g_CallbackCount));
    }

    TEST_METHOD(ConnectFails)
    {
        // connect should fail, IO should never invoked
        ResetStatics(0, 1);

        const auto test(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
        test->Start();

        do
        {
            Sleep(100);
        }
        while (ctsSocketState::InternalState::Closed != test->GetCurrentState());

        Assert::AreEqual(2L, ctl::ctMemoryGuardRead(&g_CallbackCount));
    }

    TEST_METHOD(IOFails)
    {
        // IO should fail
        ResetStatics(0, 0, 1);

        const auto test(std::make_shared<ctsSocketState>(std::weak_ptr<ctsSocketBroker>()));
        test->Start();

        do
        {
            Sleep(100);
        }
        while (ctsSocketState::InternalState::Closed != test->GetCurrentState());

        Assert::AreEqual(3L, ctl::ctMemoryGuardRead(&g_CallbackCount));
    }
};
}
