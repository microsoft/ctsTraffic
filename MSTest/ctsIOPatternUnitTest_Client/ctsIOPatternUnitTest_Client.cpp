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
#include <memory>
// OS headers
#include <Windows.h>
// ctl headers
#include <ctTimer.hpp>
#include <ctString.hpp>
// project headers
#include "ctsIOTask.hpp"
#include "ctsConfig.h"
#include "ctsIOPattern.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{
// Test writer must define specialization of ToString<const Q& q> types used in Assert
template <>
std::wstring ToString<ctsTraffic::ctsTask>(const ctsTraffic::ctsTask& task)
{
    return wil::str_printf<std::wstring>(
        L"ctsIOTask:\n"
        L"\tbuffer: 0x%p\n"
        L"\tbuffer_length: %u\n"
        L"\tbuffer_offset: %u\n"
        L"\texpected_pattern_offset: %u\n"
        L"\tioAction: %ws\n"
        L"\trio_bufferid: %p\n"
        L"\ttime_offset_milliseconds: %lld\n"
        L"\tverify_io: %ws\n",
        task.m_buffer,
        task.m_bufferLength,
        task.m_bufferOffset,
        task.m_expectedPatternOffset,
        ctsTraffic::ctsTask::PrintTaskAction(task.m_ioAction),
        task.m_rioBufferid,
        task.m_timeOffsetMilliseconds,
        task.m_trackIo ? L"true" : L"false");
}

template <>
std::wstring ToString<ctsTraffic::ctsTaskAction>(const ctsTraffic::ctsTaskAction& action)
{
    return ctsTraffic::ctsTask::PrintTaskAction(action);
}

template <>
std::wstring ToString<ctsTraffic::ctsIoStatus>(const ctsTraffic::ctsIoStatus& status)
{
    switch (status)
    {
        case ctsTraffic::ctsIoStatus::ContinueIo:
            return L"ContinueIo";
        case ctsTraffic::ctsIoStatus::CompletedIo:
            return L"CompletedIo";
        case ctsTraffic::ctsIoStatus::FailedIo:
            return L"FailedIo";
    }
    return L"Unknown_ctsIOStatus";
}
}


///
/// statics to return in the Fakes
///
int64_t g_tcpBytesPerSecond = 0LL;
uint32_t g_MaxBufferSize = 0UL;
uint32_t g_BufferSize = 0UL;
uint64_t g_transferSize = 0ULL;
bool g_IsListening = false;
ctsTraffic::ctsConfig::MediaStreamSettings g_MediaStreamSettings;
const uint32_t g_TestRecvBufferLength = 1024;
const uint32_t g_TestBufferLength = 4;
const uint32_t g_TestErrorCode = 1;

///
/// Fakes
///
namespace ctsTraffic::ctsConfig
{
ctsConfigSettings* g_configSettings;

void PrintConnectionResults(uint32_t) noexcept
{
}

void PrintConnectionResults(const wil::networking::socket_address&, const wil::networking::socket_address&, uint32_t, const ctsTcpStatistics&) noexcept
{
}

void PrintConnectionResults(const wil::networking::socket_address&, const wil::networking::socket_address&, uint32_t, const ctsUdpStatistics&) noexcept
{
}

void PrintDebug(_In_ _Printf_format_string_ PCWSTR, ...) noexcept
{
}

void PrintException(const std::exception&) noexcept
{
}

void PrintJitterUpdate(const JitterFrameEntry&, const JitterFrameEntry&) noexcept
{
}

void PrintErrorInfo(_In_ _Printf_format_string_ PCWSTR, ...) noexcept
{
}

void PrintTcpDetails(const wil::networking::socket_address&, const wil::networking::socket_address&, SOCKET, const ctsTcpStatistics&) noexcept
{
}

bool IsListening() noexcept
{
    return g_IsListening;
}


const MediaStreamSettings& GetMediaStream() noexcept
{
    return g_MediaStreamSettings;
}

int64_t GetTcpBytesPerSecond() noexcept
{
    return g_tcpBytesPerSecond;
}

uint32_t GetMaxBufferSize() noexcept
{
    return g_MaxBufferSize;
}

uint32_t GetMinBufferSize() noexcept
{
    return g_BufferSize;
}

uint32_t GetBufferSize() noexcept
{
    return g_BufferSize;
}

uint64_t GetTransferSize() noexcept
{
    return g_transferSize;
}

float GetStatusTimeStamp() noexcept
{
    return static_cast<float>((ctl::ctTimer::snap_qpc_as_msec() - g_configSettings->StartTimeMilliseconds) / 1000.0);
}

bool ShutdownCalled() noexcept
{
    return false;
}

uint32_t ConsoleVerbosity() noexcept
{
    return 0;
}

TcpShutdownType GetShutdownType() noexcept
{
    return ctsConfig::g_configSettings->TcpShutdown;
}
}

///
/// End of Fakes
///

///
/// ctsIoPattern references these global ctsConfig::Settings
///   IoPattern
///   TcpBytesPerSecondPeriod
///   Protocol
///   UseSharedBuffer
///   ShouldVerifyBuffers
///   PrePostRecvs
///   PrePostSends
///   PushBytes
///   PullBytes
///
/// Must define these statics for returning relevant values to ctsIOPattern
///   ctsSignedLongLong g_TcpBytesPerSecond
///   uint32_t g_MaxBufferSize
///   uint32_t g_BufferSize
///   uint64_t g_TransferSize
///   bool g_IsListening
///

using namespace ctsTraffic;

namespace ctsUnitTest
{
TEST_CLASS(ctsIOPatternUnitTest_Client)
{
private:
    enum TestRole
    {
        Client,
        Server
    };

    enum TestShutdownMethod
    {
        Graceful,
        Hard
    };

    static const uint32_t DefaultTransferSize = 10UL;

    void SetTestBaseClassDefaults(TestRole _role, TestShutdownMethod _shutdown = Graceful) const
    {
        if (Server == _role && Hard == _shutdown)
        {
            Assert::Fail(L"Servers only support the default Graceful shutdown");
        }

        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        ctsConfig::g_configSettings->ConnectionLimit = 8;
        ctsConfig::g_configSettings->TcpShutdown = (Graceful == _shutdown) ? ctsConfig::TcpShutdownType::GracefulShutdown : ctsConfig::TcpShutdownType::HardShutdown;

        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = DefaultTransferSize;
        g_IsListening = (Server == _role);
    }

public:
    TEST_CLASS_INITIALIZE(Setup)
    {
        ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;

        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        ctsConfig::g_configSettings->ConnectionLimit = 8;
    }

    TEST_CLASS_CLEANUP(Cleanup)
    {
        delete ctsConfig::g_configSettings;
    }

    TEST_METHOD(TestBaseClass_SuccessfulSend)
    {
        this->SetTestBaseClassDefaults(Client, Graceful);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate graceful shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        // wait for the server's FIN
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(TestBaseClass_SuccessfulMultipleSends)
    {
        this->SetTestBaseClassDefaults(Client, Graceful);
        ctsConfig::g_configSettings->PrePostSends = 2;
        g_BufferSize = DefaultTransferSize;
        g_transferSize = DefaultTransferSize * 2;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task1 = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task1.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task1.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task1, ctsStatistics::ConnectionIdLength, 0));

        test_task1 = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task1.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task1.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task1).c_str());

        const ctsTask test_task2 = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task2.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task2.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task2).c_str());

        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task1, DefaultTransferSize, 0));
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task2, DefaultTransferSize, 0));

        // recv server completion
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate graceful shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        // wait for the server's FIN
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(TestBaseClass_SuccessfulSend_HardShutdown)
    {
        this->SetTestBaseClassDefaults(Client, Hard);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate hard shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(TestBaseClass_ReceivedNoBytesWithServerStatus)
    {
        this->SetTestBaseClassDefaults(Client);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // receive server status
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(TestBaseClass_FailedReceivingServerStatus)
    {
        this->SetTestBaseClassDefaults(Client);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
    }

    TEST_METHOD(TestBaseClass_FailSend)
    {
        this->SetTestBaseClassDefaults(Client);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestBaseClass_FailMultipleSends)
    {
        this->SetTestBaseClassDefaults(Client);
        ctsConfig::g_configSettings->PrePostSends = 2;
        g_BufferSize = DefaultTransferSize;
        g_transferSize = DefaultTransferSize * 2;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task1 = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task1.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task1.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task1, ctsStatistics::ConnectionIdLength, 0));

        test_task1 = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task1.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task1.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task1).c_str());

        const ctsTask test_task2 = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task2.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task2.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task2).c_str());

        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task1, DefaultTransferSize, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task2, DefaultTransferSize, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestBaseClass_FailReceivingConnectionId)
    {
        this->SetTestBaseClassDefaults(Client);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        const ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestBaseClass_FailGracefulShutdownAfterSend)
    {
        this->SetTestBaseClassDefaults(Client, Graceful);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate graceful shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestBaseClass_FailHardShutdownAfterSend)
    {
        this->SetTestBaseClassDefaults(Client, Hard);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate hard shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestBaseClass_FailGracefulShutdownAFterRecv)
    {
        this->SetTestBaseClassDefaults(Client, Graceful);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate graceful shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestBaseClass_FailHardShutdownAFterRecv)
    {
        this->SetTestBaseClassDefaults(Client, Hard);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate hard shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestBaseClass_FailFINAfterSend)
    {
        this->SetTestBaseClassDefaults(Client, Graceful);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate graceful shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        // recv final fin
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestClientBaseClass_FailFINAfterRecv)
    {
        this->SetTestBaseClassDefaults(Client);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate graceful shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        // wait for the final FIN
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestClientBaseClass_TooManyBytesOnFINAfterSend)
    {
        this->SetTestBaseClassDefaults(Client);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate graceful shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        // recv the final FIN
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 1, 0));
        Assert::AreEqual(static_cast<uint32_t>(c_statusErrorTooMuchDataTransferred), test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestClientBaseClass_TooManyBytesOnFINAfterRecv)
    {
        this->SetTestBaseClassDefaults(Client);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, DefaultTransferSize, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // initiate graceful shutdown
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        // recv the final FIN
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 1, 0));
        Assert::AreEqual(static_cast<uint32_t>(c_statusErrorTooMuchDataTransferred), test_pattern->GetLastPatternError());
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///
    /// PushClient
    ///
    ///
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    TEST_METHOD(PushClient_NotVerifyingBuffersNotUsingSharedBuffer_Graceful)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushClient_NotVerifyingBuffersNotUsingSharedBuffer_Rude)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushClient_VerifyingBuffersNotUsingSharedBuffer_Graceful)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushClient_VerifyingBuffersNotUsingSharedBuffer_Rude)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushClient_NotVerifyingBuffersUsingSharedBuffer_Graceful)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = true;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushClient_NotVerifyingBuffersUsingSharedBuffer_Rude)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = true;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushClient_MultipleSendsWithISBEnabled)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 0;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        // ISB should indicate to keep 2 sends in flight
        test_pattern->SetIdealSendBacklog(g_TestRecvBufferLength * 2);

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 5; ++io_count)
        {
            const ctsTask test_task_one = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task_one.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task_one.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task_one).c_str()).c_str());

            const ctsTask test_task_two = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task_two.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task_two.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task_two).c_str()).c_str());

            const ctsTask test_task_three = test_pattern->InitiateIo();
            const uint32_t ZeroBytes = 0;
            Assert::AreEqual(ZeroBytes, test_task_three.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::None, test_task_three.m_ioAction);

            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task_one, g_TestRecvBufferLength, 0));
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task_two, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushClient_MultipleSendsWithISBEnabledInterleaving)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 0;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        // ISB should indicate to keep 2 sends in flight
        test_pattern->SetIdealSendBacklog(g_TestRecvBufferLength * 2);

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", 0, ToString<ctsTask>(test_task).c_str()).c_str());

        for (uint32_t io_count = 1; io_count < 10; ++io_count)
        {
            const ctsTask test_task_one = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task_one.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task_one.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task_one).c_str()).c_str());

            const ctsTask test_task_three = test_pattern->InitiateIo();
            const uint32_t ZeroBytes = 0;
            Assert::AreEqual(ZeroBytes, test_task_three.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::None, test_task_three.m_ioAction);

            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task_one, g_TestRecvBufferLength, 0));
        }

        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushClient_LargeNumberOfSendsWithISBEnabled)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 0;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        // ISB should indicate to keep 2 sends in flight
        test_pattern->SetIdealSendBacklog(static_cast<uint32_t>(g_transferSize));

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        std::vector<ctsTask> pended_tasks;
        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());

            pended_tasks.push_back(test_task);
        }

        // all are now pended, next should be empty
        ctsTask testTaskEmpty = test_pattern->InitiateIo();
        const uint32_t ZeroBytes = 0;
        Assert::AreEqual(ZeroBytes, testTaskEmpty.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::None, testTaskEmpty.m_ioAction);

        for (uint32_t ioCount = 0; ioCount < 10; ++ioCount)
        {
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(pended_tasks[ioCount], g_TestRecvBufferLength, 0));

            // after the 10th completion, it will move to the below protocol
            if (ioCount < 9)
            {
                testTaskEmpty = test_pattern->InitiateIo();
                Assert::AreEqual(ZeroBytes, testTaskEmpty.m_bufferLength);
                Assert::AreEqual(ctsTaskAction::None, testTaskEmpty.m_ioAction);
            }
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushClient_OneSendInFlightWithISBEnabledWhenISBIsSmallerThanBufferSize)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 0;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr testPattern(ctsIoPattern::MakeIoPattern());
        // ISB should indicate to keep 1 send in flight because buffer is larger than ISB
        testPattern->SetIdealSendBacklog(g_TestRecvBufferLength / 2);

        ctsTask testTask = testPattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, testTask.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, testTask.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, testPattern->CompleteIo(testTask, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t ioCount = 0; ioCount < 10; ++ioCount)
        {
            const ctsTask testTaskLoop = testPattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, testTaskLoop.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, testTaskLoop.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", ioCount, ToString<ctsTask>(testTaskLoop).c_str()).c_str());

            const ctsTask testTaskLoopTwo = testPattern->InitiateIo();
            const uint32_t ZeroBytes = 0;
            Assert::AreEqual(ZeroBytes, testTaskLoopTwo.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::None, testTaskLoopTwo.m_ioAction);

            Assert::AreEqual(ctsIoStatus::ContinueIo, testPattern->CompleteIo(testTaskLoop, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        testTask = testPattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, testTask.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, testTask.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, testPattern->CompleteIo(testTask, 4, 0));

        testTask = testPattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, testTask.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(testTask).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, testPattern->CompleteIo(testTask, 0, 0));

        testTask = testPattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, testTask.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(testTask).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, testPattern->CompleteIo(testTask, 0, 0));
    }

    TEST_METHOD(PushClient_MultipleSendsWithISBEnabledOffsetFromBufferSize)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 0;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        // ISB should indicate to keep 2 sends in flight
        test_pattern->SetIdealSendBacklog(g_TestRecvBufferLength * 2 - 1);

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 5; ++io_count)
        {
            const ctsTask test_task_one = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task_one.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task_one.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task_one).c_str()).c_str());

            const ctsTask test_task_two = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task_two.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task_two.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task_two).c_str()).c_str());

            const ctsTask test_task_three = test_pattern->InitiateIo();
            const uint32_t ZeroBytes = 0;
            Assert::AreEqual(ZeroBytes, test_task_three.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::None, test_task_three.m_ioAction);

            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task_one, g_TestRecvBufferLength, 0));
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task_two, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///
    /// PullClient
    ///
    ///
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    TEST_METHOD(PullClient_NotVerifyingBuffersNotUsingSharedBuffer_Graceful)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullClient_NotVerifyingBuffersNotUsingSharedBuffer_Rude)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullClient_NotVerifyingBuffersNotUsingSharedBuffer_SmallRecvs_Graceful)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength * 2;
        g_BufferSize = g_TestRecvBufferLength * 2;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 9; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength * 2, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // the final recv is just 1024 bytes
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", 10, ToString<ctsTask>(test_task).c_str()).c_str());
        // "recv" the correct bytes
        memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullClient_NotVerifyingBuffersNotUsingSharedBuffer_SmallRecvs_Rude)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength * 2;
        g_BufferSize = g_TestRecvBufferLength * 2;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 9; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength * 2, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // the final recv is just 1024 bytes
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", 10, ToString<ctsTask>(test_task).c_str()).c_str());
        // "recv" the correct bytes
        memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullClient_VerifyingBuffersNotUsingSharedBuffer_Graceful)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullClient_VerifyingBuffersNotUsingSharedBuffer_Rude)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullClient_VerifyingBuffersNotUsingSharedBuffer_SmallRecvs_Graceful)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength * 2;
        g_BufferSize = g_TestRecvBufferLength * 2;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 9; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength * 2, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // the final recv is just 1024 bytes
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", 10, ToString<ctsTask>(test_task).c_str()).c_str());
        // "recv" the correct bytes
        memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullClient_VerifyingBuffersNotUsingSharedBuffer_SmallRecvs_Rude)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength * 2;
        g_BufferSize = g_TestRecvBufferLength * 2;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 9; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength * 2, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // the final recv is just 1024 bytes
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", 10, ToString<ctsTask>(test_task).c_str()).c_str());
        // "recv" the correct bytes
        memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullClient_NotVerifyingBuffersUsingSharedBuffer_Graceful)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = true;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 0, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullClient_NotVerifyingBuffersUsingSharedBuffer_Rude)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = true;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = false;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // recv server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::HardShutdown, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }
};
}
