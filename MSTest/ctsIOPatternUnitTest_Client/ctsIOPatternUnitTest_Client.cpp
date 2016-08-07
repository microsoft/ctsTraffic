/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <SDKDDKVer.h>
#include "CppUnitTest.h"
// cpp headers
#include <memory>
// ctl headers
#include <ctTimer.hpp>
#include <ctVersionConversion.hpp>
// project headers
#include "ctsIOTask.hpp"
#include "ctsConfig.h"
#include "ctsIOPattern.h"


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;

namespace Microsoft {
    namespace VisualStudio {
        namespace CppUnitTestFramework {

            // Test writer must define specialization of ToString<const Q& q> types used in Assert
            template <> static std::wstring ToString<ctsTraffic::ctsIOTask>(const ctsTraffic::ctsIOTask& _task)
            {
                return ctl::ctString::format_string(
                    L"ctsIOTask:\n"
                    L"\tbuffer: 0x%p\n"
                    L"\tbuffer_length: %u\n"
                    L"\tbuffer_offset: %u\n"
                    L"\texpected_pattern_offset: %u\n"
                    L"\tioAction: %s\n"
                    L"\trio_bufferid: %p\n"
                    L"\ttime_offset_milliseconds: %lld\n"
                    L"\tverify_io: %s\n",
                    _task.buffer, 
                    _task.buffer_length, 
                    _task.buffer_offset, 
                    _task.expected_pattern_offset, 
                    ctsTraffic::ctsIOTask::PrintIOAction(_task.ioAction),
                    _task.rio_bufferid,
                    _task.time_offset_milliseconds,
                    _task.track_io ? L"true" : L"false");
            }
            template <> static std::wstring ToString<ctsTraffic::IOTaskAction>(const ctsTraffic::IOTaskAction& _action)
            {
                return ctsTraffic::ctsIOTask::PrintIOAction(_action);
            }
                
            template <> static std::wstring ToString<ctsTraffic::ctsIOStatus>(const ctsTraffic::ctsIOStatus& _status)
            {
                switch (_status) {
                    case ctsTraffic::ctsIOStatus::ContinueIo: return L"ContinueIo";
                    case ctsTraffic::ctsIOStatus::CompletedIo: return L"CompletedIo";
                    case ctsTraffic::ctsIOStatus::FailedIo: return L"FailedIo";
                }
                return L"Unknown_ctsIOStatus";
            }
        }
    }
}


///
/// statics to return in the Fakes
///
ctsTraffic::ctsSignedLongLong s_TcpBytesPerSecond = 0LL;
ctsTraffic::ctsUnsignedLong s_MaxBufferSize = 0UL;
ctsTraffic::ctsUnsignedLong s_BufferSize = 0UL;
ctsTraffic::ctsUnsignedLongLong s_TransferSize = 0ULL;
bool s_IsListening = false;
ctsTraffic::ctsConfig::MediaStreamSettings s_MediaStreamSettings;

///
/// Fakes
///
namespace ctsTraffic {
    namespace ctsConfig {
        ctsConfigSettings* Settings;

        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error) NOEXCEPT
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsTcpStatistics& _stats) NOEXCEPT
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsUdpStatistics& _stats) NOEXCEPT
        {
        }
        void PrintDebug(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) NOEXCEPT
        {
        }
        void PrintException(const std::exception& e) NOEXCEPT
        {
        }
        void PrintJitterUpdate(long long _sequence_number, long long _sender_qpc, long long _sender_qpf, long long _recevier_qpc, long long _receiver_qpf) NOEXCEPT
        {
        }
        void PrintErrorInfo(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) NOEXCEPT
        {
        }

        bool IsListening() NOEXCEPT
        {
            return s_IsListening;
        }


        const MediaStreamSettings& GetMediaStream() NOEXCEPT
        {
            return s_MediaStreamSettings;
        }

        ctsSignedLongLong GetTcpBytesPerSecond() NOEXCEPT
        {
            return s_TcpBytesPerSecond;
        }
        ctsUnsignedLong GetMaxBufferSize() NOEXCEPT
        {
            return s_MaxBufferSize;
        }
        ctsUnsignedLong GetBufferSize() NOEXCEPT
        {
            return s_BufferSize;
        }
        ctsUnsignedLongLong GetTransferSize() NOEXCEPT
        {
            return s_TransferSize;
        }

        float GetStatusTimeStamp() NOEXCEPT
        {
            return static_cast<float>((ctl::ctTimer::snap_qpc_as_msec() - static_cast<long long>(Settings->StartTimeMilliseconds)) / 1000.0);
        }
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
///   ctsSignedLongLong s_TcpBytesPerSecond
///   ctsUnsignedLong s_MaxBufferSize
///   ctsUnsignedLong s_BufferSize
///   ctsUnsignedLongLong s_TransferSize
///   bool s_IsListening
///

using namespace ctsTraffic;
namespace ctsUnitTest {	

    TEST_CLASS(ctsIOPatternUnitTest_Client)
    {
    private:
        enum TestRole  {
            Client,
            Server
        };
        enum TestShutdownMethod
        {
            Graceful,
            Hard
        };

        static const unsigned long DefaultTransferSize = 10UL;
        void SetTestBaseClassDefaults(TestRole _role, TestShutdownMethod _shutdown = Graceful)
        {
            if (Server == _role && Hard == _shutdown) {
                Assert::Fail(L"Servers only support the default Graceful shutdown");
            }

            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = true;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            ctsConfig::Settings->ConnectionLimit = 8;
            ctsConfig::Settings->TcpShutdown = (Graceful == _shutdown) ? ctsConfig::TcpShutdownType::GracefulShutdown : ctsConfig::TcpShutdownType::HardShutdown;

            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = DefaultTransferSize;
            s_IsListening = (Server == _role);
        }
    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            ctsConfig::Settings = new ctsConfig::ctsConfigSettings;

            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = true;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            ctsConfig::Settings->ConnectionLimit = 8;
        }
        TEST_CLASS_CLEANUP(Cleanup)
        {
            delete ctsConfig::Settings;
        }

        TEST_METHOD(TestBaseClass_SuccessfulSend)
        {
            this->SetTestBaseClassDefaults(Client, Graceful);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate graceful shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            // wait for the server's FIN
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }

        TEST_METHOD(TestBaseClass_SuccessfulMultipleSends)
        {
            this->SetTestBaseClassDefaults(Client, Graceful);
            ctsConfig::Settings->PrePostSends = 2;
            s_BufferSize = DefaultTransferSize;
            s_TransferSize = DefaultTransferSize * 2;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task1 = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task1.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task1.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task1, ctsStatistics::ConnectionIdLength, 0));

            test_task1 = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task1.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task1.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task1).c_str());

            ctsIOTask test_task2 = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task2.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task2.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task2).c_str());

            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task1, this->DefaultTransferSize, 0));
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task2, this->DefaultTransferSize, 0));

            // recv server completion
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate graceful shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            // wait for the server's FIN
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }

        TEST_METHOD(TestBaseClass_SuccessfulSend_HardShutdown)
        {
            this->SetTestBaseClassDefaults(Client, Hard);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate hard shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }

        TEST_METHOD(TestBaseClass_ReceivedNoBytesWithServerStatus)
        {
            this->SetTestBaseClassDefaults(Client);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // receive server status
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 0, 0));
        }

        TEST_METHOD(TestBaseClass_FailedReceivingServerStatus)
        {
            this->SetTestBaseClassDefaults(Client);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 0, 1));
        }

        TEST_METHOD(TestBaseClass_FailSend)
        {
            this->SetTestBaseClassDefaults(Client);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
        }

        TEST_METHOD(TestBaseClass_FailMultipleSends)
        {
            this->SetTestBaseClassDefaults(Client);
            ctsConfig::Settings->PrePostSends = 2;
            s_BufferSize = DefaultTransferSize;
            s_TransferSize = DefaultTransferSize * 2;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task1 = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task1.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task1.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task1, ctsStatistics::ConnectionIdLength, 0));

            test_task1 = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task1.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task1.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task1).c_str());

            ctsIOTask test_task2 = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task2.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task2.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task2).c_str());

            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task1, this->DefaultTransferSize, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task2, this->DefaultTransferSize, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
        }

        TEST_METHOD(TestBaseClass_FailReceivingConnectionId)
        {
            this->SetTestBaseClassDefaults(Client);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 0, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
        }

        TEST_METHOD(TestBaseClass_FailGracefulShutdownAfterSend)
        {
            this->SetTestBaseClassDefaults(Client, Graceful);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate graceful shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 0, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
        }

        TEST_METHOD(TestBaseClass_FailHardShutdownAfterSend)
        {
            this->SetTestBaseClassDefaults(Client, Hard);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate hard shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 0, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
        }

        TEST_METHOD(TestBaseClass_FailGracefulShutdownAFterRecv)
        {
            this->SetTestBaseClassDefaults(Client, Graceful);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate graceful shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 0, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
        }

        TEST_METHOD(TestBaseClass_FailHardShutdownAFterRecv)
        {
            this->SetTestBaseClassDefaults(Client, Hard);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate hard shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 0, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
        }

        TEST_METHOD(TestBaseClass_FailFINAfterSend)
        {
            this->SetTestBaseClassDefaults(Client, Graceful);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate graceful shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            // recv final fin
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 0, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
        }

        TEST_METHOD(TestClientBaseClass_FailFINAfterRecv)
        {
            this->SetTestBaseClassDefaults(Client);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate graceful shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            // wait for the final FIN
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 0, 1));
            Assert::AreEqual(1UL, test_pattern->get_last_error());
        }

        TEST_METHOD(TestClientBaseClass_TooManyBytesOnFINAfterSend)
        {
            this->SetTestBaseClassDefaults(Client);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate graceful shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            // recv the final FIN
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 1, 0));
            Assert::AreEqual(ctsStatusErrorTooMuchDataTransferred, test_pattern->get_last_error());
        }

        TEST_METHOD(TestClientBaseClass_TooManyBytesOnFINAfterRecv)
        {
            this->SetTestBaseClassDefaults(Client);

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(this->DefaultTransferSize, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, this->DefaultTransferSize, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            // initiate graceful shutdown
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            // recv the final FIN
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::FailedIo, test_pattern->complete_io(test_task, 1, 0));
            Assert::AreEqual(ctsStatusErrorTooMuchDataTransferred, test_pattern->get_last_error());
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
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }
        
            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_NotVerifyingBuffersNotUsingSharedBuffer_Rude)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_VerifyingBuffersNotUsingSharedBuffer_Graceful)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = true;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_VerifyingBuffersNotUsingSharedBuffer_Rude)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = true;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_NotVerifyingBuffersUsingSharedBuffer_Graceful)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = true;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_NotVerifyingBuffersUsingSharedBuffer_Rude)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            ctsConfig::Settings->UseSharedBuffer = true;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_MultipleSendsWithISBEnabled)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 0;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            // ISB should indicate to keep 2 sends in flight
            test_pattern->set_ideal_send_backlog(1024 * 2);

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 5; ++io_count) {
                ctsIOTask test_task_one = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task_one.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task_one.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task_one).c_str()).c_str());

                ctsIOTask test_task_two = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task_two.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task_two.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task_two).c_str()).c_str());

                ctsIOTask test_task_three = test_pattern->initiate_io();
                Assert::AreEqual(0UL, test_task_three.buffer_length);
                Assert::AreEqual(IOTaskAction::None, test_task_three.ioAction);

                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task_one, 1024, 0));
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task_two, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_MultipleSendsWithISBEnabledInterleaving)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 0;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            // ISB should indicate to keep 2 sends in flight
            test_pattern->set_ideal_send_backlog(1024 * 2);

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(1024UL, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", 0, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());

            for (unsigned long io_count = 1; io_count < 10; ++io_count) {
                ctsIOTask test_task_one = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task_one.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task_one.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task_one).c_str()).c_str());

                ctsIOTask test_task_three = test_pattern->initiate_io();
                Assert::AreEqual(0UL, test_task_three.buffer_length);
                Assert::AreEqual(IOTaskAction::None, test_task_three.ioAction);

                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task_one, 1024, 0));
            }

            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_LargeNumberOfSendsWithISBEnabled)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 0;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            // ISB should indicate to keep 2 sends in flight
            test_pattern->set_ideal_send_backlog(s_TransferSize);

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            std::vector<ctsIOTask> pended_tasks;
            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());

                pended_tasks.push_back(test_task);
            }

            // all are now pended, next should be empty
            ctsIOTask test_task_empty = test_pattern->initiate_io();
            Assert::AreEqual(0UL, test_task_empty.buffer_length);
            Assert::AreEqual(IOTaskAction::None, test_task_empty.ioAction);

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(pended_tasks[io_count], 1024, 0));

                // after the 10th completion, it will move to the below protocol
                if (io_count < 9) {
                    test_task_empty = test_pattern->initiate_io();
                    Assert::AreEqual(0UL, test_task_empty.buffer_length);
                    Assert::AreEqual(IOTaskAction::None, test_task_empty.ioAction);
                }
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_OneSendInFlightWithISBEnabledWhenISBIsSmallerThanBufferSize)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 0;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            // ISB should indicate to keep 1 send in flight because buffer is larger than ISB
            test_pattern->set_ideal_send_backlog(1024 / 2);

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 20; ++io_count) {
                ctsIOTask test_task = test_pattern->initiate_io();
                Assert::AreEqual(512UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());

                ctsIOTask test_task_three = test_pattern->initiate_io();
                Assert::AreEqual(0UL, test_task_three.buffer_length);
                Assert::AreEqual(IOTaskAction::None, test_task_three.ioAction);

                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 512UL, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PushClient_MultipleSendsWithISBEnabledOffsetFromBufferSize)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Push;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 0;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());
            // ISB should indicate to keep 2 sends in flight
            test_pattern->set_ideal_send_backlog(1024 * 2 - 1);

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 5; ++io_count) {
                ctsIOTask test_task_one = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task_one.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task_one.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task_one).c_str()).c_str());

                ctsIOTask test_task_two = test_pattern->initiate_io();
                Assert::AreEqual(1023UL, test_task_two.buffer_length);
                Assert::AreEqual(IOTaskAction::Send, test_task_two.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task_two).c_str()).c_str());

                ctsIOTask test_task_three = test_pattern->initiate_io();
                Assert::AreEqual(0UL, test_task_three.buffer_length);
                Assert::AreEqual(IOTaskAction::None, test_task_three.ioAction);

                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task_one, 1024, 0));
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task_two, 1023, 0));
            }
            // there are still 5 bytes remaining to be sent
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(5UL, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Send, test_task.ioAction);
            Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", 6, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 5, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
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
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PullClient_NotVerifyingBuffersNotUsingSharedBuffer_Rude)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PullClient_NotVerifyingBuffersNotUsingSharedBuffer_SmallRecvs_Graceful)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 2048;
            s_BufferSize = 2048;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 9; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(2048UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // the final recv is just 1024 bytes
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(1024UL, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", 10, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PullClient_NotVerifyingBuffersNotUsingSharedBuffer_SmallRecvs_Rude)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 2048;
            s_BufferSize = 2048;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 9; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(2048UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // the final recv is just 1024 bytes
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(1024UL, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", 10, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PullClient_VerifyingBuffersNotUsingSharedBuffer_Graceful)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = true;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PullClient_VerifyingBuffersNotUsingSharedBuffer_Rude)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = true;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PullClient_VerifyingBuffersNotUsingSharedBuffer_SmallRecvs_Graceful)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = true;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 2048;
            s_BufferSize = 2048;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 9; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(2048UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // the final recv is just 1024 bytes
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(1024UL, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", 10, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PullClient_VerifyingBuffersNotUsingSharedBuffer_SmallRecvs_Rude)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            ctsConfig::Settings->UseSharedBuffer = false;
            ctsConfig::Settings->ShouldVerifyBuffers = true;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 2048;
            s_BufferSize = 2048;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 9; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(2048UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // the final recv is just 1024 bytes
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(1024UL, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", 10, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
            // "recv" the correct bytes
            ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PullClient_NotVerifyingBuffersUsingSharedBuffer_Graceful)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
            ctsConfig::Settings->UseSharedBuffer = true;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::GracefulShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 0, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
        TEST_METHOD(PullClient_NotVerifyingBuffersUsingSharedBuffer_Rude)
        {
            ctsConfig::Settings->IoPattern = ctsConfig::IoPatternType::Pull;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::HardShutdown;
            ctsConfig::Settings->UseSharedBuffer = true;
            ctsConfig::Settings->ShouldVerifyBuffers = false;
            ctsConfig::Settings->PrePostRecvs = 1;
            ctsConfig::Settings->PrePostSends = 1;
            s_TcpBytesPerSecond = 0LL;
            s_MaxBufferSize = 1024;
            s_BufferSize = 1024;
            s_TransferSize = 1024 * 10;
            s_IsListening = false;

            std::shared_ptr<ctsIOPattern> test_pattern(ctsIOPattern::MakeIOPattern());

            ctsIOTask test_task = test_pattern->initiate_io();
            Assert::AreEqual(ctsStatistics::ConnectionIdLength, test_task.buffer_length);
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, ctsStatistics::ConnectionIdLength, 0));

            for (unsigned long io_count = 0; io_count < 10; ++io_count) {
                test_task = test_pattern->initiate_io();
                Assert::AreEqual(1024UL, test_task.buffer_length);
                Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
                Logger::WriteMessage(ctl::ctString::format_string(L"%u: %s", io_count, ToString<ctsTraffic::ctsIOTask>(test_task).c_str()).c_str());
                // "recv" the correct bytes
                ::memcpy(test_task.buffer, ctsIOPattern::AccessSharedBuffer() + test_task.expected_pattern_offset, test_task.buffer_length);
                Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 1024, 0));
            }

            // recv server completion
            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::Recv, test_task.ioAction);
            Assert::AreEqual(4UL, test_task.buffer_length);
            Assert::AreEqual(ctsIOStatus::ContinueIo, test_pattern->complete_io(test_task, 4, 0));

            test_task = test_pattern->initiate_io();
            Assert::AreEqual(IOTaskAction::HardShutdown, test_task.ioAction);
            Logger::WriteMessage(ToString<ctsTraffic::ctsIOTask>(test_task).c_str());
            Assert::AreEqual(ctsIOStatus::CompletedIo, test_pattern->complete_io(test_task, 0, 0));
        }
    };
}