/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#define CTSTRAFFIC_UNIT_TESTS

#include <sdkddkver.h>
#include "CppUnitTest.h"

#include <memory>

#include <ctString.hpp>
#include "ctsIOTask.hpp"
#include "ctsStatistics.hpp"
#include "ctsIOPatternRateLimitPolicy.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

int64_t g_QpcTime = 0LL;

uint64_t g_TransferSize = 0ULL;
int64_t g_TcpBytesPerSecond = 0LL;

///
/// Fakes
///
namespace ctl::ctTimer
{
int64_t ctsnap_qpc_as_msec() noexcept
{
    return g_QpcTime;
}
}

namespace ctsTraffic::ctsConfig
{
ctsConfigSettings* g_configSettings;

void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t) noexcept
{
}

void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t,
    const ctsTcpStatistics&) noexcept
{
}

void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t,
    const ctsUdpStatistics&) noexcept
{
}

void PrintDebug(_In_ _Printf_format_string_ PCWSTR _text, ...) noexcept
{
    va_list va_args;
    va_start(va_args, _text);
    std::wstring outputString;
    wil::details::str_vprintf_nothrow<std::wstring>(outputString, _text, va_args);
    Logger::WriteMessage(outputString.c_str());
    va_end(va_args);
}

void PrintException(const std::exception&) noexcept
{
}

void PrintErrorInfo(_In_ PCWSTR) noexcept
{
}

uint64_t GetTransferSize() noexcept
{
    return g_TransferSize;
}

int64_t GetTcpBytesPerSecond() noexcept
{
    return g_TcpBytesPerSecond;
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
TEST_CLASS(ctsIOPatternRateLimitPolicyUnitTest)
{
public:
    TEST_CLASS_INITIALIZE(Setup)
    {
        ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;

        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
    }

    TEST_CLASS_CLEANUP(Cleanup)
    {
        delete ctsConfig::g_configSettings;
    }

    TEST_METHOD(SendingDontThrottlePolicy)
    {
        g_TcpBytesPerSecond = 1LL;
        g_QpcTime = 1LL;

        const auto NoTimer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitDontThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        NoTimer->update_time_offset(test_task, 100);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        g_QpcTime = 2LL;
        NoTimer->update_time_offset(test_task, 100);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
    }

    TEST_METHOD(ReceivingDontThrottlePolicy)
    {
        g_TcpBytesPerSecond = 1LL;
        g_QpcTime = 1LL;

        const auto NoTimer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitDontThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Recv;

        NoTimer->update_time_offset(test_task, 100);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        g_QpcTime = 2LL;
        NoTimer->update_time_offset(test_task, 100);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
    }

    TEST_METHOD(ReceivingThrottlingPolicy)
    {
        g_TcpBytesPerSecond = 1LL;
        g_QpcTime = 1LL;

        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Recv;

        test_timer->update_time_offset(test_task, 100);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        g_QpcTime = 2LL;
        test_timer->update_time_offset(test_task, 100);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
    }


    ///
    /// tests if calling send() always at time zero
    ///
    TEST_METHOD(ExactlyOneBufferPerInterval_RequestBeforeSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 10LL;
        // one byte every 100ms
        const int64_t TestBytes = 1;

        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        auto time_offset = 0LL;
        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            time_offset += 100LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(time_offset, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(MoreThanOneBufferPerInterval_RequestBeforeSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 100LL;
        // ten bytes every 100ms
        const int64_t TestBytes = 1;
        // should send 10 every 100ms
        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        auto time_offset = 0LL;
        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            if (counter > 0)
            {
                if (0 == counter % 10)
                {
                    time_offset += 100;
                }
            }

            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(time_offset, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(LessThanOneBufferPerInterval_RequestBeforeSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 10LL;
        // 100 bytes every 10 seconds
        const int64_t TestBytes = 100;

        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        auto time_offset = 0LL;
        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            time_offset += 10000LL; // 10 seconds
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(time_offset, test_task.m_timeOffsetMilliseconds);
        }
    }

    ///
    /// tests if calling send() exactly on schedule
    ///
    TEST_METHOD(ExactlyOneBufferPerInterval_RequestOnSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 10LL;
        // one byte every 100ms
        const int64_t TestBytes = 1;

        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            g_QpcTime += 100LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(MoreThanOneBufferPerInterval_RequestOnSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 100LL;
        // ten bytes every 100ms
        const int64_t TestBytes = 1;
        // should send 10 every 100ms
        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            if (counter > 0)
            {
                if (0 == counter % 10)
                {
                    g_QpcTime += 100LL;
                }
            }
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(LessThanOneBufferPerInterval_RequestOnSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 10LL;
        // 100 bytes every 10 seconds
        const int64_t TestBytes = 100;

        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            if (counter > 0)
            {
                g_QpcTime += 10000LL; // 10 seconds
            }
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
        }
    }

    ///
    /// tests if calling send() one quantum *after* what was previously scheduled
    ///
    TEST_METHOD(ExactlyOneBufferPerInterval_RequestOneQuantumAfterSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 10LL;
        // one byte every 100ms
        const int64_t TestBytes = 1;

        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            g_QpcTime += 200LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(MoreThanOneBufferPerInterval_RequestOneQuantumAfterSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 100LL;
        // ten bytes every 100ms
        const int64_t TestBytes = 1;
        // should send 10 every 100ms
        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            if (counter > 0)
            {
                if (0 == counter % 10)
                {
                    g_QpcTime += 2000;
                }
            }
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(LessThanOneBufferPerInterval_RequestOneQuantumAfterSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 10LL;
        // 100 bytes every 10 seconds
        const int64_t TestBytes = 100;

        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            if (counter > 0)
            {
                g_QpcTime += 11000; // 1 second after time expected
            }
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
        }
    }

    ///
    /// tests if calling send() one quantum *before* what was previously scheduled
    ///
    TEST_METHOD(ExactlyOneBufferPerInterval_RequestOneQuantumBeforeSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 10LL;
        // one byte every 100ms
        const int64_t TestBytes = 1;

        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(100LL, test_task.m_timeOffsetMilliseconds);

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            g_QpcTime += 100LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(100LL, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(MoreThanOneBufferPerInterval_RequestOneQuantumBeforeSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 100LL;
        // ten bytes every 100ms
        const int64_t TestBytes = 1;
        // should send 10 every 100ms
        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        // fill the first 1 second (10 quantums)
        auto expected_time = 0LL;
        for (uint32_t counter = 0; counter < 100; ++counter)
        {
            if (0 == counter % 10)
            {
                if (counter > 0)
                {
                    expected_time += 100LL;
                }
            }
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(expected_time, test_task.m_timeOffsetMilliseconds);
        }

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            if (0 == counter % 10)
            {
                if (counter > 0)
                {
                    g_QpcTime += 100LL;
                }
            }
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(1000LL, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(LessThanOneBufferPerInterval_RequestOneQuantumBeforeSchedule)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;
        g_TcpBytesPerSecond = 10LL;
        // 100 bytes every 10 seconds
        const int64_t TestBytes = 100;

        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(10000LL, test_task.m_timeOffsetMilliseconds);

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            g_QpcTime += 10000LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(10000LL, test_task.m_timeOffsetMilliseconds);
        }
    }


    TEST_METHOD(SendingOneEvenlySplitPerQuantum)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;

        g_TcpBytesPerSecond = 10LL;
        const int64_t TestBytes = 2;
        // 10 bytes per second, sending 2 bytes at a time, 
        // - should be evenly split 5 times per second (every 200ms)
        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        const auto ExpectedTimeOffset = 199LL;

        g_QpcTime = 1LL;
        test_timer->update_time_offset(test_task, TestBytes);
        Assert::AreEqual(ExpectedTimeOffset, test_task.m_timeOffsetMilliseconds);

        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            g_QpcTime += 200LL;
            // since time will be evenly offset by 200ms, 
            //   and we will aways be 1ms passed the 200ms slot,
            //   we should always require to wait 199ms
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(ExpectedTimeOffset, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(SendingManyEvenlySplitPerQuantum)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;

        g_TcpBytesPerSecond = 100LL;
        const int64_t TestBytes = 2;
        // 100 bytes per second, sending 2 bytes at a time, 
        // - should send 5 2-byte sends every quantum
        // - followed by a time offset to the next 100ms offset
        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        // expect the time offsets to look like:
        // send #1 : qpc_time 0 : time_offset 0 (sent 2 bytes)
        // send #2 : qpc_time 1 : time_offset 0 (sent 4 bytes)
        // send #3 : qpc_time 2 : time_offset 0 (sent 6 bytes)
        // send #4 : qpc_time 3 : time_offset 0 (sent 8 bytes)
        // send #5 : qpc_time 4 : time_offset 0 (sent 10 bytes) ** filled the quantum
        for (uint32_t counter = 0; counter < 5; ++counter)
        {
            Logger::WriteMessage(wil::str_printf<std::wstring>(
                L"QpcTime %lld : sending %lld bytes : expect offset %lld\n",
                g_QpcTime, TestBytes, 0LL).c_str());
            test_timer->update_time_offset(test_task, TestBytes);
            Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
            // starting at zero, so increment afterwards in this loop
            ++g_QpcTime;
        }

        g_QpcTime = 4;
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
        for (uint32_t counter = 0; counter < 200; ++counter)
        {
            if (counter % 5 == 0)
            {
                ++g_QpcTime;
                Logger::WriteMessage(wil::str_printf<std::wstring>(
                    L"QpcTime %lld : sending %lld bytes : expect offset %lld\n",
                    g_QpcTime, TestBytes, 95LL).c_str());
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(95LL, test_task.m_timeOffsetMilliseconds);
            }
            else if (counter % 5 == 1)
            {
                // the 2nd send should offset by 96ms to start 1m into the next quantum
                g_QpcTime += 96;
                Logger::WriteMessage(wil::str_printf<std::wstring>(
                    L"QpcTime %lld : sending %lld bytes : expect offset %lld\n",
                    g_QpcTime, TestBytes, 0LL).c_str());
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
            }
            else
            {
                ++g_QpcTime;
                Logger::WriteMessage(wil::str_printf<std::wstring>(
                    L"QpcTime %lld : sending %lld bytes : expect offset %lld\n",
                    g_QpcTime, TestBytes, 0LL).c_str());
                test_timer->update_time_offset(test_task, TestBytes);
                Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
            }
        }
    }

    TEST_METHOD(SendingSingleBufferPerQuantum)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;

        g_TcpBytesPerSecond = 10LL;
        const auto TestBytes = 10LL;
        // 10 bytes per second, sending 2 bytes at a time, 
        // - should be evenly split 5 times per second (every 200ms)
        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;
        g_QpcTime = 1LL;
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        g_QpcTime += 1LL;
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(998LL, test_task.m_timeOffsetMilliseconds);

        for (uint32_t counter = 0; counter < 10; ++counter)
        {
            g_QpcTime += 1000LL;
            test_timer->update_time_offset(test_task, TestBytes);
            Logger::WriteMessage(
                wil::str_printf<std::wstring>(
                    L"QPC %lld  -  offset %lld\n",
                    g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
            Assert::AreEqual(998LL, test_task.m_timeOffsetMilliseconds);
        }
    }

    TEST_METHOD(SendingDoubleBufferPerQuantum)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;

        g_TcpBytesPerSecond = 10LL;
        const auto TestBytes = 5LL;
        // 10 bytes per second, sending 2 bytes at a time, 
        // - should be evenly split 5 times per second (every 200ms)
        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        // first can be sent immediately
        g_QpcTime = 1LL;
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        // second can be sent at half second
        g_QpcTime = 2LL;
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(498LL, test_task.m_timeOffsetMilliseconds);

        // third must be sent at the next second
        g_QpcTime = 3LL;
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(997LL, test_task.m_timeOffsetMilliseconds);

        g_QpcTime = 1000;
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(500LL, test_task.m_timeOffsetMilliseconds);
    }

    TEST_METHOD(SendingTripleBufferPerQuantum)
    {
        ctsConfig::g_configSettings->TcpBytesPerSecondPeriod = 100LL;
        g_QpcTime = 0LL;

        g_TcpBytesPerSecond = 10LL;
        const auto TestBytes = 3LL;
        // 10 bytes per second, sending 2 bytes at a time, 
        // - should be evenly split 5 times per second (every 200ms)
        const auto test_timer = std::make_unique<ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>>();

        ctsTask test_task;
        test_task.m_ioAction = ctsTaskAction::Send;

        // first can be sent immediately
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);

        // second can be sent at one-thrid second
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(300LL, test_task.m_timeOffsetMilliseconds);

        // third must be sent at two-thrids second
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(600LL, test_task.m_timeOffsetMilliseconds);

        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(900LL, test_task.m_timeOffsetMilliseconds);

        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(1200LL, test_task.m_timeOffsetMilliseconds);

        g_QpcTime = 1000;
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(500LL, test_task.m_timeOffsetMilliseconds);
        // for the time period 1500

        g_QpcTime = 2000;
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(0LL, test_task.m_timeOffsetMilliseconds);
        // resets the quantum to time 2000

        g_QpcTime = 2000;
        test_timer->update_time_offset(test_task, TestBytes);
        Logger::WriteMessage(
            wil::str_printf<std::wstring>(
                L"QPC %lld  -  offset %lld\n",
                g_QpcTime, test_task.m_timeOffsetMilliseconds).c_str());
        Assert::AreEqual(300LL, test_task.m_timeOffsetMilliseconds);
        // still in the time period 2000 - next should be in 2300
    }
};
}
