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

#include "ctsIOPatternState.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{
    template<> inline std::wstring ToString<ctsTraffic::ctsUnsignedLongLong>(const ctsTraffic::ctsUnsignedLongLong& _value)
    {
        return std::to_wstring(static_cast<unsigned long long>(_value));
    }
    /*
    enum ctsIOPatternProtocolTask {
        NoIo,
        SendConnectionId,
        RecvConnectionId,
        MoreIo,
        SendCompletion,
        RecvCompletion,
        GracefulShutdown,
        HardShutdown,
        RequestFin
        };
    enum ctsIOPatternProtocolError {
        NoError,
        TooManyBytes,
        TooFewBytes,
        CorruptedBytes,
        ErrorIOFailed,
        SuccessfullyCompleted
    */
    template<> inline std::wstring ToString<ctsTraffic::ctsIOPatternProtocolTask>(const ctsTraffic::ctsIOPatternProtocolTask& _value)
    {
        switch (_value)
        {
            case ctsTraffic::ctsIOPatternProtocolTask::NoIo: return L"NoIo";
            case ctsTraffic::ctsIOPatternProtocolTask::SendConnectionId: return L"SendConnectionId";
            case ctsTraffic::ctsIOPatternProtocolTask::RecvConnectionId: return L"RecvConnectionId";
            case ctsTraffic::ctsIOPatternProtocolTask::MoreIo: return L"MoreIo";
            case ctsTraffic::ctsIOPatternProtocolTask::RecvCompletion: return L"RecvCompletion";
            case ctsTraffic::ctsIOPatternProtocolTask::SendCompletion: return L"SendCompletion";
            case ctsTraffic::ctsIOPatternProtocolTask::GracefulShutdown: return L"GracefulShutdown";
            case ctsTraffic::ctsIOPatternProtocolTask::HardShutdown: return L"HardShutdown";
            case ctsTraffic::ctsIOPatternProtocolTask::RequestFIN: return L"this->RequestFIN";
        }

        Assert::Fail(L"Unknown ctsIOPatternProtocolTask");
    }

    template<> inline std::wstring ToString<ctsTraffic::ctsIOPatternProtocolError>(const ctsTraffic::ctsIOPatternProtocolError& _value)
    {
        switch (_value)
        {
            case ctsTraffic::ctsIOPatternProtocolError::NoError: return L"NoError";
            case ctsTraffic::ctsIOPatternProtocolError::TooManyBytes: return L"TooManyBytes";
            case ctsTraffic::ctsIOPatternProtocolError::TooFewBytes: return L"TooFewBytes";
            case ctsTraffic::ctsIOPatternProtocolError::ErrorIOFailed: return L"ErrorIOFailed";
            case ctsTraffic::ctsIOPatternProtocolError::CorruptedBytes: return L"CorruptedBytes";
            case ctsTraffic::ctsIOPatternProtocolError::SuccessfullyCompleted: return L"SuccessfullyCompleted";
        }

        Assert::Fail(L"Unknown ctsIOPatternProtocolError");
    }
}

ctsTraffic::ctsUnsignedLongLong s_TransferSize = 0ULL;
bool s_Listening = false;
///
/// Fakes
///
namespace ctsTraffic::ctsConfig
{
    ctsConfigSettings* Settings;

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
    void PrintJitterUpdate(long long, long long, long long, long long, long long) noexcept
    {
    }
    void PrintErrorInfo(_In_z_ _Printf_format_string_ PCWSTR, ...) noexcept
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
    bool ShutdownCalled() noexcept
    {
        return false;
    }
    unsigned long ConsoleVerbosity() noexcept
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
    TEST_CLASS(ctsIOPatternStateUnitTest)
    {
    private:
        //
        // The pattern state to use with each test
        //
        std::unique_ptr<ctsIOPatternState> ioPatternState;

        enum Role
        {
            Client,
            Server
        };

        void InitGracefulShutdownTest(unsigned long long _test_transfer_size, Role _role = Client)
        {
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            s_Listening = (Server == _role);
            s_TransferSize = _test_transfer_size;
            this->ioPatternState = std::make_unique<ctsIOPatternState>();

            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(this->ioPatternState->get_remaining_transfer(), s_TransferSize);
        }
        void InitHardShutdownTest(unsigned long long _test_transfer_size)
        {
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            s_Listening = false; // client-only
            s_TransferSize = _test_transfer_size;
            this->ioPatternState = std::make_unique<ctsIOPatternState>();

            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(this->ioPatternState->get_remaining_transfer(), s_TransferSize);
        }

        //
        // Private members to implement building out a ctsIOTask for each task
        //

        ctsIOTask RequestConnectionId() const
        {
            auto task = this->ioPatternState->get_next_task();
            if (s_Listening)
            {
                Assert::AreEqual(ctsIOPatternProtocolTask::SendConnectionId, task);
            }
            else
            {
                Assert::AreEqual(ctsIOPatternProtocolTask::RecvConnectionId, task);
            }

            ctsIOTask test_task;
            if (s_Listening)
            {
                test_task.ioAction = IOTaskAction::Send;
            }
            else
            {
                test_task.ioAction = IOTaskAction::Recv;
            }
            test_task.track_io = false;
            test_task.buffer_length = ctsStatistics::ConnectionIdLength;

            this->ioPatternState->notify_next_task(test_task);
            Assert::IsFalse(this->ioPatternState->is_completed());

            return test_task;
        }

        ctsIOTask RequestMoreIo(unsigned long _buffer_length) const
        {
            auto task = this->ioPatternState->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::MoreIo, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Recv;
            test_task.track_io = true;
            test_task.buffer_length = _buffer_length;

            this->ioPatternState->notify_next_task(test_task);
            Assert::IsFalse(this->ioPatternState->is_completed());

            return test_task;
        }

        ctsIOTask RequestSendStatus(_In_ unsigned long* _status_buffer)
        {
            // get_next_task
            auto task = this->ioPatternState->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::SendCompletion, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            test_task.track_io = false;
            test_task.buffer = reinterpret_cast<char*>(_status_buffer);
            test_task.buffer_length = 4;

            // notify_next_task
            this->ioPatternState->notify_next_task(test_task);
            Assert::IsFalse(this->ioPatternState->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return test_task;
        }

        ctsIOTask RequestRecvStatus(_In_ unsigned long* _status_buffer)
        {
            // get_next_task
            auto task = this->ioPatternState->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::RecvCompletion, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Recv;
            test_task.track_io = false;
            test_task.buffer = reinterpret_cast<char*>(_status_buffer);
            test_task.buffer_length = 4;

            // notify_next_task
            this->ioPatternState->notify_next_task(test_task);
            Assert::IsFalse(this->ioPatternState->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return test_task;
        }

        ctsIOTask RequestFin()
        {
            // get_next_task
            auto task = this->ioPatternState->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::RequestFIN, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Recv;
            test_task.track_io = false;
            test_task.buffer_length = 16;

            // notify_next_task
            this->ioPatternState->notify_next_task(test_task);
            Assert::IsFalse(this->ioPatternState->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return test_task;
        }

        ctsIOTask RequestGracefulShutdown()
        {
            // get_next_task
            auto task = this->ioPatternState->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::GracefulShutdown, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::GracefulShutdown;
            test_task.track_io = false;
            test_task.buffer_length = 0;

            // notify_next_task
            this->ioPatternState->notify_next_task(test_task);
            Assert::IsFalse(this->ioPatternState->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return test_task;
        }

        ctsIOTask RequestHardShutdown()
        {
            // get_next_task
            auto task = this->ioPatternState->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::HardShutdown, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::HardShutdown;
            test_task.track_io = false;
            test_task.buffer_length = 0;

            // notify_next_task
            this->ioPatternState->notify_next_task(test_task);
            Assert::IsFalse(this->ioPatternState->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo();

            return test_task;
        }

        void VerifyNoMoreIo() const
        {
            auto no_io_task = this->ioPatternState->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, no_io_task);
        }

    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            ctsConfig::Settings = new ctsConfig::ctsConfigSettings;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            delete ctsConfig::Settings;
        }

        TEST_METHOD(TestGetMaxTransfer)
        {
            this->InitGracefulShutdownTest(100);
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_max_transfer());

            this->InitHardShutdownTest(100);
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_max_transfer());
        }

        TEST_METHOD(TestGetRemainingTransfer)
        {
            this->InitGracefulShutdownTest(100);
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_remaining_transfer());

            this->InitHardShutdownTest(100);
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_remaining_transfer());
        }

        TEST_METHOD(TestSetMaxTransfer)
        {
            static const ctsUnsignedLongLong TestTransferSize(100);

            this->InitGracefulShutdownTest(250);
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_max_transfer());
            this->ioPatternState->set_max_transfer(TestTransferSize);
            Assert::AreEqual(TestTransferSize, this->ioPatternState->get_max_transfer());

            this->InitHardShutdownTest(250);
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_max_transfer());
            this->ioPatternState->set_max_transfer(TestTransferSize);
            Assert::AreEqual(TestTransferSize, this->ioPatternState->get_max_transfer());
        }

        TEST_METHOD(TestGetRemainingTransferAfterSetMaxTransfer)
        {
            static const ctsUnsignedLongLong TestTransferSize(100);

            this->InitGracefulShutdownTest(250);
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_max_transfer());
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_remaining_transfer());

            this->ioPatternState->set_max_transfer(TestTransferSize);
            Assert::AreEqual(TestTransferSize, this->ioPatternState->get_max_transfer());
            Assert::AreEqual(TestTransferSize, this->ioPatternState->get_remaining_transfer());

            this->InitHardShutdownTest(250);
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_max_transfer());
            Assert::AreEqual(s_TransferSize, this->ioPatternState->get_remaining_transfer());

            this->ioPatternState->set_max_transfer(TestTransferSize);
            Assert::AreEqual(TestTransferSize, this->ioPatternState->get_max_transfer());
            Assert::AreEqual(TestTransferSize, this->ioPatternState->get_remaining_transfer());
        }

        TEST_METHOD(TestClientIsCompletedNoIo)
        {
            this->InitGracefulShutdownTest(100, Client);
            Assert::IsFalse(this->ioPatternState->is_completed());

            this->InitHardShutdownTest(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
        }
        TEST_METHOD(TestServerIsCompletedNoIo)
        {
            this->InitGracefulShutdownTest(100, Server);
            Assert::IsFalse(this->ioPatternState->is_completed());
        }

        TEST_METHOD(TestSuccessfullySendConnectionId)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            Assert::IsFalse(this->ioPatternState->is_completed());
        }

        TEST_METHOD(TestFailedSendConnectionId)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            // indicate an error
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(1));
            Assert::IsTrue(this->ioPatternState->is_completed());
        }

        TEST_METHOD(TestSuccessfullyReceiveConnectionId)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            Assert::IsFalse(this->ioPatternState->is_completed());

            this->InitHardShutdownTest(100);
            test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            Assert::IsFalse(this->ioPatternState->is_completed());
        }

        TEST_METHOD(TestFailedReceiveConnectionId)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            // indicate an error
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(1));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100);
            test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            // indicate an error
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(1));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestReceivedTooFewBytesForConnectionId)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength - 1));
            Assert::IsTrue(this->ioPatternState->is_completed());

            this->InitHardShutdownTest(100);
            test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength - 1));
            Assert::IsTrue(this->ioPatternState->is_completed());
        }

        TEST_METHOD(TestClientFailIo)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            test_task = this->RequestMoreIo(50);
            // indicate an error
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(1));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->completed_task(test_task, 50));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(1));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100);
            test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            test_task = this->RequestMoreIo(50);
            // indicate an error
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(1));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->completed_task(test_task, 50));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(1));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();
        }
        TEST_METHOD(TestServerFailIo)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            test_task = this->RequestMoreIo(50);

            // indicate an error
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(1));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->completed_task(test_task, 50));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(1));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientFailTooManyBytes)
        {
            this->InitGracefulShutdownTest(150, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(150);
            test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();
        }
        TEST_METHOD(TestServerFailTooManyBytes)
        {
            this->InitGracefulShutdownTest(150, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientFailTooFewBytes)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // 2 IO tasks - completing too few bytes
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 50));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100);
            test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // 2 IO tasks - completing too few bytes
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 50));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();
        }
        TEST_METHOD(TestServerFailTooFewBytes)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // 2 IO tasks - completing too few bytes
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 50));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClient_GracefulShutdown_FINFailedTooManyBytes)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            // Recv server status
            unsigned long status_code = NO_ERROR;
            test_task = this->RequestRecvStatus(&status_code);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            // Shutdown 
            test_task = this->RequestGracefulShutdown();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            // Request FIN 
            test_task = this->RequestFin();
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, this->ioPatternState->completed_task(test_task, 1));
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(0));
            this->VerifyNoMoreIo();

            // No FIN test for HardShutdown - since HardShutdown just sends a RST
        }

        TEST_METHOD(TestServerFINFailedTooManyBytes)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            // Send status to client
            unsigned long status = NO_ERROR;
            test_task = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            // Request FIN task
            test_task = this->RequestFin();
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, this->ioPatternState->completed_task(test_task, 1));
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsIOPatternProtocolError::ErrorIOFailed, this->ioPatternState->update_error(0));
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientSingleIo)
        {
            this->InitGracefulShutdownTest(100, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Receive server status
            unsigned long status = NO_ERROR;
            test_task = this->RequestRecvStatus(&status);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Shutdown Task
            test_task = this->RequestGracefulShutdown();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Request FIN task
            test_task = this->RequestFin();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100);
            test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Receive server status
            status = NO_ERROR;
            test_task = this->RequestRecvStatus(&status);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Shutdown Task
            test_task = this->RequestHardShutdown();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            this->VerifyNoMoreIo();
        }
        TEST_METHOD(TestServerSingleIo_FIN)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Send status to client
            unsigned long status = NO_ERROR;
            test_task = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Request FIN task
            test_task = this->RequestFin();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerSingleIo_RST)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Send status to client
            unsigned long status = NO_ERROR;
            test_task = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Request FIN task - but that fails with WSAECONNRESET - which is OK if the client wanted to RST instead of FIN
            test_task = this->RequestFin();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(WSAECONNRESET));
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerSingleIo_RST_with_other_error)
        {
            this->InitGracefulShutdownTest(100, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task
            test_task = this->RequestMoreIo(100);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Send status to client
            unsigned long status = NO_ERROR;
            test_task = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Request FIN task - but that fails with WSAECONNRESET - which is OK if the client wanted to RST instead of FIN
            test_task = this->RequestFin();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(WSAECONNABORTED));
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientMultipleIo)
        {
            this->InitGracefulShutdownTest(100 * 3, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            test_task = this->RequestMoreIo(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), this->ioPatternState->get_remaining_transfer());
            // IO Task #2
            test_task = this->RequestMoreIo(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), this->ioPatternState->get_remaining_transfer());
            // IO Task #3
            test_task = this->RequestMoreIo(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Recv the server status
            unsigned long status = NO_ERROR;
            test_task = this->RequestRecvStatus(&status);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Graceful shutdown
            test_task = this->RequestGracefulShutdown();
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Request FIN task
            test_task = this->RequestFin();
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            this->VerifyNoMoreIo();

            this->InitHardShutdownTest(100 * 3);
            test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            test_task = this->RequestMoreIo(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), this->ioPatternState->get_remaining_transfer());
            // IO Task #2
            test_task = this->RequestMoreIo(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), this->ioPatternState->get_remaining_transfer());
            // IO Task #3
            test_task = this->RequestMoreIo(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Recv the server status
            status = NO_ERROR;
            test_task = this->RequestRecvStatus(&status);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // shutdown
            test_task = this->RequestHardShutdown();
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestServerMultipleIo)
        {
            this->InitGracefulShutdownTest(100 * 3, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            test_task = this->RequestMoreIo(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), this->ioPatternState->get_remaining_transfer());
            // IO Task #2
            test_task = this->RequestMoreIo(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), this->ioPatternState->get_remaining_transfer());
            // IO Task #3
            test_task = this->RequestMoreIo(100);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 100));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Send server status
            unsigned long status = NO_ERROR;
            test_task = this->RequestSendStatus(&status);
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, 4));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            // Request FIN task
            test_task = this->RequestFin();
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(test_task, 0));
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->update_error(0));
            this->VerifyNoMoreIo();
        }

        TEST_METHOD(TestClientOverlappingMultipleIo)
        {
            this->InitGracefulShutdownTest(100 * 3, Client);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(100);
            Assert::AreEqual(ctsUnsignedLongLong(200), this->ioPatternState->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(100);
            Assert::AreEqual(ctsUnsignedLongLong(100), this->ioPatternState->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(100);
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo();
            // complete_io 1
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task1, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, this->ioPatternState->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 2
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task2, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, this->ioPatternState->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 3
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task3, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // Recv server status
            //
            unsigned long status_buffer = NO_ERROR;
            ctsIOTask server_status_task = this->RequestRecvStatus(&status_buffer);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(server_status_task, 4));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // Shutdown Task
            //
            ctsIOTask shutdown_task = this->RequestGracefulShutdown();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(shutdown_task, 0));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // Request FIN task
            //
            ctsIOTask final_fin_task = this->RequestFin();
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(final_fin_task, 0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            this->VerifyNoMoreIo();


            this->InitHardShutdownTest(100 * 3);
            test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            test_task1 = this->RequestMoreIo(100);
            Assert::AreEqual(ctsUnsignedLongLong(200), this->ioPatternState->get_remaining_transfer());
            // IO Task #2
            test_task2 = this->RequestMoreIo(100);
            Assert::AreEqual(ctsUnsignedLongLong(100), this->ioPatternState->get_remaining_transfer());
            // IO Task #3
            test_task3 = this->RequestMoreIo(100);
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo();
            // complete_io 1
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task1, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, this->ioPatternState->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 2
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task2, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, this->ioPatternState->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 3
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task3, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // Recv server status
            //
            status_buffer = NO_ERROR;
            server_status_task = this->RequestRecvStatus(&status_buffer);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(server_status_task, 4));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // Shutdown Task
            //
            shutdown_task = this->RequestHardShutdown();
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(final_fin_task, 0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            this->VerifyNoMoreIo();
        }
        TEST_METHOD(TestServerOverlappingMultipleIo)
        {
            this->InitGracefulShutdownTest(100 * 3, Server);
            ctsIOTask test_task = this->RequestConnectionId();
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task, ctsStatistics::ConnectionIdLength));
            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(100);
            Assert::AreEqual(ctsUnsignedLongLong(200), this->ioPatternState->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(100);
            Assert::AreEqual(ctsUnsignedLongLong(100), this->ioPatternState->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(100);
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo();
            // complete_io 1
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task1, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, this->ioPatternState->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 2
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task2, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, this->ioPatternState->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo();
            // complete_io 3
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(test_task3, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // Send server status
            //
            unsigned long status = NO_ERROR;
            ctsIOTask send_status_task = this->RequestSendStatus(&status);
            Assert::AreEqual(ctsIOPatternProtocolError::NoError, this->ioPatternState->completed_task(send_status_task, 100));
            Assert::IsFalse(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
            //
            // Request FIN task
            //
            ctsIOTask fin_task = this->RequestFin();
            Assert::AreEqual(ctsIOPatternProtocolError::SuccessfullyCompleted, this->ioPatternState->completed_task(fin_task, 0));
            Assert::IsTrue(this->ioPatternState->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(0), this->ioPatternState->get_remaining_transfer());
        }
    };
}