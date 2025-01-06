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

#include "ctsIOPatternState.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{
    template <>
    inline std::wstring ToString<ctsTraffic::ctsIoPatternType>(const ctsTraffic::ctsIoPatternType& value)
    {
        switch (value)
        {
        case ctsTraffic::ctsIoPatternType::NoIo:
            return L"NoIo";
        case ctsTraffic::ctsIoPatternType::SendConnectionId:
            return L"SendConnectionId";
        case ctsTraffic::ctsIoPatternType::RecvConnectionId:
            return L"RecvConnectionId";
        case ctsTraffic::ctsIoPatternType::MoreIo:
            return L"MoreIo";
        case ctsTraffic::ctsIoPatternType::RecvCompletion:
            return L"RecvCompletion";
        case ctsTraffic::ctsIoPatternType::SendCompletion:
            return L"SendCompletion";
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
    inline std::wstring ToString<ctsTraffic::ctsIoPatternError>(const ctsTraffic::ctsIoPatternError& value)
    {
        switch (value)
        {
        case ctsTraffic::ctsIoPatternError::NoError:
            return L"NoError";
        case ctsTraffic::ctsIoPatternError::TooManyBytes:
            return L"TooManyBytes";
        case ctsTraffic::ctsIoPatternError::TooFewBytes:
            return L"TooFewBytes";
        case ctsTraffic::ctsIoPatternError::ErrorIoFailed:
            return L"ErrorIOFailed";
        case ctsTraffic::ctsIoPatternError::CorruptedBytes:
            return L"CorruptedBytes";
        case ctsTraffic::ctsIoPatternError::SuccessfullyCompleted:
            return L"SuccessfullyCompleted";
        }

        Assert::Fail(L"Unknown ctsIoPatternError");
    }
}

uint64_t g_transferSize = 0ULL;
bool g_isListening = false;

///
/// Fakes
///
namespace ctsTraffic::ctsConfig
{
    ctsConfigSettings* g_configSettings;

    void PrintConnectionResults(const socket_address&, const socket_address&, uint32_t) noexcept
    {
    }

    void PrintConnectionResults(const socket_address&, const socket_address&, uint32_t,
                                const ctsTcpStatistics&) noexcept
    {
    }

    void PrintConnectionResults(const socket_address&, const socket_address&, uint32_t,
                                const ctsUdpStatistics&) noexcept
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
        return g_configSettings->TcpShutdown;
    }
}

///
/// End of Fakes
///

using namespace ctsTraffic;

namespace ctsUnitTest
{
    TEST_CLASS(ctsIOPatternStateUnitTest)
    {
    private:
        //
        // The pattern state to use with each test
        //
        std::unique_ptr<ctsIoPatternState> m_ioPatternState;

        enum Role
        {
            Client,
            Server
        };

        void InitGracefulShutdownTest(uint64_t testTransferSize, Role _role = Client)
        {
            ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            g_isListening = (Server == _role);
            g_transferSize = testTransferSize;
            m_ioPatternState = std::make_unique<ctsIoPatternState>();

            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(m_ioPatternState->GetRemainingTransfer(), g_transferSize);
        }

        void InitHardShutdownTest(uint64_t testTransferSize)
        {
            ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            g_isListening = false; // client-only
            g_transferSize = testTransferSize;
            m_ioPatternState = std::make_unique<ctsIoPatternState>();

            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(m_ioPatternState->GetRemainingTransfer(), g_transferSize);
        }

        //
        // Private members to implement building out a ctsIOTask for each task
        //

        [[nodiscard]] ctsTask RequestConnectionId() const
        {
            const auto task = m_ioPatternState->GetNextPatternType();
            if (g_isListening)
            {
                Assert::AreEqual(ctsIoPatternType::SendConnectionId, task);
            }
            else
            {
                Assert::AreEqual(ctsIoPatternType::RecvConnectionId, task);
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

            m_ioPatternState->NotifyNextTask(testTask);
            Assert::IsFalse(m_ioPatternState->IsCompleted());

            return testTask;
        }

        [[nodiscard]] ctsTask RequestMoreIo(uint32_t bufferLength) const
        {
            const auto task = m_ioPatternState->GetNextPatternType();
            Assert::AreEqual(ctsIoPatternType::MoreIo, task);

            ctsTask testTask;
            testTask.m_ioAction = ctsTaskAction::Recv;
            testTask.m_trackIo = true;
            testTask.m_bufferLength = bufferLength;

            m_ioPatternState->NotifyNextTask(testTask);
            Assert::IsFalse(m_ioPatternState->IsCompleted());

            return testTask;
        }

        [[nodiscard]] ctsTask RequestSendStatus(_In_ uint32_t* statusBuffer) const
        {
            // get_next_task
            const auto task = m_ioPatternState->GetNextPatternType();
            Assert::AreEqual(ctsIoPatternType::SendCompletion, task);

            ctsTask testTask;
            testTask.m_ioAction = ctsTaskAction::Send;
            testTask.m_trackIo = false;
            testTask.m_buffer = reinterpret_cast<char*>(statusBuffer);
            testTask.m_bufferLength = 4;

            // NotifyNextTask
            m_ioPatternState->NotifyNextTask(testTask);
            Assert::IsFalse(m_ioPatternState->IsCompleted());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return testTask;
        }

        [[nodiscard]] ctsTask RequestRecvStatus(_In_ uint32_t* statusBuffer) const
        {
            // get_next_task
            const auto task = m_ioPatternState->GetNextPatternType();
            Assert::AreEqual(ctsIoPatternType::RecvCompletion, task);

            ctsTask testTask;
            testTask.m_ioAction = ctsTaskAction::Recv;
            testTask.m_trackIo = false;
            testTask.m_buffer = reinterpret_cast<char*>(statusBuffer);
            testTask.m_bufferLength = 4;

            // NotifyNextTask
            m_ioPatternState->NotifyNextTask(testTask);
            Assert::IsFalse(m_ioPatternState->IsCompleted());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return testTask;
        }

        [[nodiscard]] ctsTask RequestFin() const
        {
            // get_next_task
            const auto task = m_ioPatternState->GetNextPatternType();
            Assert::AreEqual(ctsIoPatternType::RequestFin, task);

            ctsTask testTask;
            testTask.m_ioAction = ctsTaskAction::Recv;
            testTask.m_trackIo = false;
            testTask.m_bufferLength = 16;

            // NotifyNextTask
            m_ioPatternState->NotifyNextTask(testTask);
            Assert::IsFalse(m_ioPatternState->IsCompleted());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return testTask;
        }

        [[nodiscard]] ctsTask RequestGracefulShutdown() const
        {
            // get_next_task
            const auto task = m_ioPatternState->GetNextPatternType();
            Assert::AreEqual(ctsIoPatternType::GracefulShutdown, task);

            ctsTask testTask;
            testTask.m_ioAction = ctsTaskAction::GracefulShutdown;
            testTask.m_trackIo = false;
            testTask.m_bufferLength = 0;

            // NotifyNextTask
            m_ioPatternState->NotifyNextTask(testTask);
            Assert::IsFalse(m_ioPatternState->IsCompleted());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return testTask;
        }

        [[nodiscard]] ctsTask RequestHardShutdown() const
        {
            // get_next_task
            const auto task = m_ioPatternState->GetNextPatternType();
            Assert::AreEqual(ctsIoPatternType::HardShutdown, task);

            ctsTask testTask;
            testTask.m_ioAction = ctsTaskAction::HardShutdown;
            testTask.m_trackIo = false;
            testTask.m_bufferLength = 0;

            // NotifyNextTask
            m_ioPatternState->NotifyNextTask(testTask);
            Assert::IsFalse(m_ioPatternState->IsCompleted());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return testTask;
        }

        void VerifyNoMoreIo() const
        {
            const auto noIoTask = m_ioPatternState->GetNextPatternType();
            Assert::AreEqual(ctsIoPatternType::NoIo, noIoTask);
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

        TEST_METHOD(TestGetMaxTransfer)
        {
            this->InitGracefulShutdownTest(100);
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetMaxTransfer());

            this->InitHardShutdownTest(100);
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetMaxTransfer());
        }

        TEST_METHOD(TestGetRemainingTransfer)
        {
            this->InitGracefulShutdownTest(100);
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetRemainingTransfer());

            this->InitHardShutdownTest(100);
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetRemainingTransfer());
        }

        TEST_METHOD(TestSetMaxTransfer)
        {
            static constexpr uint64_t c_testTransferSize(100);

            this->InitGracefulShutdownTest(250);
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetMaxTransfer());
            m_ioPatternState->SetMaxTransfer(c_testTransferSize);
            Assert::AreEqual(c_testTransferSize, m_ioPatternState->GetMaxTransfer());

            this->InitHardShutdownTest(250);
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetMaxTransfer());
            m_ioPatternState->SetMaxTransfer(c_testTransferSize);
            Assert::AreEqual(c_testTransferSize, m_ioPatternState->GetMaxTransfer());
        }

        TEST_METHOD(TestGetRemainingTransferAfterSetMaxTransfer)
        {
            static constexpr uint64_t c_testTransferSize(100);

            this->InitGracefulShutdownTest(250);
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetMaxTransfer());
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetRemainingTransfer());

            m_ioPatternState->SetMaxTransfer(c_testTransferSize);
            Assert::AreEqual(c_testTransferSize, m_ioPatternState->GetMaxTransfer());
            Assert::AreEqual(c_testTransferSize, m_ioPatternState->GetRemainingTransfer());

            this->InitHardShutdownTest(250);
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetMaxTransfer());
            Assert::AreEqual(g_transferSize, m_ioPatternState->GetRemainingTransfer());

            m_ioPatternState->SetMaxTransfer(c_testTransferSize);
            Assert::AreEqual(c_testTransferSize, m_ioPatternState->GetMaxTransfer());
            Assert::AreEqual(c_testTransferSize, m_ioPatternState->GetRemainingTransfer());
        }

        TEST_METHOD(TestClientIsCompletedNoIo)
        {
            this->InitGracefulShutdownTest(100, Client);
            Assert::IsFalse(m_ioPatternState->IsCompleted());

            this->InitHardShutdownTest(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
        }

        TEST_METHOD(TestServerIsCompletedNoIo)
        {
            this->InitGracefulShutdownTest(100, Server);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
        }

        TEST_METHOD(TestSuccessfullySendConnectionId)
        {
            this->InitGracefulShutdownTest(100, Server);
            const ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, testTask.m_bufferLength);
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
        }

        TEST_METHOD(TestFailedSendConnectionId)
        {
            this->InitGracefulShutdownTest(100, Server);
            const ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, testTask.m_bufferLength);
            // indicate an error
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
        }

        TEST_METHOD(TestSuccessfullyReceiveConnectionId)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, testTask.m_bufferLength);
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            Assert::IsFalse(m_ioPatternState->IsCompleted());

            this->InitHardShutdownTest(100);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, testTask.m_bufferLength);
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
        }

        TEST_METHOD(TestFailedReceiveConnectionId)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, testTask.m_bufferLength);
            // indicate an error
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, testTask.m_bufferLength);
            // indicate an error
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestReceivedTooFewBytesForConnectionId)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, testTask.m_bufferLength);
            Assert::AreEqual(ctsIoPatternError::TooFewBytes,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength - 1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());

            this->InitHardShutdownTest(100);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, testTask.m_bufferLength);
            Assert::AreEqual(ctsIoPatternError::TooFewBytes,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength - 1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
        }

        TEST_METHOD(TestClientFailIo)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            testTask = this->RequestMoreIo(50);
            // indicate an error
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->CompletedTask(testTask, 50));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            testTask = this->RequestMoreIo(50);
            // indicate an error
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->CompletedTask(testTask, 50));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerFailIo)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            testTask = this->RequestMoreIo(50);

            // indicate an error
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->CompletedTask(testTask, 50));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(1));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientFailTooManyBytes)
        {
            this->InitGracefulShutdownTest(150, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::TooManyBytes, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(150);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::TooManyBytes, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerFailTooManyBytes)
        {
            this->InitGracefulShutdownTest(150, Server);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::TooManyBytes, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientFailTooFewBytes)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // 2 IO tasks - completing too few bytes
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 50));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::TooFewBytes, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // 2 IO tasks - completing too few bytes
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 50));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::TooFewBytes, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerFailTooFewBytes)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // 2 IO tasks - completing too few bytes
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 50));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::TooFewBytes, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClient_GracefulShutdown_FINFailedTooManyBytes)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            // Recv server status
            uint32_t statusCode = NO_ERROR;
            testTask = this->RequestRecvStatus(&statusCode);
            // write "DONE" in the message to complete it
            memcpy_s(testTask.m_buffer, testTask.m_bufferLength, "DONE", 4);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            // Shutdown 
            testTask = this->RequestGracefulShutdown();
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            // Request FIN 
            testTask = this->RequestFin();
            Assert::AreEqual(ctsIoPatternError::TooManyBytes, m_ioPatternState->CompletedTask(testTask, 1));
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(0));
            this->VerifyNoMoreIo();

            // No FIN test for HardShutdown - since HardShutdown just sends a RST
        }

        TEST_METHOD(TestServerFINFailedTooManyBytes)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            // Send status to client
            uint32_t status = NO_ERROR;
            testTask = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            // Request FIN task
            testTask = this->RequestFin();
            Assert::AreEqual(ctsIoPatternError::TooManyBytes, m_ioPatternState->CompletedTask(testTask, 1));
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(ctsIoPatternError::ErrorIoFailed, m_ioPatternState->UpdateError(0));
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientSingleIo)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Receive server status
            uint32_t status = NO_ERROR;
            testTask = this->RequestRecvStatus(&status);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            // write "DONE" in the message to complete it
            memcpy_s(testTask.m_buffer, testTask.m_bufferLength, "DONE", 4);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Shutdown Task
            testTask = this->RequestGracefulShutdown();
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Request FIN task
            testTask = this->RequestFin();
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Receive server status
            status = NO_ERROR;
            testTask = this->RequestRecvStatus(&status);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            // write "DONE" in the message to complete it
            memcpy_s(testTask.m_buffer, testTask.m_bufferLength, "DONE", 4);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Shutdown Task
            testTask = this->RequestHardShutdown();
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerSingleIo_FIN)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Send status to client
            uint32_t status = NO_ERROR;
            testTask = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Request FIN task
            testTask = this->RequestFin();
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerSingleIo_RST)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Send status to client
            uint32_t status = NO_ERROR;
            testTask = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Request FIN task - but that fails with WSAECONNRESET - which is OK if the client wanted to RST instead of FIN
            testTask = this->RequestFin();
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(WSAECONNRESET));
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerSingleIo_RST_with_other_error)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task
            testTask = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Send status to client
            uint32_t status = NO_ERROR;
            testTask = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Request FIN task - but that fails with WSAECONNRESET - which is OK if the client wanted to RST instead of FIN
            testTask = this->RequestFin();
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(WSAECONNABORTED));
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientMultipleIo)
        {
            this->InitGracefulShutdownTest(100 * 3, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            // IO Task #2
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            // IO Task #3
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Recv the server status
            uint32_t status = NO_ERROR;
            testTask = this->RequestRecvStatus(&status);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // write "DONE" in the message to complete it
            memcpy_s(testTask.m_buffer, testTask.m_bufferLength, "DONE", 4);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Graceful shutdown
            testTask = this->RequestGracefulShutdown();
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Request FIN task
            testTask = this->RequestFin();
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();

            this->InitGracefulShutdownTest(100 * 3, Client);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            // IO Task #2
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            // IO Task #3
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Recv the server status
            status = NO_ERROR;
            testTask = this->RequestRecvStatus(&status);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // not writing "DONE" in the message - should fail the completion
            Assert::AreEqual(ctsIoPatternError::TooFewBytes, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100 * 3);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            // IO Task #2
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            // IO Task #3
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Recv the server status
            status = NO_ERROR;
            testTask = this->RequestRecvStatus(&status);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // write "DONE" in the message to complete it
            memcpy_s(testTask.m_buffer, testTask.m_bufferLength, "DONE", 4);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // shutdown
            testTask = this->RequestHardShutdown();
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerMultipleIo)
        {
            this->InitGracefulShutdownTest(100 * 3, Server);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            // IO Task #2
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            // IO Task #3
            testTask = this->RequestMoreIo(100);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 100));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Send server status
            uint32_t status = NO_ERROR;
            testTask = this->RequestSendStatus(&status);
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask, 4));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            // Request FIN task
            testTask = this->RequestFin();
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted, m_ioPatternState->CompletedTask(testTask, 0));
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->UpdateError(0));
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientOverlappingMultipleIo)
        {
            this->InitGracefulShutdownTest(100 * 3, Client);
            ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            ctsTask testTask1 = this->RequestMoreIo(100);
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            // IO Task #2
            ctsTask testTask2 = this->RequestMoreIo(100);
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            // IO Task #3
            ctsTask testTask3 = this->RequestMoreIo(100);
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo();
            // complete_io 1
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask1, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternType::NoIo, m_ioPatternState->GetNextPatternType());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 2
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask2, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternType::NoIo, m_ioPatternState->GetNextPatternType());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 3
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask3, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // Recv server status
            //
            uint32_t statusBuffer = NO_ERROR;
            ctsTask serverStatusTask = this->RequestRecvStatus(&statusBuffer);
            // write "DONE" in the message to complete it
            memcpy_s(serverStatusTask.m_buffer, testTask.m_bufferLength, "DONE", 4);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(serverStatusTask, 4));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // Shutdown Task
            //
            ctsTask shutdownTask = this->RequestGracefulShutdown();
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(shutdownTask, 0));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // Request FIN task
            //
            ctsTask finalFinTask = this->RequestFin();
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted,
                             m_ioPatternState->CompletedTask(finalFinTask, 0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();


            this->InitHardShutdownTest(100 * 3);
            testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            testTask1 = this->RequestMoreIo(100);
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            // IO Task #2
            testTask2 = this->RequestMoreIo(100);
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            // IO Task #3
            testTask3 = this->RequestMoreIo(100);
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo();
            // complete_io 1
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask1, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternType::NoIo, m_ioPatternState->GetNextPatternType());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 2
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask2, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternType::NoIo, m_ioPatternState->GetNextPatternType());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 3
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask3, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // Recv server status
            //
            statusBuffer = NO_ERROR;
            serverStatusTask = this->RequestRecvStatus(&statusBuffer);
            // write "DONE" in the message to complete it
            memcpy_s(serverStatusTask.m_buffer, testTask.m_bufferLength, "DONE", 4);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(serverStatusTask, 4));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // Shutdown Task
            //
            shutdownTask = this->RequestHardShutdown();
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted,
                             m_ioPatternState->CompletedTask(finalFinTask, 0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerOverlappingMultipleIo)
        {
            this->InitGracefulShutdownTest(100 * 3, Server);
            const ctsTask testTask = this->RequestConnectionId();
            Assert::AreEqual(ctsIoPatternError::NoError,
                             m_ioPatternState->CompletedTask(testTask, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            const ctsTask testTask1 = this->RequestMoreIo(100);
            Assert::AreEqual(static_cast<uint64_t>(200), m_ioPatternState->GetRemainingTransfer());
            // IO Task #2
            const ctsTask testTask2 = this->RequestMoreIo(100);
            Assert::AreEqual(static_cast<uint64_t>(100), m_ioPatternState->GetRemainingTransfer());
            // IO Task #3
            const ctsTask testTask3 = this->RequestMoreIo(100);
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo();
            // complete_io 1
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask1, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternType::NoIo, m_ioPatternState->GetNextPatternType());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 2
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask2, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            Assert::AreEqual(ctsIoPatternType::NoIo, m_ioPatternState->GetNextPatternType());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 3
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(testTask3, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // Send server status
            //
            uint32_t status = NO_ERROR;
            const ctsTask sendStatusTask = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIoPatternError::NoError, m_ioPatternState->CompletedTask(sendStatusTask, 100));
            Assert::IsFalse(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
            //
            // Request FIN task
            //
            const ctsTask finTask = this->RequestFin();
            Assert::AreEqual(ctsIoPatternError::SuccessfullyCompleted, m_ioPatternState->CompletedTask(finTask, 0));
            Assert::IsTrue(m_ioPatternState->IsCompleted());
            Assert::AreEqual(static_cast<uint64_t>(0), m_ioPatternState->GetRemainingTransfer());
        }
    };
}
