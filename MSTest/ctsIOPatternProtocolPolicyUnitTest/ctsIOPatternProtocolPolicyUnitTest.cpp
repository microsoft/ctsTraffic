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

#include "ctsStatistics.hpp"
#include "ctsIOPatternProtocolPolicy.hpp"

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
        SendConnectionGuid,
        RecvConnectionGuid,
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
        };
    */
    template<> inline std::wstring ToString<ctsTraffic::ctsIOPatternProtocolTask>(const ctsTraffic::ctsIOPatternProtocolTask& _value)
    {
        switch (_value)
        {
            case ctsTraffic::ctsIOPatternProtocolTask::NoIo: return L"NoIo";
            case ctsTraffic::ctsIOPatternProtocolTask::SendConnectionGuid: return L"SendConnectionGuid";
            case ctsTraffic::ctsIOPatternProtocolTask::RecvConnectionGuid: return L"RecvConnectionGuid";
            case ctsTraffic::ctsIOPatternProtocolTask::MoreIo: return L"MoreIo";
            case ctsTraffic::ctsIOPatternProtocolTask::SendCompletion: return L"SendCompletion";
            case ctsTraffic::ctsIOPatternProtocolTask::RecvCompletion: return L"RecvCompletion";
            case ctsTraffic::ctsIOPatternProtocolTask::GracefulShutdown: return L"GracefulShutdown";
            case ctsTraffic::ctsIOPatternProtocolTask::HardShutdown: return L"HardShutdown";
            case ctsTraffic::ctsIOPatternProtocolTask::RequestFIN: return L"RequestFIN";
        }

        Assert::Fail(L"Unknown ctsIOPatternProtocolTask");
    }

    template<> inline std::wstring ToString<ctsTraffic::ctsIOPatternProtocolError>(const ctsTraffic::ctsIOPatternProtocolError& _value)
    {
        switch (_value)
        {
            case ctsTraffic::ctsIOPatternProtocolError::NotProtocolError: return L"NotProtocolError";
            case ctsTraffic::ctsIOPatternProtocolError::NoConnectionGuid: return L"NoConnectionGuid";
            case ctsTraffic::ctsIOPatternProtocolError::ZeroByteXfer: return L"ZeroByteXfer";
            case ctsTraffic::ctsIOPatternProtocolError::TooManyBytes: return L"TooManyBytes";
            case ctsTraffic::ctsIOPatternProtocolError::TooFewBytes: return L"TooFewBytes";
            case ctsTraffic::ctsIOPatternProtocolError::CorruptedXfer: return L"CorruptedXfer";
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
    TEST_CLASS(ctsIOPatternProtocolPolicyUnitTest)
    {
    private:
        unsigned long ZERO = 0UL;
        unsigned long TEST_ERROR = 1UL;

        std::unique_ptr<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpClient>> InitClientGracefulShutdownTest(unsigned long long _test_transfer_size) const
        {
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            s_Listening = false;
            s_TransferSize = _test_transfer_size;

            auto return_pattern(std::make_unique<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpClient>>());
            Assert::IsFalse(return_pattern->is_completed());
            Assert::AreEqual(s_TransferSize, return_pattern->get_max_transfer());
            Assert::AreEqual(s_TransferSize, return_pattern->get_remaining_transfer());
            return return_pattern;
        }
        std::unique_ptr<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpServer>> InitServerGracefulShutdownTest(unsigned long long _test_transfer_size) const
        {
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            s_Listening = true;
            s_TransferSize = _test_transfer_size;

            auto return_pattern(std::make_unique<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpServer>>());
            Assert::IsFalse(return_pattern->is_completed());
            Assert::AreEqual(s_TransferSize, return_pattern->get_max_transfer());
            Assert::AreEqual(s_TransferSize, return_pattern->get_remaining_transfer());
            return return_pattern;
        }
        std::unique_ptr<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpClient>> InitClientHardShutdownTest(unsigned long long _test_transfer_size) const
        {
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            s_Listening = false; // client-only
            s_TransferSize = _test_transfer_size;

            auto return_pattern(std::make_unique<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpClient>>());
            Assert::IsFalse(return_pattern->is_completed());
            Assert::AreEqual(s_TransferSize, return_pattern->get_max_transfer());
            Assert::AreEqual(s_TransferSize, return_pattern->get_remaining_transfer());
            return return_pattern;
        }
        std::unique_ptr<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolUdp>> InitUdpClientTest(unsigned long long _test_transfer_size) const
        {
            s_Listening = false;
            s_TransferSize = _test_transfer_size;

            auto return_pattern(std::make_unique<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolUdp>>());
            Assert::IsFalse(return_pattern->is_completed());
            Assert::AreEqual(s_TransferSize, return_pattern->get_max_transfer());
            Assert::AreEqual(s_TransferSize, return_pattern->get_remaining_transfer());
            return return_pattern;
        }
        std::unique_ptr<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolUdp>> InitUdpServerTest(unsigned long long _test_transfer_size) const
        {
            s_Listening = true;
            s_TransferSize = _test_transfer_size;

            auto return_pattern(std::make_unique<ctsIOPatternProtocolPolicy<ctsIOPatternProtocolUdp>>());
            Assert::IsFalse(return_pattern->is_completed());
            Assert::AreEqual(s_TransferSize, return_pattern->get_max_transfer());
            Assert::AreEqual(s_TransferSize, return_pattern->get_remaining_transfer());
            return return_pattern;
        }
        //
        // Private members to implement building out a ctsIOTask for each task
        //
        template <typename IOPattern>
        ctsIOTask RequestConnectionGuid(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern)
        {
            auto task = _ioPattern->get_next_task();
            if (s_Listening)
            {
                Assert::AreEqual(ctsIOPatternProtocolTask::SendConnectionGuid, task);
            }
            else
            {
                Assert::AreEqual(ctsIOPatternProtocolTask::RecvConnectionGuid, task);
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

            _ioPattern->notify_next_task(test_task);
            Assert::IsFalse(_ioPattern->is_completed());

            return test_task;
        }

        template <typename IOPattern>
        ctsIOTask RequestMoreIo(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern, unsigned long _buffer_length)
        {
            auto task = _ioPattern->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::MoreIo, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Recv;
            test_task.track_io = true;
            test_task.buffer_length = _buffer_length;

            _ioPattern->notify_next_task(test_task);
            Assert::IsFalse(_ioPattern->is_completed());

            return test_task;
        }

        template <typename IOPattern>
        ctsIOTask RequestSendStatus(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern, _In_ unsigned long* _status_buffer)
        {
            // get_next_task
            auto task = _ioPattern->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::SendCompletion, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            test_task.track_io = false;
            test_task.buffer = reinterpret_cast<char*>(_status_buffer);
            test_task.buffer_length = 4;

            // notify_next_task
            _ioPattern->notify_next_task(test_task);
            Assert::IsFalse(_ioPattern->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo(_ioPattern);

            return test_task;
        }

        template <typename IOPattern>
        ctsIOTask RequestRecvStatus(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern, _In_ unsigned long* _status_buffer)
        {
            // get_next_task
            auto task = _ioPattern->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::RecvCompletion, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Recv;
            test_task.track_io = false;
            test_task.buffer = reinterpret_cast<char*>(_status_buffer);
            test_task.buffer_length = 4;

            // notify_next_task
            _ioPattern->notify_next_task(test_task);
            Assert::IsFalse(_ioPattern->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo(_ioPattern);

            return test_task;
        }

        template <typename IOPattern>
        ctsIOTask RequestFin(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern)
        {
            // get_next_task
            auto task = _ioPattern->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::RequestFIN, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Recv;
            test_task.track_io = false;
            test_task.buffer_length = 16;

            // notify_next_task
            _ioPattern->notify_next_task(test_task);
            Assert::IsFalse(_ioPattern->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo(_ioPattern);

            return test_task;
        }

        template <typename IOPattern>
        ctsIOTask RequestGracefulShutdown(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern)
        {
            // get_next_task
            auto task = _ioPattern->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::GracefulShutdown, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::GracefulShutdown;
            test_task.track_io = false;
            test_task.buffer_length = 0;

            // notify_next_task
            _ioPattern->notify_next_task(test_task);
            Assert::IsFalse(_ioPattern->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo(_ioPattern);

            return test_task;
        }

        template <typename IOPattern>
        ctsIOTask RequestHardShutdown(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern)
        {
            // get_next_task
            auto task = _ioPattern->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::HardShutdown, task);

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::HardShutdown;
            test_task.track_io = false;
            test_task.buffer_length = 0;

            // notify_next_task
            _ioPattern->notify_next_task(test_task);
            Assert::IsFalse(_ioPattern->is_completed());

            // should return NoIO since we are waiting on this task
            this->VerifyNoMoreIo(_ioPattern);

            return test_task;
        }

        template <typename IOPattern>
        void VerifyNoMoreIo(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern)
        {
            auto no_io_task = _ioPattern->get_next_task();
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, no_io_task);
        }

        template <typename IOPattern>
        void FailIoAndVerify(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern)
        {
            Assert::AreEqual(TEST_ERROR, _ioPattern->update_last_error(TEST_ERROR));
            Assert::IsTrue(_ioPattern->is_completed());
            Assert::AreEqual(TEST_ERROR, _ioPattern->get_last_error());
            Assert::AreEqual(TEST_ERROR, _ioPattern->update_last_error(NO_ERROR));
            Assert::AreEqual(TEST_ERROR, _ioPattern->get_last_error());
        }

        template <typename IOPattern>
        void CompleteIoAndVerifySuccess(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern, ctsIOTask _task, unsigned long _bytes)
        {
            Assert::AreEqual(ZERO, _ioPattern->update_last_error(ZERO));
            _ioPattern->completed_task(_task, _bytes);
            Assert::AreEqual(ZERO, _ioPattern->get_last_error());
            Assert::AreEqual(ZERO, _ioPattern->update_last_error(ZERO));
        }

        template <typename IOPattern>
        void RequestAndCompleteConnectionGuid(std::unique_ptr<ctsIOPatternProtocolPolicy<IOPattern>>& _ioPattern)
        {
            ctsIOTask test_task = this->RequestConnectionGuid(_ioPattern);
            _ioPattern->completed_task(test_task, ctsStatistics::ConnectionIdLength);
            Assert::AreEqual(ZERO, _ioPattern->get_last_error());
            Assert::IsFalse(_ioPattern->is_completed());
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


        TEST_METHOD(GracefulShutdownSetMaxTransfer)
        {
            ctsUnsignedLongLong TestTransferSize(100);

            auto io_pattern = this->InitClientGracefulShutdownTest(250);
            Assert::AreEqual(s_TransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(ctsUnsignedLongLong(250), io_pattern->get_remaining_transfer());

            io_pattern->set_max_transfer(TestTransferSize);
            Assert::AreEqual(TestTransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(TestTransferSize, io_pattern->get_remaining_transfer());
        }

        TEST_METHOD(HardShutdownSetMaxTransfer)
        {
            ctsUnsignedLongLong TestTransferSize(100);

            auto io_pattern = this->InitClientHardShutdownTest(250);
            Assert::AreEqual(s_TransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(ctsUnsignedLongLong(250), io_pattern->get_remaining_transfer());

            io_pattern->set_max_transfer(TestTransferSize);
            Assert::AreEqual(TestTransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(TestTransferSize, io_pattern->get_remaining_transfer());
        }

        TEST_METHOD(TCPServerShutdownSetMaxTransfer)
        {
            ctsUnsignedLongLong TestTransferSize(100);

            auto io_pattern = this->InitServerGracefulShutdownTest(250);
            Assert::AreEqual(s_TransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(ctsUnsignedLongLong(250), io_pattern->get_remaining_transfer());

            io_pattern->set_max_transfer(TestTransferSize);
            Assert::AreEqual(TestTransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(TestTransferSize, io_pattern->get_remaining_transfer());
        }

        TEST_METHOD(UdpClientSetMaxTransfer)
        {
            ctsUnsignedLongLong TestTransferSize(100);

            auto io_pattern = this->InitUdpClientTest(250);
            Assert::AreEqual(s_TransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(ctsUnsignedLongLong(250), io_pattern->get_remaining_transfer());

            io_pattern->set_max_transfer(TestTransferSize);
            Assert::AreEqual(TestTransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(TestTransferSize, io_pattern->get_remaining_transfer());
        }

        TEST_METHOD(UdpServerSetMaxTransfer)
        {
            ctsUnsignedLongLong TestTransferSize(100);

            auto io_pattern = this->InitUdpServerTest(250);
            Assert::AreEqual(s_TransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(ctsUnsignedLongLong(250), io_pattern->get_remaining_transfer());

            io_pattern->set_max_transfer(TestTransferSize);
            Assert::AreEqual(TestTransferSize, io_pattern->get_max_transfer());
            Assert::AreEqual(TestTransferSize, io_pattern->get_remaining_transfer());
        }


        TEST_METHOD(SuccessfullySendConnectionGuid)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, ctsStatistics::ConnectionIdLength);
            Assert::IsFalse(io_pattern->is_completed());
        }

        TEST_METHOD(UDPSuccessfullySendConnectionGuid)
        {
            auto io_pattern = this->InitUdpServerTest(100);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, ctsStatistics::ConnectionIdLength);
            Assert::IsFalse(io_pattern->is_completed());
        }

        TEST_METHOD(FailedSendConnectionGuid)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            this->FailIoAndVerify(io_pattern);
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPFailedSendConnectionGuid)
        {
            auto io_pattern = this->InitUdpServerTest(100);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            this->FailIoAndVerify(io_pattern);
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownSuccessfullyReceiveConnectionGuid)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(250);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, ctsStatistics::ConnectionIdLength);
            Assert::IsFalse(io_pattern->is_completed());
        }

        TEST_METHOD(HardShutdownSuccessfullyReceiveConnectionGuid)
        {
            auto io_pattern = this->InitClientHardShutdownTest(250);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, ctsStatistics::ConnectionIdLength);
            Assert::IsFalse(io_pattern->is_completed());
        }

        TEST_METHOD(UDPSuccessfullyReceiveConnectionGuid)
        {
            auto io_pattern = this->InitUdpClientTest(250);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, ctsStatistics::ConnectionIdLength);
            Assert::IsFalse(io_pattern->is_completed());
        }

        TEST_METHOD(GracefulShutdownFailedReceiveConnectionGuid)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(250);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            this->FailIoAndVerify(io_pattern);
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownFailedReceiveConnectionGuid)
        {
            auto io_pattern = this->InitClientHardShutdownTest(250);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            this->FailIoAndVerify(io_pattern);
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPFailedReceiveConnectionGuid)
        {
            auto io_pattern = this->InitUdpClientTest(250);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            this->FailIoAndVerify(io_pattern);
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownContinueIoAfterFailure)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);
            this->FailIoAndVerify(io_pattern);
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
        }

        TEST_METHOD(HardShutdownContinueIoAfterFailure)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);
            this->FailIoAndVerify(io_pattern);
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
        }

        TEST_METHOD(UDPContinueIoAfterFailure)
        {
            auto io_pattern = this->InitUdpClientTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);
            this->FailIoAndVerify(io_pattern);
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
        }

        TEST_METHOD(GracefulShutdownReceivedTooFewBytesForConnectionGuid)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(250);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, ctsStatistics::ConnectionIdLength - 1);
            Assert::AreEqual(ctsIOPatternProtocolError::NoConnectionGuid, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownReceivedTooFewBytesForConnectionGuid)
        {
            auto io_pattern = this->InitClientHardShutdownTest(250);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, ctsStatistics::ConnectionIdLength - 1);
            Assert::AreEqual(ctsIOPatternProtocolError::NoConnectionGuid, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPReceivedTooFewBytesForConnectionGuid)
        {
            auto io_pattern = this->InitUdpClientTest(250);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, ctsStatistics::ConnectionIdLength - 1);
            Assert::AreEqual(ctsIOPatternProtocolError::NoConnectionGuid, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownReceivedZeroBytes)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, 0);
            Assert::AreEqual(ctsIOPatternProtocolError::NoConnectionGuid, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownReceivedZeroBytes)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, 0);
            Assert::AreEqual(ctsIOPatternProtocolError::NoConnectionGuid, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPReceivedZeroBytes)
        {
            auto io_pattern = this->InitUdpClientTest(100);
            ctsIOTask test_task = this->RequestConnectionGuid(io_pattern);
            io_pattern->completed_task(test_task, 0);
            Assert::AreEqual(ctsIOPatternProtocolError::NoConnectionGuid, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownReceivedZeroBytesAfterConnectionGuid)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 0);
            Assert::AreEqual(ctsIOPatternProtocolError::ZeroByteXfer, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownReceivedZeroBytesAfterConnectionGuid)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 0);
            Assert::AreEqual(ctsIOPatternProtocolError::ZeroByteXfer, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerShutdownReceivedZeroBytesAfterConnectionGuid)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 0);
            Assert::AreEqual(ctsIOPatternProtocolError::ZeroByteXfer, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        /* Receiving a 0-byte datagram is just fine, differing from TCP behavior */
        TEST_METHOD(UDPReceivedZeroBytesAfterConnectionGuid)
        {
            auto io_pattern = this->InitUdpClientTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 0);
            Assert::AreEqual(ctsIOPatternProtocolError::NotProtocolError, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::IsFalse(io_pattern->is_completed());
        }

        TEST_METHOD(GracefulShutdownClientFailIo)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(250);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 50);
            this->FailIoAndVerify(io_pattern);
            io_pattern->completed_task(test_task, 50);
            Assert::AreEqual(TEST_ERROR, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(TEST_ERROR, io_pattern->update_last_error(TEST_ERROR));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownClientFailIo)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 50);
            this->FailIoAndVerify(io_pattern);
            io_pattern->completed_task(test_task, 50);
            Assert::AreEqual(TEST_ERROR, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(TEST_ERROR, io_pattern->update_last_error(TEST_ERROR));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerFailIo)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 50);
            this->FailIoAndVerify(io_pattern);
            io_pattern->completed_task(test_task, 50);
            Assert::AreEqual(TEST_ERROR, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(TEST_ERROR, io_pattern->update_last_error(TEST_ERROR));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPClientFailIo)
        {
            auto io_pattern = this->InitUdpClientTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 50);
            this->FailIoAndVerify(io_pattern);
            io_pattern->completed_task(test_task, 50);
            Assert::AreEqual(TEST_ERROR, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(TEST_ERROR, io_pattern->update_last_error(TEST_ERROR));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPServerFailIo)
        {
            auto io_pattern = this->InitUdpServerTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 50);
            this->FailIoAndVerify(io_pattern);
            io_pattern->completed_task(test_task, 50);
            Assert::AreEqual(TEST_ERROR, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(TEST_ERROR, io_pattern->update_last_error(TEST_ERROR));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownClientFailTooManyBytes)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(150);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());

            test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorTooMuchDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownClientFailTooManyBytes)
        {
            auto io_pattern = this->InitClientHardShutdownTest(150);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());

            test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorTooMuchDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerFailTooManyBytes)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(150);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());

            test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorTooMuchDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPClientFailTooManyBytes)
        {
            auto io_pattern = this->InitUdpClientTest(150);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());

            test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorTooMuchDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPServerFailTooManyBytes)
        {
            auto io_pattern = this->InitUdpServerTest(150);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());

            test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 100);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorTooMuchDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownClientFailTooFewBytes)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // 2 IO tasks - completing too few bytes
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 50); // only 50 of 100 bytes
            Assert::IsFalse(io_pattern->is_completed());

            test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 0); // complete zero bytes - indicating FIN
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorNotAllDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownClientFailTooFewBytes)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // 2 IO tasks - completing too few bytes
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 50); // only 50 of 100 bytes
            Assert::IsFalse(io_pattern->is_completed());

            test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 0); // complete zero bytes - indicating FIN
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorNotAllDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerFailTooFewBytes)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // 2 IO tasks - completing too few bytes
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 50); // only 50 of 100 bytes

            test_task = this->RequestMoreIo(io_pattern, 100);
            io_pattern->completed_task(test_task, 0); // complete zero bytes - indicating FIN
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorNotAllDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }


        TEST_METHOD(GracefulShutdownClientFailFinalStatusTooFewBytes)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Recv server status (should be 4 bytes - only completing 2)
            unsigned long status_code = ZERO;
            test_task = this->RequestRecvStatus(io_pattern, &status_code);
            io_pattern->completed_task(test_task, 2);
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorNotAllDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownClientFailFinalStatusTooFewBytes)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Recv server status (should be 4 bytes - only completing 2)
            unsigned long status_code = ZERO;
            test_task = this->RequestRecvStatus(io_pattern, &status_code);
            io_pattern->completed_task(test_task, 2);
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorNotAllDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        /* UDP doesn't send a final status */

        TEST_METHOD(GracefulShutdownClientFailFinalStatusZeroBytes)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Recv server status (should be 4 bytes - completing 0 - as in a FIN)
            unsigned long status_code = ZERO;
            test_task = this->RequestRecvStatus(io_pattern, &status_code);
            io_pattern->completed_task(test_task, 0);
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorNotAllDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownClientFailFinalStatusZeroBytes)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Recv server status (should be 4 bytes - only completing 0 - as in a FIN)
            unsigned long status_code = ZERO;
            test_task = this->RequestRecvStatus(io_pattern, &status_code);
            io_pattern->completed_task(test_task, 0);
            Assert::AreEqual(ctsIOPatternProtocolError::TooFewBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsStatusErrorNotAllDataTransferred, io_pattern->update_last_error(ZERO));
            Assert::IsTrue(io_pattern->is_completed());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(Client_GracefulShutdown_FINFailedTooManyBytes)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Recv server status (4 bytes)
            unsigned long status_code = ZERO;
            test_task = this->RequestRecvStatus(io_pattern, &status_code);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Shutdown (0 byte FIN)
            test_task = this->RequestGracefulShutdown(io_pattern);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Request FIN 
            test_task = this->RequestFin(io_pattern);
            io_pattern->completed_task(test_task, 1);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsStatusErrorTooMuchDataTransferred, io_pattern->update_last_error(ZERO));
            this->VerifyNoMoreIo(io_pattern);

            // No FIN test for HardShutdown - since HardShutdown just sends a RST
        }

        TEST_METHOD(TCPServerFINFailedTooManyBytes)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Send status to client
            unsigned long status = ZERO;
            test_task = this->RequestSendStatus(io_pattern, &status);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Request FIN task
            test_task = this->RequestFin(io_pattern);
            io_pattern->completed_task(test_task, 1);
            Assert::AreEqual(ctsIOPatternProtocolError::TooManyBytes, ctsIOPatternStateCheckProtocolError(io_pattern->get_last_error()));
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsStatusErrorTooMuchDataTransferred, io_pattern->update_last_error(ZERO));
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownClientSingleIo)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Receive server status
            unsigned long status = ZERO;
            test_task = this->RequestRecvStatus(io_pattern, &status);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Shutdown Task
            test_task = this->RequestGracefulShutdown(io_pattern);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Request FIN task
            test_task = this->RequestFin(io_pattern);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownClientSingleIo)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Receive server status
            unsigned long status = ZERO;
            test_task = this->RequestRecvStatus(io_pattern, &status);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Shutdown Task
            test_task = this->RequestHardShutdown(io_pattern);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPClientSingleIo)
        {
            auto io_pattern = this->InitUdpClientTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPServerSingleIo)
        {
            auto io_pattern = this->InitUdpServerTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerSingleIo_FIN)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Send status to client
            unsigned long status = ZERO;
            test_task = this->RequestSendStatus(io_pattern, &status);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Request FIN task
            test_task = this->RequestFin(io_pattern);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerSingleIo_RST_connreset)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Send status to client
            unsigned long status = ZERO;
            test_task = this->RequestSendStatus(io_pattern, &status);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Request FIN task - but that fails with WSAECONNRESET - which is OK if the client wanted to RST instead of FIN
            test_task = this->RequestFin(io_pattern);
            Assert::AreEqual(ZERO, io_pattern->update_last_error(WSAECONNRESET));
            //            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerSingleIo_RST_connaborted)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Send status to client
            unsigned long status = ZERO;
            test_task = this->RequestSendStatus(io_pattern, &status);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Request FIN task - but that fails with WSAECONNABORTED - which is OK if the client wanted to RST instead of FIN
            test_task = this->RequestFin(io_pattern);
            Assert::AreEqual(ZERO, io_pattern->update_last_error(WSAECONNABORTED));
            //            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerSingleIo_RST_timedout)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Send status to client
            unsigned long status = ZERO;
            test_task = this->RequestSendStatus(io_pattern, &status);
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Request FIN task - but that fails with WSAETIMEDOUT - which is OK if the client wanted to RST instead of FIN
            test_task = this->RequestFin(io_pattern);
            Assert::AreEqual(ZERO, io_pattern->update_last_error(WSAETIMEDOUT));
            //            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownClientMultipleIo)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());

            // IO Task #2
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());

            // IO Task #3
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Recv the server status
            unsigned long status = ZERO;
            test_task = this->RequestRecvStatus(io_pattern, &status);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Graceful shutdown
            test_task = this->RequestGracefulShutdown(io_pattern);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Request FIN task
            test_task = this->RequestFin(io_pattern);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownClientMultipleIo)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());

            // IO Task #2
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());

            // IO Task #3
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Recv the server status
            unsigned long status = ZERO;
            test_task = this->RequestRecvStatus(io_pattern, &status);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // shutdown
            test_task = this->RequestHardShutdown(io_pattern);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerMultipleIo)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());

            // IO Task #2
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());

            // IO Task #3
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Send server status
            unsigned long status = ZERO;
            test_task = this->RequestSendStatus(io_pattern, &status);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            // Request FIN task
            test_task = this->RequestFin(io_pattern);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ZERO, io_pattern->update_last_error(ZERO));
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPClientMultipleIo)
        {
            auto io_pattern = this->InitUdpClientTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());

            // IO Task #2
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());

            // IO Task #3
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            this->VerifyNoMoreIo(io_pattern);
        }
        TEST_METHOD(UDPServerMultipleIo)
        {
            auto io_pattern = this->InitUdpServerTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());

            // IO Task #2
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());

            // IO Task #3
            test_task = this->RequestMoreIo(io_pattern, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            this->CompleteIoAndVerifySuccess(io_pattern, test_task, 100);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownClientOverlappingMultipleIo)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task1, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task2, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task3, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // Recv server status
            //
            unsigned long status_buffer = ZERO;
            ctsIOTask server_status_task = this->RequestRecvStatus(io_pattern, &status_buffer);
            this->CompleteIoAndVerifySuccess(io_pattern, server_status_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // Shutdown Task
            //
            ctsIOTask shutdown_task = this->RequestGracefulShutdown(io_pattern);
            this->CompleteIoAndVerifySuccess(io_pattern, shutdown_task, 0);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // Request FIN task
            //
            ctsIOTask final_fin_task = this->RequestFin(io_pattern);
            this->CompleteIoAndVerifySuccess(io_pattern, final_fin_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownClientOverlappingMultipleIo)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task1, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task2, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task3, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // Recv server status
            //
            unsigned long status_buffer = ZERO;
            ctsIOTask server_status_task = this->RequestRecvStatus(io_pattern, &status_buffer);
            this->CompleteIoAndVerifySuccess(io_pattern, server_status_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // Shutdown Task
            //
            ctsIOTask shutdown_task = this->RequestHardShutdown(io_pattern);
            this->CompleteIoAndVerifySuccess(io_pattern, shutdown_task, 4);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerOverlappingMultipleIo)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task1, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task2, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task3, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // Send server status
            //
            unsigned long status = ZERO;
            ctsIOTask send_status_task = this->RequestSendStatus(io_pattern, &status);
            this->CompleteIoAndVerifySuccess(io_pattern, send_status_task, 4);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // Request FIN task
            //
            ctsIOTask fin_task = this->RequestFin(io_pattern);
            this->CompleteIoAndVerifySuccess(io_pattern, fin_task, 0);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPClientOverlappingMultipleIo)
        {
            auto io_pattern = this->InitUdpClientTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task1, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task2, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task3, 100);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            this->VerifyNoMoreIo(io_pattern);
        }
        TEST_METHOD(UDPServerOverlappingMultipleIo)
        {
            auto io_pattern = this->InitUdpServerTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task1, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task2, 100);
            Assert::IsFalse(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3
            //
            this->CompleteIoAndVerifySuccess(io_pattern, test_task3, 100);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());

            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(GracefulShutdownFailingOneIoWithClientOverlappingMultipleIo)
        {
            auto io_pattern = this->InitClientGracefulShutdownTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->FailIoAndVerify(io_pattern);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task2, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task3, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // Since failed should be no more IO
            //
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(HardShutdownFailingOneIoWithClientOverlappingMultipleIo)
        {
            auto io_pattern = this->InitClientHardShutdownTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->FailIoAndVerify(io_pattern);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task2, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task3, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // Since failed should be no more IO
            //
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(TCPServerFailingOneIoWithOverlappingMultipleIo)
        {
            auto io_pattern = this->InitServerGracefulShutdownTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->FailIoAndVerify(io_pattern);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task2, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task3, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // Since failed should be no more IO
            //
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPClientFailingOneIoWithOverlappingMultipleIo)
        {
            auto io_pattern = this->InitUdpClientTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->FailIoAndVerify(io_pattern);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task2, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task3, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // Since failed should be no more IO
            //
            this->VerifyNoMoreIo(io_pattern);
        }

        TEST_METHOD(UDPServerFailingOneIoWithOverlappingMultipleIo)
        {
            auto io_pattern = this->InitUdpServerTest(100 * 3);
            this->RequestAndCompleteConnectionGuid(io_pattern);

            // IO Task #1
            ctsIOTask test_task1 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(200), io_pattern->get_remaining_transfer());
            // IO Task #2
            ctsIOTask test_task2 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(100), io_pattern->get_remaining_transfer());
            // IO Task #3
            ctsIOTask test_task3 = this->RequestMoreIo(io_pattern, 100);
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            //
            // all IO is now posted
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 1
            //
            this->FailIoAndVerify(io_pattern);
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 2 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task2, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // should return NoIO while IO is still pended
            //
            this->VerifyNoMoreIo(io_pattern);
            //
            // complete_io 3 successfully - after the first failed
            //
            Assert::AreEqual(1UL, io_pattern->update_last_error(ZERO));
            io_pattern->completed_task(test_task3, 100);
            Assert::AreEqual(1UL, io_pattern->get_last_error());
            Assert::IsTrue(io_pattern->is_completed());
            Assert::AreEqual(ctsUnsignedLongLong(ZERO), io_pattern->get_remaining_transfer());
            Assert::AreEqual(ctsIOPatternProtocolTask::NoIo, io_pattern->get_next_task());
            //
            // Since failed should be no more IO
            //
            this->VerifyNoMoreIo(io_pattern);
        }
    };
}