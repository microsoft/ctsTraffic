/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// ctl headers
#include <ctTimer.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsIOTask.hpp"

namespace ctsTraffic
{
using ctsIOPatternRateLimitThrottle = struct ctsIOPatternRateLimitThrottle_t;
using ctsIOPatternRateLimitDontThrottle = struct ctsIOPatternRateLimitDontThrottle_t;

template <typename Protocol>
struct ctsIOPatternRateLimitPolicy
{
    void update_time_offset(ctsTask&, const int64_t& bufferSize) noexcept = delete;
};


///
/// ctsIOPatternRateLimitDontThrottle
///
template <>
struct ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitDontThrottle>
{
    // ReSharper disable once CppMemberFunctionMayBeStatic
    void update_time_offset(ctsTask&, const int64_t&) const noexcept
    {
        // no-op
    }
};

///
/// ctsIOPatternRateLimitThrottle
///
template <>
struct ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>
{
private:
    const uint64_t m_bytesSendingPerQuantum;
    const int64_t m_quantumPeriodMs{ctsConfig::g_configSettings->TcpBytesPerSecondPeriod};
    uint64_t m_bytesSentThisQuantum{0};
    int64_t m_quantumStartTimeMs{ctl::ctTimer::SnapQpcInMillis()};

public:
    ctsIOPatternRateLimitPolicy() noexcept :
        m_bytesSendingPerQuantum(ctsConfig::GetTcpBytesPerSecond() * ctsConfig::g_configSettings->TcpBytesPerSecondPeriod / 1000LL)
    {
#ifdef CTSTRAFFIC_UNIT_TESTS
        PRINT_DEBUG_INFO(
            L"\t\tctsIOPatternRateLimitPolicy: BytesSendingPerQuantum - %llu, QuantumPeriodMs - %lld\n",
            m_bytesSendingPerQuantum, m_quantumPeriodMs);
#endif
    }

    void update_time_offset(ctsTask& task, uint64_t bufferSize) noexcept
    {
        if (task.m_ioAction != ctsTaskAction::Send)
        {
            return;
        }

        task.m_timeOffsetMilliseconds = 0LL;
        const auto currentTimeMs(ctl::ctTimer::SnapQpcInMillis());

        if (m_bytesSentThisQuantum < m_bytesSendingPerQuantum)
        {
            if (currentTimeMs < m_quantumStartTimeMs + m_quantumPeriodMs)
            {
                if (currentTimeMs > m_quantumStartTimeMs)
                {
                    // time is in the current quantum
                    m_bytesSentThisQuantum += bufferSize;
                }
                else
                {
                    // time is still in a prior quantum
                    task.m_timeOffsetMilliseconds = this->NewQuantumStartTime() - currentTimeMs;
                    m_bytesSentThisQuantum += bufferSize;
                }
            }
            else
            {
                // time is already in a new quantum - start over
                m_bytesSentThisQuantum = bufferSize;
                m_quantumStartTimeMs += (currentTimeMs - m_quantumStartTimeMs);
            }
        }
        else
        {
            // have already fulfilled the prior quantum
            const auto new_quantum_start_time_ms = this->NewQuantumStartTime();

            if (currentTimeMs < new_quantum_start_time_ms)
            {
                task.m_timeOffsetMilliseconds = new_quantum_start_time_ms - currentTimeMs;
                m_bytesSentThisQuantum = bufferSize;
                m_quantumStartTimeMs = new_quantum_start_time_ms;
            }
            else
            {
                m_bytesSentThisQuantum = bufferSize;
                m_quantumStartTimeMs += (currentTimeMs - m_quantumStartTimeMs);
            }
        }
#ifdef CTSTRAFFIC_UNIT_TESTS
        PRINT_DEBUG_INFO(
            L"\t\tctsIOPatternRateLimitPolicy\n"
            L"\tcurrent_time_ms: %lld\n"
            L"\tquantum_start_time_ms: %lld\n"
            L"\tbytes_sent_this_quantum: %llu\n",
            currentTimeMs,
            m_quantumStartTimeMs,
            m_bytesSentThisQuantum);
#endif
    }

private:
    [[nodiscard]] int64_t NewQuantumStartTime() const
    {
        return m_quantumStartTimeMs + static_cast<int64_t>(m_bytesSentThisQuantum / m_bytesSendingPerQuantum * m_quantumPeriodMs);
    }
};
}
