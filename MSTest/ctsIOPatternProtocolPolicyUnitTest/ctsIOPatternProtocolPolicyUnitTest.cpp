/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// ReSharper disable CppInconsistentNaming
#include <sdkddkver.h>
#include "CppUnitTest.h"

#include <memory>

#include "ctsStatistics.hpp"
#include "ctsIOPatternProtocolPolicy.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{
template <>
inline std::wstring ToString<ctsTraffic::ctsIoPatternType>(const ctsTraffic::ctsIoPatternType& q)
{
    switch (q)
    {
        case ctsTraffic::ctsIoPatternType::NoIo:
            return L"NoIo";
        case ctsTraffic::ctsIoPatternType::SendConnectionGuid:
            return L"SendConnectionGuid";
        case ctsTraffic::ctsIoPatternType::RecvConnectionGuid:
            return L"RecvConnectionGuid";
        case ctsTraffic::ctsIoPatternType::MoreIo:
            return L"MoreIo";
        case ctsTraffic::ctsIoPatternType::SendCompletion:
            return L"SendCompletion";
        case ctsTraffic::ctsIoPatternType::RecvCompletion:
            return L"RecvCompletion";
        case ctsTraffic::ctsIoPatternType::GracefulShutdown:
            return L"GracefulShutdown";
        case ctsTraffic::ctsIoPatternType::HardShutdown:
            return L"HardShutdown";
        case ctsTraffic::ctsIoPatternType::RequestFin:
            return L"RequestFIN";
    }

    Assert::Fail(L"Unknown ctsIOPatternProtocolTask");
}

template <>
inline std::wstring ToString<ctsTraffic::ctsIoPatternError>(const ctsTraffic::ctsIoPatternError& q)
{
    switch (q)
    {
        case ctsTraffic::ctsIoPatternError::NotProtocolError:
            return L"NotProtocolError";
        case ctsTraffic::ctsIoPatternError::NoConnectionGuid:
            return L"NoConnectionGuid";
        case ctsTraffic::ctsIoPatternError::ZeroByteXfer:
            return L"ZeroByteXfer";
        case ctsTraffic::ctsIoPatternError::TooManyBytes:
            return L"TooManyBytes";
        case ctsTraffic::ctsIoPatternError::TooFewBytes:
            return L"TooFewBytes";
        case ctsTraffic::ctsIoPatternError::CorruptedXfer:
            return L"CorruptedXfer";
    }

    Assert::Fail(L"Unknown ctsIoPatternError");
}
}

uint64_t g_transferSize = 0ULL;
bool g_isListening = false;
const uint32_t g_TestErrorCode = 1;

///
/// Fakes
///
namespace ctsTraffic::ctsConfig
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

using namespace ctsTraffic;

namespace ctsUnitTest
{
TEST_CLASS(ctsIOPatternProtocolPolicyUnitTest)
{
private:
    uint32_t m_zero = 0UL;
    uint32_t m_testError = 1UL;

    [[nodiscard]] std::unique_ptr<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpClient>> InitClientGracefulShutdownTest(uint64_t testTransferSize) const
    {
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        g_isListening = false;
        g_transferSize = testTransferSize;

        auto returnPattern(std::make_unique<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpClient>>());
        Assert::IsFalse(returnPattern->IsCompleted());
        Assert::AreEqual(g_transferSize, returnPattern->GetMaxTransfer());
        Assert::AreEqual(g_transferSize, returnPattern->GetRemainingTransfer());
        return returnPattern;
    }

    [[nodiscard]] std::unique_ptr<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpServer>> InitServerGracefulShutdownTest(uint64_t testTransferSize) const
    {
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        g_isListening = true;
        g_transferSize = testTransferSize;

        auto returnPattern(std::make_unique<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpServer>>());
        Assert::IsFalse(returnPattern->IsCompleted());
        Assert::AreEqual(g_transferSize, returnPattern->GetMaxTransfer());
        Assert::AreEqual(g_transferSize, returnPattern->GetRemainingTransfer());
        return returnPattern;
    }

    [[nodiscard]] std::unique_ptr<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpClient>> InitClientHardShutdownTest(uint64_t testTransferSize) const
    {
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
        g_isListening = false; // client-only
        g_transferSize = testTransferSize;

        auto returnPattern(std::make_unique<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpClient>>());
        Assert::IsFalse(returnPattern->IsCompleted());
        Assert::AreEqual(g_transferSize, returnPattern->GetMaxTransfer());
        Assert::AreEqual(g_transferSize, returnPattern->GetRemainingTransfer());
        return returnPattern;
    }

    [[nodiscard]] std::unique_ptr<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolUdp>> InitUdpClientTest(uint64_t testTransferSize) const
    {
        g_isListening = false;
        g_transferSize = testTransferSize;

        auto returnPattern(std::make_unique<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolUdp>>());
        Assert::IsFalse(returnPattern->IsCompleted());
        Assert::AreEqual(g_transferSize, returnPattern->GetMaxTransfer());
        Assert::AreEqual(g_transferSize, returnPattern->GetRemainingTransfer());
        return returnPattern;
    }

    [[nodiscard]] std::unique_ptr<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolUdp>> InitUdpServerTest(uint64_t testTransferSize) const
    {
        g_isListening = true;
        g_transferSize = testTransferSize;

        auto returnPattern(std::make_unique<ctsIoPatternProtocolPolicy<ctsIoPatternProtocolUdp>>());
        Assert::IsFalse(returnPattern->IsCompleted());
        Assert::AreEqual(g_transferSize, returnPattern->GetMaxTransfer());
        Assert::AreEqual(g_transferSize, returnPattern->GetRemainingTransfer());
        return returnPattern;
    }

    //
    // Private members to implement building out a ctsIOTask for each task
    //
    template <typename IoPattern>
    ctsTask RequestConnectionGuid(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern)
    {
        auto task = ioPattern->GetNextPatternType();
        if (g_isListening)
        {
            Assert::AreEqual(ctsIoPatternType::SendConnectionGuid, task);
        }
        else
        {
            Assert::AreEqual(ctsIoPatternType::RecvConnectionGuid, task);
        }

        ctsTask testTask;
        if (g_isListening)
        {
            testTask.m_ioAction = ctsTaskAction::Send;
        }
        else
        {
            testTask.m_ioAction = ctsTaskAction::Recv;
        }
        testTask.m_trackIo = false;
        testTask.m_bufferLength = ctsStatistics::ConnectionIdLength;

        ioPattern->NotifyNextTask(testTask);
        Assert::IsFalse(ioPattern->IsCompleted());

        return testTask;
    }

    template <typename IoPattern>
    ctsTask RequestMoreIo(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern, uint32_t bufferLength)
    {
        auto task = ioPattern->GetNextPatternType();
        Assert::AreEqual(ctsIoPatternType::MoreIo, task);

        ctsTask testTask;
        testTask.m_ioAction = ctsTaskAction::Recv;
        testTask.m_trackIo = true;
        testTask.m_bufferLength = bufferLength;

        ioPattern->NotifyNextTask(testTask);
        Assert::IsFalse(ioPattern->IsCompleted());

        return testTask;
    }

    template <typename IoPattern>
    ctsTask RequestSendStatus(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern, _In_ uint32_t* statusBuffer)
    {
        // GetNextPatternType
        auto task = ioPattern->GetNextPatternType();
        Assert::AreEqual(ctsIoPatternType::SendCompletion, task);

        ctsTask testTask;
        testTask.m_ioAction = ctsTaskAction::Send;
        testTask.m_trackIo = false;
        testTask.m_buffer = reinterpret_cast<char*>(statusBuffer);
        testTask.m_bufferLength = 4;

        // NotifyNextTask
        ioPattern->NotifyNextTask(testTask);
        Assert::IsFalse(ioPattern->IsCompleted());

        // should return NoIO since we are waiting on this task
        VerifyNoMoreIo(ioPattern);

        return testTask;
    }

    template <typename IoPattern>
    ctsTask RequestRecvStatus(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern, _In_ uint32_t* statusBuffer)
    {
        // GetNextPatternType
        auto task = ioPattern->GetNextPatternType();
        Assert::AreEqual(ctsIoPatternType::RecvCompletion, task);

        ctsTask testTask;
        testTask.m_ioAction = ctsTaskAction::Recv;
        testTask.m_trackIo = false;
        testTask.m_buffer = reinterpret_cast<char*>(statusBuffer);
        testTask.m_bufferLength = 4;

        // NotifyNextTask
        ioPattern->NotifyNextTask(testTask);
        Assert::IsFalse(ioPattern->IsCompleted());

        // should return NoIO since we are waiting on this task
        VerifyNoMoreIo(ioPattern);

        return testTask;
    }

    template <typename IoPattern>
    ctsTask RequestFin(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern)
    {
        // GetNextPatternType
        auto task = ioPattern->GetNextPatternType();
        Assert::AreEqual(ctsIoPatternType::RequestFin, task);

        ctsTask testTask;
        testTask.m_ioAction = ctsTaskAction::Recv;
        testTask.m_trackIo = false;
        testTask.m_bufferLength = 16;

        // NotifyNextTask
        ioPattern->NotifyNextTask(testTask);
        Assert::IsFalse(ioPattern->IsCompleted());

        // should return NoIO since we are waiting on this task
        VerifyNoMoreIo(ioPattern);

        return testTask;
    }

    template <typename IoPattern>
    ctsTask RequestGracefulShutdown(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern)
    {
        // GetNextPatternType
        auto task = ioPattern->GetNextPatternType();
        Assert::AreEqual(ctsIoPatternType::GracefulShutdown, task);

        ctsTask testTask;
        testTask.m_ioAction = ctsTaskAction::GracefulShutdown;
        testTask.m_trackIo = false;
        testTask.m_bufferLength = 0;

        // NotifyNextTask
        ioPattern->NotifyNextTask(testTask);
        Assert::IsFalse(ioPattern->IsCompleted());

        // should return NoIO since we are waiting on this task
        VerifyNoMoreIo(ioPattern);

        return testTask;
    }

    template <typename IoPattern>
    ctsTask RequestHardShutdown(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern)
    {
        // GetNextPatternType
        auto task = ioPattern->GetNextPatternType();
        Assert::AreEqual(ctsIoPatternType::HardShutdown, task);

        ctsTask testTask;
        testTask.m_ioAction = ctsTaskAction::HardShutdown;
        testTask.m_trackIo = false;
        testTask.m_bufferLength = 0;

        // NotifyNextTask
        ioPattern->NotifyNextTask(testTask);
        Assert::IsFalse(ioPattern->IsCompleted());

        // should return NoIO since we are waiting on this task
        VerifyNoMoreIo(ioPattern);

        return testTask;
    }

    template <typename IoPattern>
    void VerifyNoMoreIo(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern)
    {
        auto noIoTask = ioPattern->GetNextPatternType();
        Assert::AreEqual(ctsIoPatternType::NoIo, noIoTask);
    }

    template <typename IoPattern>
    void FailIoAndVerify(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern)
    {
        Assert::AreEqual(m_testError, ioPattern->UpdateLastError(m_testError));
        Assert::IsTrue(ioPattern->IsCompleted());
        Assert::AreEqual(m_testError, ioPattern->GetLastError());
        Assert::AreEqual(m_testError, ioPattern->UpdateLastError(NO_ERROR));
        Assert::AreEqual(m_testError, ioPattern->GetLastError());
    }

    template <typename IoPattern>
    void CompleteIoAndVerifySuccess(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern, ctsTask task, uint32_t bytes)
    {
        Assert::AreEqual(m_zero, ioPattern->UpdateLastError(m_zero));
        ioPattern->CompletedTask(task, bytes);
        Assert::AreEqual(m_zero, ioPattern->GetLastError());
        Assert::AreEqual(m_zero, ioPattern->UpdateLastError(m_zero));
    }

    template <typename IoPattern>
    void RequestAndCompleteConnectionGuid(std::unique_ptr<ctsIoPatternProtocolPolicy<IoPattern>>& ioPattern)
    {
        ctsTask testTask = RequestConnectionGuid(ioPattern);
        ioPattern->CompletedTask(testTask, ctsStatistics::ConnectionIdLength);
        Assert::AreEqual(m_zero, ioPattern->GetLastError());
        Assert::IsFalse(ioPattern->IsCompleted());
    }

public:
    TEST_CLASS_INITIALIZE(Setup)
    {
        ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
    }

    TEST_CLASS_CLEANUP(Cleanup)
    {
        delete ctsConfig::g_configSettings;
    }


    TEST_METHOD(GracefulShutdownSetMaxTransfer)
    {
        const uint64_t testTransferSize(100);

        const auto testPattern = InitClientGracefulShutdownTest(250);
        Assert::AreEqual(g_transferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(250ull, testPattern->GetRemainingTransfer());

        testPattern->SetMaxTransfer(testTransferSize);
        Assert::AreEqual(testTransferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(testTransferSize, testPattern->GetRemainingTransfer());
    }

    TEST_METHOD(HardShutdownSetMaxTransfer)
    {
        const uint64_t testTransferSize(100);

        const auto testPattern = InitClientHardShutdownTest(250);
        Assert::AreEqual(g_transferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(250ull, testPattern->GetRemainingTransfer());

        testPattern->SetMaxTransfer(testTransferSize);
        Assert::AreEqual(testTransferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(testTransferSize, testPattern->GetRemainingTransfer());
    }

    TEST_METHOD(TCPServerShutdownSetMaxTransfer)
    {
        const uint64_t testTransferSize(100);

        const auto testPattern = InitServerGracefulShutdownTest(250);
        Assert::AreEqual(g_transferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(250ull, testPattern->GetRemainingTransfer());

        testPattern->SetMaxTransfer(testTransferSize);
        Assert::AreEqual(testTransferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(testTransferSize, testPattern->GetRemainingTransfer());
    }

    TEST_METHOD(UdpClientSetMaxTransfer)
    {
        const uint64_t testTransferSize(100);

        const auto testPattern = InitUdpClientTest(250);
        Assert::AreEqual(g_transferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(250ull, testPattern->GetRemainingTransfer());

        testPattern->SetMaxTransfer(testTransferSize);
        Assert::AreEqual(testTransferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(testTransferSize, testPattern->GetRemainingTransfer());
    }

    TEST_METHOD(UdpServerSetMaxTransfer)
    {
        const uint64_t testTransferSize(100);

        const auto testPattern = InitUdpServerTest(250);
        Assert::AreEqual(g_transferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(250ull, testPattern->GetRemainingTransfer());

        testPattern->SetMaxTransfer(testTransferSize);
        Assert::AreEqual(testTransferSize, testPattern->GetMaxTransfer());
        Assert::AreEqual(testTransferSize, testPattern->GetRemainingTransfer());
    }


    TEST_METHOD(SuccessfullySendConnectionGuid)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, ctsStatistics::ConnectionIdLength);
        Assert::IsFalse(testPattern->IsCompleted());
    }

    TEST_METHOD(UDPSuccessfullySendConnectionGuid)
    {
        auto testPattern = InitUdpServerTest(100);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, ctsStatistics::ConnectionIdLength);
        Assert::IsFalse(testPattern->IsCompleted());
    }

    TEST_METHOD(FailedSendConnectionGuid)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        [[maybe_unused]] ctsTask testTask = RequestConnectionGuid(testPattern);
        FailIoAndVerify(testPattern);
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPFailedSendConnectionGuid)
    {
        auto testPattern = InitUdpServerTest(100);
        [[maybe_unused]] ctsTask testTask = RequestConnectionGuid(testPattern);
        FailIoAndVerify(testPattern);
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownSuccessfullyReceiveConnectionGuid)
    {
        auto testPattern = InitClientGracefulShutdownTest(250);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, ctsStatistics::ConnectionIdLength);
        Assert::IsFalse(testPattern->IsCompleted());
    }

    TEST_METHOD(HardShutdownSuccessfullyReceiveConnectionGuid)
    {
        auto testPattern = InitClientHardShutdownTest(250);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, ctsStatistics::ConnectionIdLength);
        Assert::IsFalse(testPattern->IsCompleted());
    }

    TEST_METHOD(UDPSuccessfullyReceiveConnectionGuid)
    {
        auto testPattern = InitUdpClientTest(250);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, ctsStatistics::ConnectionIdLength);
        Assert::IsFalse(testPattern->IsCompleted());
    }

    TEST_METHOD(GracefulShutdownFailedReceiveConnectionGuid)
    {
        auto testPattern = InitClientGracefulShutdownTest(250);
        [[maybe_unused]] ctsTask testTask = RequestConnectionGuid(testPattern);
        FailIoAndVerify(testPattern);
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownFailedReceiveConnectionGuid)
    {
        auto testPattern = InitClientHardShutdownTest(250);
        [[maybe_unused]] ctsTask testTask = RequestConnectionGuid(testPattern);
        FailIoAndVerify(testPattern);
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPFailedReceiveConnectionGuid)
    {
        auto testPattern = InitUdpClientTest(250);
        [[maybe_unused]] ctsTask testTask = RequestConnectionGuid(testPattern);
        FailIoAndVerify(testPattern);
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownContinueIoAfterFailure)
    {
        auto testPattern = InitClientGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);
        FailIoAndVerify(testPattern);
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
    }

    TEST_METHOD(HardShutdownContinueIoAfterFailure)
    {
        auto testPattern = InitClientHardShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);
        FailIoAndVerify(testPattern);
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
    }

    TEST_METHOD(UDPContinueIoAfterFailure)
    {
        auto testPattern = InitUdpClientTest(100);
        RequestAndCompleteConnectionGuid(testPattern);
        FailIoAndVerify(testPattern);
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
    }

    TEST_METHOD(GracefulShutdownReceivedTooFewBytesForConnectionGuid)
    {
        auto testPattern = InitClientGracefulShutdownTest(250);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, ctsStatistics::ConnectionIdLength - 1);
        Assert::AreEqual(ctsIoPatternError::NoConnectionGuid, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownReceivedTooFewBytesForConnectionGuid)
    {
        auto testPattern = InitClientHardShutdownTest(250);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, ctsStatistics::ConnectionIdLength - 1);
        Assert::AreEqual(ctsIoPatternError::NoConnectionGuid, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPReceivedTooFewBytesForConnectionGuid)
    {
        auto testPattern = InitUdpClientTest(250);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, ctsStatistics::ConnectionIdLength - 1);
        Assert::AreEqual(ctsIoPatternError::NoConnectionGuid, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownReceivedZeroBytes)
    {
        auto testPattern = InitClientGracefulShutdownTest(100);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, 0);
        Assert::AreEqual(ctsIoPatternError::NoConnectionGuid, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownReceivedZeroBytes)
    {
        auto testPattern = InitClientHardShutdownTest(100);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, 0);
        Assert::AreEqual(ctsIoPatternError::NoConnectionGuid, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPReceivedZeroBytes)
    {
        auto testPattern = InitUdpClientTest(100);
        const ctsTask testTask = RequestConnectionGuid(testPattern);
        testPattern->CompletedTask(testTask, 0);
        Assert::AreEqual(ctsIoPatternError::NoConnectionGuid, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownReceivedZeroBytesAfterConnectionGuid)
    {
        auto testPattern = InitClientGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        const ctsTask testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 0);
        Assert::AreEqual(ctsIoPatternError::ZeroByteXfer, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownReceivedZeroBytesAfterConnectionGuid)
    {
        auto testPattern = InitClientHardShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        const ctsTask testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 0);
        Assert::AreEqual(ctsIoPatternError::ZeroByteXfer, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerShutdownReceivedZeroBytesAfterConnectionGuid)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        const ctsTask testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 0);
        Assert::AreEqual(ctsIoPatternError::ZeroByteXfer, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    /* Receiving a 0-byte datagram is just fine, differing from TCP behavior */
    TEST_METHOD(UDPReceivedZeroBytesAfterConnectionGuid)
    {
        auto testPattern = InitUdpClientTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        const ctsTask testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 0);
        Assert::AreEqual(ctsIoPatternError::NotProtocolError, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::IsFalse(testPattern->IsCompleted());
    }

    TEST_METHOD(GracefulShutdownClientFailIo)
    {
        auto testPattern = InitClientGracefulShutdownTest(250);
        RequestAndCompleteConnectionGuid(testPattern);

        const ctsTask testTask = RequestMoreIo(testPattern, 50);
        FailIoAndVerify(testPattern);
        testPattern->CompletedTask(testTask, 50);
        Assert::AreEqual(m_testError, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(m_testError, testPattern->UpdateLastError(m_testError));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownClientFailIo)
    {
        auto testPattern = InitClientHardShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        const ctsTask testTask = RequestMoreIo(testPattern, 50);
        FailIoAndVerify(testPattern);
        testPattern->CompletedTask(testTask, 50);
        Assert::AreEqual(m_testError, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(m_testError, testPattern->UpdateLastError(m_testError));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerFailIo)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        const ctsTask testTask = RequestMoreIo(testPattern, 50);
        FailIoAndVerify(testPattern);
        testPattern->CompletedTask(testTask, 50);
        Assert::AreEqual(m_testError, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(m_testError, testPattern->UpdateLastError(m_testError));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPClientFailIo)
    {
        auto testPattern = InitUdpClientTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        const ctsTask testTask = RequestMoreIo(testPattern, 50);
        FailIoAndVerify(testPattern);
        testPattern->CompletedTask(testTask, 50);
        Assert::AreEqual(m_testError, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(m_testError, testPattern->UpdateLastError(m_testError));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPServerFailIo)
    {
        auto testPattern = InitUdpServerTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        const ctsTask testTask = RequestMoreIo(testPattern, 50);
        FailIoAndVerify(testPattern);
        testPattern->CompletedTask(testTask, 50);
        Assert::AreEqual(m_testError, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(m_testError, testPattern->UpdateLastError(m_testError));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownClientFailTooManyBytes)
    {
        auto testPattern = InitClientGracefulShutdownTest(150);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());

        testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 100);
        Assert::AreEqual(ctsIoPatternError::TooManyBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorTooMuchDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownClientFailTooManyBytes)
    {
        auto testPattern = InitClientHardShutdownTest(150);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());

        testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 100);
        Assert::AreEqual(ctsIoPatternError::TooManyBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorTooMuchDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerFailTooManyBytes)
    {
        auto testPattern = InitServerGracefulShutdownTest(150);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());

        testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 100);
        Assert::AreEqual(ctsIoPatternError::TooManyBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorTooMuchDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPClientFailTooManyBytes)
    {
        auto testPattern = InitUdpClientTest(150);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());

        testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 100);
        Assert::AreEqual(ctsIoPatternError::TooManyBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorTooMuchDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPServerFailTooManyBytes)
    {
        auto testPattern = InitUdpServerTest(150);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());

        testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 100);
        Assert::AreEqual(ctsIoPatternError::TooManyBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorTooMuchDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownClientFailTooFewBytes)
    {
        auto testPattern = InitClientGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // 2 IO tasks - completing too few bytes
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 50); // only 50 of 100 bytes
        Assert::IsFalse(testPattern->IsCompleted());

        testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 0); // complete zero bytes - indicating FIN
        Assert::AreEqual(ctsIoPatternError::TooFewBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorNotAllDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownClientFailTooFewBytes)
    {
        auto testPattern = InitClientHardShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // 2 IO tasks - completing too few bytes
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 50); // only 50 of 100 bytes
        Assert::IsFalse(testPattern->IsCompleted());

        testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 0); // complete zero bytes - indicating FIN
        Assert::AreEqual(ctsIoPatternError::TooFewBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorNotAllDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerFailTooFewBytes)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // 2 IO tasks - completing too few bytes
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 50); // only 50 of 100 bytes

        testTask = RequestMoreIo(testPattern, 100);
        testPattern->CompletedTask(testTask, 0); // complete zero bytes - indicating FIN
        Assert::AreEqual(ctsIoPatternError::TooFewBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorNotAllDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }


    TEST_METHOD(GracefulShutdownClientFailFinalStatusTooFewBytes)
    {
        auto testPattern = InitClientGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Recv server status (should be 4 bytes - only completing 2)
        uint32_t status_code = m_zero;
        testTask = RequestRecvStatus(testPattern, &status_code);
        testPattern->CompletedTask(testTask, 2);
        Assert::AreEqual(ctsIoPatternError::TooFewBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorNotAllDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownClientFailFinalStatusTooFewBytes)
    {
        auto testPattern = InitClientHardShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Recv server status (should be 4 bytes - only completing 2)
        uint32_t status_code = m_zero;
        testTask = RequestRecvStatus(testPattern, &status_code);
        testPattern->CompletedTask(testTask, 2);
        Assert::AreEqual(ctsIoPatternError::TooFewBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorNotAllDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    /* UDP doesn't send a final status */

    TEST_METHOD(GracefulShutdownClientFailFinalStatusZeroBytes)
    {
        auto testPattern = InitClientGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Recv server status (should be 4 bytes - completing 0 - as in a FIN)
        uint32_t status_code = m_zero;
        testTask = RequestRecvStatus(testPattern, &status_code);
        testPattern->CompletedTask(testTask, 0);
        Assert::AreEqual(ctsIoPatternError::TooFewBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorNotAllDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownClientFailFinalStatusZeroBytes)
    {
        auto testPattern = InitClientHardShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Recv server status (should be 4 bytes - only completing 0 - as in a FIN)
        uint32_t status_code = m_zero;
        testTask = RequestRecvStatus(testPattern, &status_code);
        testPattern->CompletedTask(testTask, 0);
        Assert::AreEqual(ctsIoPatternError::TooFewBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(c_statusErrorNotAllDataTransferred, testPattern->UpdateLastError(m_zero));
        Assert::IsTrue(testPattern->IsCompleted());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(Client_GracefulShutdown_FINFailedTooManyBytes)
    {
        auto testPattern = InitClientGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Recv server status (4 bytes)
        uint32_t status_code = m_zero;
        testTask = RequestRecvStatus(testPattern, &status_code);
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Shutdown (0 byte FIN)
        testTask = RequestGracefulShutdown(testPattern);
        CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Request FIN 
        testTask = RequestFin(testPattern);
        testPattern->CompletedTask(testTask, 1);
        Assert::AreEqual(ctsIoPatternError::TooManyBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(c_statusErrorTooMuchDataTransferred, testPattern->UpdateLastError(m_zero));
        VerifyNoMoreIo(testPattern);

        // No FIN test for HardShutdown - since HardShutdown just sends a RST
    }

    TEST_METHOD(TCPServerFINFailedTooManyBytes)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Send status to client
        uint32_t status = m_zero;
        testTask = RequestSendStatus(testPattern, &status);
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Request FIN task
        testTask = RequestFin(testPattern);
        testPattern->CompletedTask(testTask, 1);
        Assert::AreEqual(ctsIoPatternError::TooManyBytes, ctsIoPatternStateCheckProtocolError(testPattern->GetLastError()));
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(c_statusErrorTooMuchDataTransferred, testPattern->UpdateLastError(m_zero));
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownClientSingleIo)
    {
        auto testPattern = InitClientGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Receive server status
        uint32_t status = m_zero;
        testTask = RequestRecvStatus(testPattern, &status);
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Shutdown Task
        testTask = RequestGracefulShutdown(testPattern);
        CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Request FIN task
        testTask = RequestFin(testPattern);
        CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownClientSingleIo)
    {
        auto testPattern = InitClientHardShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Receive server status
        uint32_t status = m_zero;
        testTask = RequestRecvStatus(testPattern, &status);
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Shutdown Task
        testTask = RequestHardShutdown(testPattern);
        CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPClientSingleIo)
    {
        auto testPattern = InitUdpClientTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task
        const ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPServerSingleIo)
    {
        auto testPattern = InitUdpServerTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task
        const ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerSingleIo_FIN)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Send status to client
        uint32_t status = m_zero;
        testTask = RequestSendStatus(testPattern, &status);
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Request FIN task
        testTask = RequestFin(testPattern);
        CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerSingleIo_RST_connreset)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Send status to client
        uint32_t status = m_zero;
        testTask = RequestSendStatus(testPattern, &status);
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Request FIN task - but that fails with WSAECONNRESET - which is OK if the client wanted to RST instead of FIN
        [[maybe_unused]] const ctsTask finTask = RequestFin(testPattern);
        Assert::AreEqual(m_zero, testPattern->UpdateLastError(WSAECONNRESET));
        //            CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerSingleIo_RST_connaborted)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Send status to client
        uint32_t status = m_zero;
        testTask = RequestSendStatus(testPattern, &status);
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Request FIN task - but that fails with WSAECONNABORTED - which is OK if the client wanted to RST instead of FIN
        [[maybe_unused]] const ctsTask finTask = RequestFin(testPattern);
        Assert::AreEqual(m_zero, testPattern->UpdateLastError(WSAECONNABORTED));
        //            CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerSingleIo_RST_timedout)
    {
        auto testPattern = InitServerGracefulShutdownTest(100);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Send status to client
        uint32_t status = m_zero;
        testTask = RequestSendStatus(testPattern, &status);
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Request FIN task - but that fails with WSAETIMEDOUT - which is OK if the client wanted to RST instead of FIN
        [[maybe_unused]] const ctsTask finTask = RequestFin(testPattern);
        Assert::AreEqual(m_zero, testPattern->UpdateLastError(WSAETIMEDOUT));
        //            CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownClientMultipleIo)
    {
        auto testPattern = InitClientGracefulShutdownTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());

        // IO Task #2
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());

        // IO Task #3
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Recv the server status
        uint32_t status = m_zero;
        testTask = RequestRecvStatus(testPattern, &status);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Graceful shutdown
        testTask = RequestGracefulShutdown(testPattern);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Request FIN task
        testTask = RequestFin(testPattern);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownClientMultipleIo)
    {
        auto testPattern = InitClientHardShutdownTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());

        // IO Task #2
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());

        // IO Task #3
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Recv the server status
        uint32_t status = m_zero;
        testTask = RequestRecvStatus(testPattern, &status);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // shutdown
        testTask = RequestHardShutdown(testPattern);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerMultipleIo)
    {
        auto testPattern = InitServerGracefulShutdownTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());

        // IO Task #2
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());

        // IO Task #3
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Send server status
        uint32_t status = m_zero;
        testTask = RequestSendStatus(testPattern, &status);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        // Request FIN task
        testTask = RequestFin(testPattern);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(m_zero, testPattern->UpdateLastError(m_zero));
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPClientMultipleIo)
    {
        auto testPattern = InitUdpClientTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());

        // IO Task #2
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());

        // IO Task #3
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPServerMultipleIo)
    {
        auto testPattern = InitUdpServerTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        ctsTask testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());

        // IO Task #2
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());

        // IO Task #3
        testTask = RequestMoreIo(testPattern, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        CompleteIoAndVerifySuccess(testPattern, testTask, 100);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownClientOverlappingMultipleIo)
    {
        auto testPattern = InitClientGracefulShutdownTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        const ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        CompleteIoAndVerifySuccess(testPattern, testTask1, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2
        //
        CompleteIoAndVerifySuccess(testPattern, testTask2, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3
        //
        CompleteIoAndVerifySuccess(testPattern, testTask3, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // Recv server status
        //
        uint32_t status_buffer = m_zero;
        const ctsTask server_status_task = RequestRecvStatus(testPattern, &status_buffer);
        CompleteIoAndVerifySuccess(testPattern, server_status_task, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // Shutdown Task
        //
        const ctsTask shutdown_task = RequestGracefulShutdown(testPattern);
        CompleteIoAndVerifySuccess(testPattern, shutdown_task, 0);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // Request FIN task
        //
        const ctsTask final_fin_task = RequestFin(testPattern);
        CompleteIoAndVerifySuccess(testPattern, final_fin_task, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownClientOverlappingMultipleIo)
    {
        auto testPattern = InitClientHardShutdownTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        const ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        CompleteIoAndVerifySuccess(testPattern, testTask1, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2
        //
        CompleteIoAndVerifySuccess(testPattern, testTask2, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3
        //
        CompleteIoAndVerifySuccess(testPattern, testTask3, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // Recv server status
        //
        uint32_t status_buffer = m_zero;
        const ctsTask server_status_task = RequestRecvStatus(testPattern, &status_buffer);
        CompleteIoAndVerifySuccess(testPattern, server_status_task, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // Shutdown Task
        //
        const ctsTask shutdown_task = RequestHardShutdown(testPattern);
        CompleteIoAndVerifySuccess(testPattern, shutdown_task, 4);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerOverlappingMultipleIo)
    {
        auto testPattern = InitServerGracefulShutdownTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        const ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        CompleteIoAndVerifySuccess(testPattern, testTask1, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2
        //
        CompleteIoAndVerifySuccess(testPattern, testTask2, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3
        //
        CompleteIoAndVerifySuccess(testPattern, testTask3, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // Send server status
        //
        uint32_t status = m_zero;
        const ctsTask send_status_task = RequestSendStatus(testPattern, &status);
        CompleteIoAndVerifySuccess(testPattern, send_status_task, 4);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // Request FIN task
        //
        const ctsTask fin_task = RequestFin(testPattern);
        CompleteIoAndVerifySuccess(testPattern, fin_task, 0);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPClientOverlappingMultipleIo)
    {
        auto testPattern = InitUdpClientTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        const ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        CompleteIoAndVerifySuccess(testPattern, testTask1, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2
        //
        CompleteIoAndVerifySuccess(testPattern, testTask2, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3
        //
        CompleteIoAndVerifySuccess(testPattern, testTask3, 100);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPServerOverlappingMultipleIo)
    {
        auto testPattern = InitUdpServerTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        const ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        CompleteIoAndVerifySuccess(testPattern, testTask1, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2
        //
        CompleteIoAndVerifySuccess(testPattern, testTask2, 100);
        Assert::IsFalse(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3
        //
        CompleteIoAndVerifySuccess(testPattern, testTask3, 100);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());

        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(GracefulShutdownFailingOneIoWithClientOverlappingMultipleIo)
    {
        auto testPattern = InitClientGracefulShutdownTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        [[maybe_unused]] ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        FailIoAndVerify(testPattern);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask2, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask3, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // Since failed should be no more IO
        //
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(HardShutdownFailingOneIoWithClientOverlappingMultipleIo)
    {
        auto testPattern = InitClientHardShutdownTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        [[maybe_unused]] ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        FailIoAndVerify(testPattern);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask2, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask3, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // Since failed should be no more IO
        //
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(TCPServerFailingOneIoWithOverlappingMultipleIo)
    {
        auto testPattern = InitServerGracefulShutdownTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        [[maybe_unused]] ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        FailIoAndVerify(testPattern);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask2, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask3, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // Since failed should be no more IO
        //
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPClientFailingOneIoWithOverlappingMultipleIo)
    {
        auto testPattern = InitUdpClientTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        [[maybe_unused]] ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        FailIoAndVerify(testPattern);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask2, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask3, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // Since failed should be no more IO
        //
        VerifyNoMoreIo(testPattern);
    }

    TEST_METHOD(UDPServerFailingOneIoWithOverlappingMultipleIo)
    {
        auto testPattern = InitUdpServerTest(100 * 3);
        RequestAndCompleteConnectionGuid(testPattern);

        // IO Task #1
        [[maybe_unused]] ctsTask testTask1 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(200), testPattern->GetRemainingTransfer());
        // IO Task #2
        const ctsTask testTask2 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(100), testPattern->GetRemainingTransfer());
        // IO Task #3
        const ctsTask testTask3 = RequestMoreIo(testPattern, 100);
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        //
        // all IO is now posted
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 1
        //
        FailIoAndVerify(testPattern);
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 2 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask2, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // should return NoIO while IO is still pended
        //
        VerifyNoMoreIo(testPattern);
        //
        // complete_io 3 successfully - after the first failed
        //
        Assert::AreEqual(g_TestErrorCode, testPattern->UpdateLastError(m_zero));
        testPattern->CompletedTask(testTask3, 100);
        Assert::AreEqual(g_TestErrorCode, testPattern->GetLastError());
        Assert::IsTrue(testPattern->IsCompleted());
        Assert::AreEqual(static_cast<uint64_t>(m_zero), testPattern->GetRemainingTransfer());
        Assert::AreEqual(ctsIoPatternType::NoIo, testPattern->GetNextPatternType());
        //
        // Since failed should be no more IO
        //
        VerifyNoMoreIo(testPattern);
    }
};
}
