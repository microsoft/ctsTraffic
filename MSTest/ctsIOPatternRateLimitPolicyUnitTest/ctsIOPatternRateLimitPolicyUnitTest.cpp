/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#define CTSTRAFFIC_UNIT_TESTS

#include <SDKDDKVer.h>
#include "CppUnitTest.h"

#include <memory>

#include <ctString.hpp>

#include "ctsStatistics.hpp"
#include "ctsIOPatternRateLimitPolicy.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft {
    namespace VisualStudio {
        namespace CppUnitTestFramework {
            template<> static std::wstring ToString<ctsTraffic::ctsUnsignedLongLong>(const ctsTraffic::ctsUnsignedLongLong& _value)
            {
                return std::to_wstring(static_cast<unsigned long long>(_value));
            }
        }
    }
}

long long s_QpcTime = 0LL;

ctsTraffic::ctsUnsignedLongLong s_TransferSize = 0ULL;
ctsTraffic::ctsSignedLongLong s_TcpBytesPerSecond = 0LL;

///
/// Fakes
///
namespace ctl {
    namespace ctTimer {
        long long snap_qpc_as_msec() noexcept
        {
            return s_QpcTime;
        }
    }
}
namespace ctsTraffic {
    namespace ctsConfig {
        ctsConfigSettings* Settings;

        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error) noexcept
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsTcpStatistics& _stats) noexcept
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsUdpStatistics& _stats) noexcept
        {
        }
        void PrintDebug(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) noexcept
        {
            va_list va_args;
            va_start(va_args, _text);
            Logger::WriteMessage(ctl::ctString::format_string_va(_text, va_args).c_str());
            va_end(va_args);
        }
        void PrintException(const std::exception& e) noexcept
        {
        }
        void PrintJitterUpdate(long long _sequence_number, long long _sender_qpc, long long _sender_qpf, long long _recevier_qpc, long long _receiver_qpf) noexcept
        {
        }
        void PrintErrorInfo(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) noexcept
        {
        }

        ctsUnsignedLongLong GetTransferSize() noexcept
        {
            return s_TransferSize;
        }

        ctsSignedLongLong GetTcpBytesPerSecond() noexcept
        {
            return s_TcpBytesPerSecond;
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
namespace ctsUnitTest {
    TEST_CLASS(ctsIOPatternRateLimitPolicyUnitTest)
    {
    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            ctsConfig::Settings = new ctsConfig::ctsConfigSettings;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;

            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            delete ctsConfig::Settings;
        }

        TEST_METHOD(SendingDontThrottlePolicy)
        {
            s_TcpBytesPerSecond = 1LL;
            s_QpcTime = 1LL;

            auto NoTimer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitDontThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            NoTimer->update_time_offset(test_task, 100);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            s_QpcTime = 2LL;
            NoTimer->update_time_offset(test_task, 100);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
        }

        TEST_METHOD(ReceivingDontThrottlePolicy)
        {
            s_TcpBytesPerSecond = 1LL;
            s_QpcTime = 1LL;

            auto NoTimer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitDontThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Recv;

            NoTimer->update_time_offset(test_task, 100);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            s_QpcTime = 2LL;
            NoTimer->update_time_offset(test_task, 100);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
        }

        TEST_METHOD(ReceivingThrottlingPolicy)
        {
            s_TcpBytesPerSecond = 1LL;
            s_QpcTime = 1LL;

            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Recv;

            test_timer->update_time_offset(test_task, 100);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            s_QpcTime = 2LL;
            test_timer->update_time_offset(test_task, 100);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
        }


        ///
        /// tests if calling send() always at time zero
        ///
        TEST_METHOD(ExactlyOneBufferPerInterval_RequestBeforeSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 10LL;
            // one byte every 100ms
            const long long TestBytes = 1;

            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            long long time_offset = 0LL;
            for (unsigned long counter = 0; counter < 200; ++counter) {
                time_offset += 100LL;
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(time_offset, test_task.time_offset_milliseconds);
            }
        }
        TEST_METHOD(MoreThanOneBufferPerInterval_RequestBeforeSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 100LL;
            // ten bytes every 100ms
            const long long TestBytes = 1;
            // should send 10 every 100ms
            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            long long time_offset = 0LL;
            for (unsigned long counter = 0; counter < 200; ++counter) {
                if (counter > 0) {
                    if (0 == counter % 10) {
                        time_offset += 100;
                    }
                }

                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(time_offset, test_task.time_offset_milliseconds);
            }
        }
        TEST_METHOD(LessThanOneBufferPerInterval_RequestBeforeSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 10LL;
            // 100 bytes every 10 seconds
            const long long TestBytes = 100;

            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            long long time_offset = 0LL;
            for (unsigned long counter = 0; counter < 200; ++counter) {
                time_offset += 10000LL; // 10 seconds
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(time_offset, test_task.time_offset_milliseconds);
            }
        }

        ///
        /// tests if calling send() exactly on schedule
        ///
        TEST_METHOD(ExactlyOneBufferPerInterval_RequestOnSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 10LL;
            // one byte every 100ms
            const long long TestBytes = 1;

            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            for (unsigned long counter = 0; counter < 200; ++counter) {
                s_QpcTime += 100LL;
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
            }
        }
        TEST_METHOD(MoreThanOneBufferPerInterval_RequestOnSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 100LL;
            // ten bytes every 100ms
            const long long TestBytes = 1;
            // should send 10 every 100ms
            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            for (unsigned long counter = 0; counter < 200; ++counter) {
                if (counter > 0) {
                    if (0 == counter % 10) {
                        s_QpcTime += 100LL;
                    }
                }
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
            }
        }
        TEST_METHOD(LessThanOneBufferPerInterval_RequestOnSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 10LL;
            // 100 bytes every 10 seconds
            const long long TestBytes = 100;

            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            for (unsigned long counter = 0; counter < 200; ++counter) {
                if (counter > 0) {
                    s_QpcTime += 10000LL; // 10 seconds
                }
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
            }
        }

        ///
        /// tests if calling send() one quantum *after* what was previously scheduled
        ///
        TEST_METHOD(ExactlyOneBufferPerInterval_RequestOneQuantumAfterSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 10LL;
            // one byte every 100ms
            const long long TestBytes = 1;

            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            for (unsigned long counter = 0; counter < 200; ++counter) {
                s_QpcTime += 200LL;
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
            }
        }
        TEST_METHOD(MoreThanOneBufferPerInterval_RequestOneQuantumAfterSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 100LL;
            // ten bytes every 100ms
            const long long TestBytes = 1;
            // should send 10 every 100ms
            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            for (unsigned long counter = 0; counter < 200; ++counter) {
                if (counter > 0) {
                    if (0 == counter % 10) {
                        s_QpcTime += 2000;
                    }
                }
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
            }
        }
        TEST_METHOD(LessThanOneBufferPerInterval_RequestOneQuantumAfterSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 10LL;
            // 100 bytes every 10 seconds
            const long long TestBytes = 100;

            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            for (unsigned long counter = 0; counter < 200; ++counter) {
                if (counter > 0) {
                    s_QpcTime += 11000; // 1 second after time expected
                }
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
            }
        }

        ///
        /// tests if calling send() one quantum *before* what was previously scheduled
        ///
        TEST_METHOD(ExactlyOneBufferPerInterval_RequestOneQuantumBeforeSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 10LL;
            // one byte every 100ms
            const long long TestBytes = 1;

            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(100LL, test_task.time_offset_milliseconds);

            for (unsigned long counter = 0; counter < 200; ++counter) {
                s_QpcTime += 100LL;
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(100LL, test_task.time_offset_milliseconds);
            }
        }
        TEST_METHOD(MoreThanOneBufferPerInterval_RequestOneQuantumBeforeSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 100LL;
            // ten bytes every 100ms
            const long long TestBytes = 1;
            // should send 10 every 100ms
            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            // fill the first 1 second (10 quantums)
            long long expected_time = 0LL;
            for (unsigned long counter = 0; counter < 100; ++counter) {
                if (0 == counter % 10) {
                    if (counter > 0) {
                        expected_time += 100LL;
                    }
                }
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(expected_time, test_task.time_offset_milliseconds);
            }

            for (unsigned long counter = 0; counter < 200; ++counter) {
                if (0 == counter % 10) {
                    if (counter > 0) {
                        s_QpcTime += 100LL;
                    }
                }
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(1000LL, test_task.time_offset_milliseconds);
            }
        }
        TEST_METHOD(LessThanOneBufferPerInterval_RequestOneQuantumBeforeSchedule)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;
            s_TcpBytesPerSecond = 10LL;
            // 100 bytes every 10 seconds
            const long long TestBytes = 100;

            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(10000LL, test_task.time_offset_milliseconds);

            for (unsigned long counter = 0; counter < 200; ++counter) {
                s_QpcTime += 10000LL;
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(10000LL, test_task.time_offset_milliseconds);
            }
        }



        TEST_METHOD(SendingOneEvenlySplitPerQuantum)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;

            s_TcpBytesPerSecond = 10LL;
            const long long TestBytes = 2;
            // 10 bytes per second, sending 2 bytes at a time, 
            // - should be evenly split 5 times per second (every 200ms)
            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            long long ExpectedTimeOffset = 199LL;

            s_QpcTime = 1LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(ExpectedTimeOffset, test_task.time_offset_milliseconds);

            for (unsigned long counter = 0; counter < 200; ++counter) {
                s_QpcTime += 200LL;
                // since time will be evenly offset by 200ms, 
                //   and we will aways be 1ms passed the 200ms slot,
                //   we should always require to wait 199ms
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(ExpectedTimeOffset, test_task.time_offset_milliseconds);
            }
        }

        TEST_METHOD(SendingManyEvenlySplitPerQuantum)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;

            s_TcpBytesPerSecond = 100LL;
            const long long TestBytes = 2;
            // 100 bytes per second, sending 2 bytes at a time, 
            // - should send 5 2-byte sends every quantum
            // - followed by a time offset to the next 100ms offset
            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            // expect the time offsets to look like:
            // send #1 : qpc_time 0 : time_offset 0 (sent 2 bytes)
            // send #2 : qpc_time 1 : time_offset 0 (sent 4 bytes)
            // send #3 : qpc_time 2 : time_offset 0 (sent 6 bytes)
            // send #4 : qpc_time 3 : time_offset 0 (sent 8 bytes)
            // send #5 : qpc_time 4 : time_offset 0 (sent 10 bytes) ** filled the quantum
            for (unsigned long counter = 0; counter < 5; ++counter) {
                Logger::WriteMessage(ctl::ctString::format_string(
                    L"QpcTime %lld : sending %lld bytes : expect offset %lld\n",
                    s_QpcTime, TestBytes, 0LL).c_str());
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
                // starting at zero, so increment afterwards in this loop
                ++s_QpcTime;
            }

            s_QpcTime = 4;
            // send #6 : qpc_time 5 : time_offset 95 (sent 12 bytes)
            // send #7 : qpc_time 101 : time_offset 0 (sent 14 byes) <1ms after the time it should be sent>
            // send #8 : qpc_time 102 : time_offset 0 (sent 16 bytes)
            // send #9 : qpc_time 103 : time_offset 0 (sent 18 bytes)
            // send #10 : qpc_time 104 : time_offset 0 (sent 20 bytes) ** filled the quantum

            // send #11 : qpc_time 105 : time_offset 95 (sent 22 bytes)
            // send #12 : qpc_time 201 : time_offset 0 (sent 24 bytes)
            // send #12 : qpc_time 202 : time_offset 0 (sent 26 bytes)
            // send #12 : qpc_time 203 : time_offset 0 (sent 28 bytes)
            // send #12 : qpc_time 204 : time_offset 0 (sent 30 bytes) ** filled the quantum
            for (unsigned long counter = 0; counter < 200; ++counter) {
                if (counter % 5 == 0) {
                    ++s_QpcTime;
                    Logger::WriteMessage(ctl::ctString::format_string(
                        L"QpcTime %lld : sending %lld bytes : expect offset %lld\n",
                        s_QpcTime, TestBytes, 95LL).c_str());
                    test_timer->update_time_offset(test_task, TestBytes);
                    Assert::AreEqual(95LL, test_task.time_offset_milliseconds);
                
                } else if (counter % 5 == 1) {
                    // the 2nd send should offset by 96ms to start 1m into the next quantum
                    s_QpcTime += 96;
                    Logger::WriteMessage(ctl::ctString::format_string(
                        L"QpcTime %lld : sending %lld bytes : expect offset %lld\n",
                        s_QpcTime, TestBytes, 0LL).c_str());
                    test_timer->update_time_offset(test_task, TestBytes);
                    Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
                
                } else {
                    ++s_QpcTime;
                    Logger::WriteMessage(ctl::ctString::format_string(
                        L"QpcTime %lld : sending %lld bytes : expect offset %lld\n",
                        s_QpcTime, TestBytes, 0LL).c_str());
                    test_timer->update_time_offset(test_task, TestBytes);
                    Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
                }
            }
        }

        TEST_METHOD(SendingSingleBufferPerQuantum)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;

            s_TcpBytesPerSecond = 10LL;
            const long long TestBytes = 10LL;
            // 10 bytes per second, sending 2 bytes at a time, 
            // - should be evenly split 5 times per second (every 200ms)
            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;
            s_QpcTime = 1LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            s_QpcTime += 1LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(998LL, test_task.time_offset_milliseconds);

            for (unsigned long counter = 0; counter < 10; ++counter) {
                s_QpcTime += 1000LL;
                test_timer->update_time_offset(test_task, TestBytes);
                Logger::WriteMessage(
                    ctl::ctString::format_string(
                    L"QPC %lld  -  offset %lld\n",
                    s_QpcTime, test_task.time_offset_milliseconds).c_str());
                Assert::AreEqual(998LL, test_task.time_offset_milliseconds);
            }
        }

        TEST_METHOD(SendingDoubleBufferPerQuantum)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;

            s_TcpBytesPerSecond = 10LL;
            const long long TestBytes = 5LL;
            // 10 bytes per second, sending 2 bytes at a time, 
            // - should be evenly split 5 times per second (every 200ms)
            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            // first can be sent immediately
            s_QpcTime = 1LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            // second can be sent at half second
            s_QpcTime = 2LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(498LL, test_task.time_offset_milliseconds);

            // third must be sent at the next second
            s_QpcTime = 3LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(997LL, test_task.time_offset_milliseconds);

            s_QpcTime = 1000;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(500LL, test_task.time_offset_milliseconds);
        }

        TEST_METHOD(SendingTripleBufferPerQuantum)
        {
            ctsConfig::Settings->TcpBytesPerSecondPeriod = 100LL;
            s_QpcTime = 0LL;

            s_TcpBytesPerSecond = 10LL;
            const long long TestBytes = 3LL;
            // 10 bytes per second, sending 2 bytes at a time, 
            // - should be evenly split 5 times per second (every 200ms)
            auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

            ctsIOTask test_task;
            test_task.ioAction = IOTaskAction::Send;

            // first can be sent immediately
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);

            // second can be sent at one-thrid second
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(300LL, test_task.time_offset_milliseconds);

            // third must be sent at two-thrids second
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(600LL, test_task.time_offset_milliseconds);

            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(900LL, test_task.time_offset_milliseconds);

            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(1200LL, test_task.time_offset_milliseconds);

            s_QpcTime = 1000;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(500LL, test_task.time_offset_milliseconds);
            // for the time period 1500

            s_QpcTime = 2000;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(0LL, test_task.time_offset_milliseconds);
            // resets the quantum to time 2000

            s_QpcTime = 2000;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                ctl::ctString::format_string(
                L"QPC %lld  -  offset %lld\n",
                s_QpcTime, test_task.time_offset_milliseconds).c_str());
            Assert::AreEqual(300LL, test_task.time_offset_milliseconds);
            // still in the time period 2000 - next should be in 2300
        }
    };
}
