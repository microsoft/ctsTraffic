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
inline std::wstring ToString<ctsTraffic::ctsTask>(const ctsTraffic::ctsTask& task)
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
inline std::wstring ToString<ctsTraffic::ctsTaskAction>(const ctsTraffic::ctsTaskAction& action)
{
    return ctsTraffic::ctsTask::PrintTaskAction(action);
}

template <>
inline std::wstring ToString<ctsTraffic::ctsIoStatus>(const ctsTraffic::ctsIoStatus& status)
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
bool g_IsListening = true;
ctsTraffic::ctsConfig::MediaStreamSettings g_MediaStreamSettings;

const uint32_t g_TestBufferLength = 4;
const uint32_t g_TestRecvBufferLength = 1024;
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

void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsTcpStatistics&) noexcept
{
}

void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsUdpStatistics&) noexcept
{
}

void PrintDebug(_In_z_ _Printf_format_string_ PCWSTR, ...) noexcept
{
}

void PrintException(const std::exception&) noexcept
{
}

void PrintJitterUpdate(const JitterFrameEntry&, const JitterFrameEntry&) noexcept
{
}

void PrintErrorInfo(_In_z_ _Printf_format_string_ PCWSTR, ...) noexcept
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
    return static_cast<float>(ctl::ctTimer::SnapQpcInMillis() - g_configSettings->StartTimeMilliseconds) / 1000.0f;
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
const std::string g_doneString("DONE");

TEST_CLASS(ctsIOPatternUnitTest_Server)
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
        g_MaxBufferSize = 1024;
        g_BufferSize = 1024;
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

    TEST_METHOD(TestBaseClass_SingleSuccessfulRecv_Server)
    {
        this->SetTestBaseClassDefaults(Server);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        // "recv" the correct bytes
        ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, 0));

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // wait for the FIN from the client
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(TestBaseClass_FailSendingConnectionId)
    {
        this->SetTestBaseClassDefaults(Server);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        const ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestBaseClass_FailRecv)
    {
        this->SetTestBaseClassDefaults(Server);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestServerBaseClass_FailFINAfterRecv)
    {
        this->SetTestBaseClassDefaults(Server);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        // "recv" the correct bytes
        ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, 0));

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // recv FIN from client
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 0, 1));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestServerBaseClass_TooManyBytesOnFINAfterSend)
    {
        this->SetTestBaseClassDefaults(Server);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        // "recv" the correct bytes
        ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, 0));

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // recv FIN from client
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 1, 0));
        Assert::AreEqual(static_cast<uint32_t>(c_statusErrorTooMuchDataTransferred), test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestServerBaseClass_TooManyBytesOnFINAfterRecv)
    {
        this->SetTestBaseClassDefaults(Server);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        // "recv" the correct bytes
        ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, 0));

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);
        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        // recv FIN from client
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, 1, 0));
        Assert::AreEqual(static_cast<uint32_t>(c_statusErrorTooMuchDataTransferred), test_pattern->GetLastPatternError());
    }

    TEST_METHOD(TestBaseClass_InvalidBytesOnRecv)
    {
        this->SetTestBaseClassDefaults(Server);

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());
        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());
        // not returning the correct bytes
        ::ZeroMemory(test_task.m_buffer, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(test_task, ctsUnitTest::ctsIOPatternUnitTest_Server::DefaultTransferSize, 0));
        Assert::AreEqual(static_cast<uint32_t>(c_statusErrorDataDidNotMatchBitPattern), test_pattern->GetLastPatternError());
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///
    /// PushServer
    ///
    ///
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    TEST_METHOD(PushServer_NotVerifyingBuffersNotUsingSharedBuffer)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::ServerSideShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = 1024;
        g_BufferSize = 1024;
        g_transferSize = 1024 * 10;
        g_IsListening = true;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

            ctsTask empty_task = test_pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

            // "recv" the correct bytes
            ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);

        ctsTask empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushServer_NotVerifyingBuffersNotUsingSharedBuffer_SmallRecvs)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::ServerSideShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = 2048;
        g_BufferSize = 2048;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = true;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 9; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength * 2, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

            ctsTask empty_task = test_pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

            // "recv" the correct bytes
            ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // the final recv is just 1024 bytes
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", 10, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

        ctsTask empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        // "recv" the correct bytes
        ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 1024, 0));

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushServer_VerifyingBuffersNotUsingSharedBuffer)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::ServerSideShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = true;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

            ctsTask empty_task = test_pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

            // "recv" the correct bytes
            ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);

        ctsTask empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushServer_VerifyingBuffersNotUsingSharedBuffer_SmallRecvs)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::ServerSideShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = 2048;
        g_BufferSize = 2048;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = true;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 9; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength * 2, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

            ctsTask empty_task = test_pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

            // "recv" the correct bytes
            ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // the final recv is just 1024 bytes
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", 10, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

        ctsTask empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        // "recv" the correct bytes
        ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 1024, 0));

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PushServer_NotVerifyingBuffersUsingSharedBuffer)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Push;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::ServerSideShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = true;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = true;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

            ctsTask empty_task = test_pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

            // "recv" the correct bytes
            ::memcpy(test_task.m_buffer, ctsIoPattern::AccessSharedBuffer() + test_task.m_expectedPatternOffset, test_task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);

        ctsTask empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }


    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///
    /// PullServer
    ///
    ///
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    TEST_METHOD(PullServer_NotVerifyingBuffersNotUsingSharedBuffer)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::ServerSideShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = true;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

            ctsTask empty_task = test_pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);

        ctsTask empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullServer_VerifyingBuffersNotUsingSharedBuffer)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::ServerSideShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = true;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

            ctsTask empty_task = test_pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);

        ctsTask empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }

    TEST_METHOD(PullServer_NotVerifyingBuffersUsingSharedBuffer)
    {
        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Pull;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::ServerSideShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = true;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = false;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = g_TestRecvBufferLength * 10;
        g_IsListening = true;

        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        ctsTask test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, ctsStatistics::ConnectionIdLength, 0));

        for (uint32_t io_count = 0; io_count < 10; ++io_count)
        {
            test_task = test_pattern->InitiateIo();
            Assert::AreEqual(g_TestRecvBufferLength, test_task.m_bufferLength);
            Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
            Logger::WriteMessage(wil::str_printf<std::wstring>(L"%u: %ws", io_count, ToString<ctsTraffic::ctsTask>(test_task).c_str()).c_str());

            ctsTask empty_task = test_pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

            Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, g_TestRecvBufferLength, 0));
        }

        // send server completion
        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, test_task.m_ioAction);
        Assert::AreEqual(g_TestBufferLength, test_task.m_bufferLength);

        ctsTask empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        char completion[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        ::memcpy_s(completion, 4, test_task.m_buffer + test_task.m_bufferOffset, 4);
        Assert::IsTrue(g_doneString == completion);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(test_task, 4, 0));

        test_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, test_task.m_ioAction);
        Logger::WriteMessage(ToString<ctsTraffic::ctsTask>(test_task).c_str());

        empty_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, empty_task.m_ioAction);

        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(test_task, 0, 0));
    }
};
}
