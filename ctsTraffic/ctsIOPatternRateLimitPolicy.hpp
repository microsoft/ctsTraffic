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
#include "ctsSafeInt.hpp"

namespace ctsTraffic
{
    using ctsIOPatternRateLimitThrottle = struct ctsIOPatternRateLimitThrottle_t;
    using ctsIOPatternRateLimitDontThrottle = struct ctsIOPatternRateLimitDontThrottle_t;

    template <typename Protocol>
    struct ctsIOPatternRateLimitPolicy
    {
        void update_time_offset(ctsTask&, const ctsSignedLongLong& bufferSize) noexcept = delete;
    };


    ///
    /// ctsIOPatternRateLimitDontThrottle
    ///
    template<>
    struct ctsIOPatternRateLimitPolicy < ctsIOPatternRateLimitDontThrottle >
    {

        // ReSharper disable once CppMemberFunctionMayBeStatic
        void update_time_offset(ctsTask&, const ctsSignedLongLong&) const noexcept
        {
            // no-op
        }

    };

    ///
    /// ctsIOPatternRateLimitThrottle
    ///
    template<>
    struct ctsIOPatternRateLimitPolicy < ctsIOPatternRateLimitThrottle >
    {

    private:
        const ctsUnsignedLongLong m_bytesSendingPerQuantum;
        const ctsUnsignedLongLong m_quantumPeriodMs;
        ctsUnsignedLongLong m_bytesSentThisQuantum;
        ctsUnsignedLongLong m_quantumStartTimeMs;

    public:
        ctsIOPatternRateLimitPolicy() noexcept
            : m_bytesSendingPerQuantum(ctsConfig::GetTcpBytesPerSecond()* ctsConfig::g_configSettings->TcpBytesPerSecondPeriod / 1000LL),
            m_quantumPeriodMs(ctsConfig::g_configSettings->TcpBytesPerSecondPeriod),
            m_bytesSentThisQuantum(0ULL),
            m_quantumStartTimeMs(ctl::ctTimer::SnapQpcInMillis())
        {
#ifdef CTSTRAFFIC_UNIT_TESTS
            PRINT_DEBUG_INFO(
                L"\t\tctsIOPatternRateLimitPolicy: BytesSendingPerQuantum - %llu, QuantumPeriodMs - %llu\n",
                static_cast<unsigned long long>(this->m_bytesSendingPerQuantum),
                static_cast<unsigned long long>(this->m_quantumPeriodMs));
#endif
        }

        void update_time_offset(ctsTask& task, const ctsUnsignedLongLong& bufferSize) noexcept
        {
            if (task.m_ioAction != ctsTaskAction::Send)
            {
                return;
            }

            task.m_timeOffsetMilliseconds = 0LL;
            const auto currentTimeMs(ctl::ctTimer::SnapQpcInMillis());

            if (this->m_bytesSentThisQuantum < this->m_bytesSendingPerQuantum)
            {
                if (currentTimeMs < this->m_quantumStartTimeMs + this->m_quantumPeriodMs)
                {
                    if (currentTimeMs > this->m_quantumStartTimeMs)
                    {
                        // time is in the current quantum
                        this->m_bytesSentThisQuantum += bufferSize;
                    }
                    else
                    {
                        // time is still in a prior quantum
                        task.m_timeOffsetMilliseconds = this->NewQuantumStartTime() - currentTimeMs;
                        this->m_bytesSentThisQuantum += bufferSize;
                    }
                }
                else
                {
                    // time is already in a new quantum - start over
                    this->m_bytesSentThisQuantum = bufferSize;
                    this->m_quantumStartTimeMs += (currentTimeMs - this->m_quantumStartTimeMs);
                }
            }
            else
            {
                // have already fulfilled the prior quantum
                const auto new_quantum_start_time_ms = this->NewQuantumStartTime();

                if (currentTimeMs < new_quantum_start_time_ms)
                {
                    task.m_timeOffsetMilliseconds = new_quantum_start_time_ms - currentTimeMs;
                    this->m_bytesSentThisQuantum = bufferSize;
                    this->m_quantumStartTimeMs = new_quantum_start_time_ms;
                }
                else
                {
                    this->m_bytesSentThisQuantum = bufferSize;
                    this->m_quantumStartTimeMs += (currentTimeMs - this->m_quantumStartTimeMs);
                }
            }
#ifdef CTSTRAFFIC_UNIT_TESTS
            PRINT_DEBUG_INFO(
                L"\t\tctsIOPatternRateLimitPolicy\n"
                L"\tcurrent_time_ms: %lld\n"
                L"\tquantum_start_time_ms: %llu\n"
                L"\tbytes_sent_this_quantum: %llu\n",
                currentTimeMs,
                static_cast<long long>(this->m_quantumStartTimeMs),
                static_cast<long long>(this->m_bytesSentThisQuantum));
#endif
        }

    private:
        [[nodiscard]] long long NewQuantumStartTime() const
        {
            return this->m_quantumStartTimeMs + this->m_bytesSentThisQuantum / this->m_bytesSendingPerQuantum * this->m_quantumPeriodMs;
        }
    };
}

