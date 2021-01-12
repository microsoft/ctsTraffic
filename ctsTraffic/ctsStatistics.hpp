/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once
// cpp headers
#include <cstring>
// os headers
#include <Windows.h>
#include <rpc.h>
// ctl headers
#include <ctTimer.hpp>
#include <ctMemoryGuard.hpp>
#include <wil/resource.h>

namespace ctsTraffic
{
    namespace ctsStatistics
    {
        constexpr unsigned long c_connectionIdLength = 36 + 1; // UUID strings are 36 chars

        template <typename T>
        void GenerateConnectionId(_In_ T& statisticsObject)
        {
            UUID connectionId;
            RPC_STATUS status = UuidCreate(&connectionId);
            if (status != RPC_S_OK)
            {
                THROW_WIN32_MSG(status, "UuidCreate (ctsStatistics)");
            }

            RPC_CSTR connectionIdString = nullptr;
            status = UuidToStringA(&connectionId, &connectionIdString);
            if (status != RPC_S_OK)
            {
                THROW_WIN32_MSG(status, "UuidToStringA (ctsStatistics)");
            }
            FAIL_FAST_IF_MSG(
                // ReSharper disable once CppRedundantParentheses
                strlen(reinterpret_cast<LPSTR>(connectionIdString)) != (c_connectionIdLength - 1),
                "UuidToString returned a string not 36 characters long (%Iu)",
                strlen(reinterpret_cast<LPSTR>(connectionIdString)));

            const auto copyError = ::memcpy_s(statisticsObject.m_connectionIdentifier, c_connectionIdLength, connectionIdString, c_connectionIdLength);
            FAIL_FAST_IF_MSG(
                copyError != 0,
                "memcpy_s failed trying to copy a UUID string (%d)", copyError);

            RpcStringFreeA(&connectionIdString);
            statisticsObject.m_connectionIdentifier[c_connectionIdLength - 1] = '\0';
        }

        template <typename T>
        void Start(_In_ T& statisticsObject) noexcept
        {
            // only calculate the QPC the first time
            // - willing to take the cost of 2 interlocked operations the first time this is initialized
            //   versus taking a QPC hit on every IO request
            if (0LL == statisticsObject.start_time.get())
            {
                statisticsObject.m_startTime.set_conditionally(ctl::ctTimer::SnapQpcInMillis(), 0LL);
            }
        }

        template <typename T>
        void End(_In_ T& statisticsObject) noexcept
        {
            statisticsObject.m_endTime.set_conditionally(ctl::ctTimer::SnapQpcInMillis(), 0LL);
        }
    }

    struct ctsStatsTracking
    {
    private:
        long long m_currentValue = 0ll;
        long long m_previousValue = 0ll;

    public:
        ctsStatsTracking() noexcept = default;
        explicit ctsStatsTracking(long long initial_value) noexcept :
            m_currentValue(initial_value),
            m_previousValue(initial_value)
        {
        }
        ~ctsStatsTracking() noexcept = default;

        ctsStatsTracking(const ctsStatsTracking& in) noexcept :
            m_currentValue(ctl::ctMemoryGuardRead(&in.m_currentValue)),
            m_previousValue(ctl::ctMemoryGuardRead(&in.m_previousValue))
        {
        }
        ctsStatsTracking(ctsStatsTracking&& in) noexcept :
            m_currentValue(ctl::ctMemoryGuardRead(&in.m_currentValue)),
            m_previousValue(ctl::ctMemoryGuardRead(&in.m_previousValue))
        {
        }
        // not allowing assignment operator - must be explicit
        ctsStatsTracking& operator=(const ctsStatsTracking&) = delete;
        ctsStatsTracking& operator=(ctsStatsTracking&&) = delete;

        [[nodiscard]] long long GetValue() const noexcept
        {
            return ctl::ctMemoryGuardRead(&m_currentValue);
        }
        //
        // Safely writes to the current value, returning the *prior* value
        //
        long long SetValue(long long new_value) noexcept
        {
            return ctl::ctMemoryGuardWrite(&m_currentValue, new_value);
        }
        long long SetConditionally(long long new_value, long long if_equals) noexcept
        {
            return ctl::ctMemoryGuardWriteConditionally(&m_currentValue, new_value, if_equals);
        }
        //
        // Adds 1 to the current value, returning the new value
        //
        long long Increment() noexcept
        {
            return ctl::ctMemoryGuardIncrement(&m_currentValue);
        }
        //
        // Subtracts 1 from the current value, returning the new value
        //
        long long Decrement() noexcept
        {
            return ctl::ctMemoryGuardDecrement(&m_currentValue);
        }
        //
        // Adds the [in] value to the current value, returning the original value
        //
        long long Add(long long value) noexcept
        {
            return ctl::ctMemoryGuardAdd(&m_currentValue, value);
        }
        //
        // Subtracts the [in] value from the current value, returning the original value
        //
        long long Subtract(long long value) noexcept
        {
            return ctl::ctMemoryGuardSubtract(&m_currentValue, value);
        }
        //
        // Get / Sets a new value to the 'previous' value, returning the prior 'previous' value
        //
        [[nodiscard]] long long GetPriorValue() noexcept
        {
            return ctl::ctMemoryGuardRead(&m_previousValue);
        }
        long long SetPriorValue(long long new_value) noexcept
        {
            return ctl::ctMemoryGuardWrite(&m_previousValue, new_value);
        }
        //
        // Updates the previous value with the current value
        // - returning the difference (current_value - previous_value)
        //
        [[nodiscard]] long long SnapValueDifference() noexcept
        {
            const auto captureCurrentValue = ctl::ctMemoryGuardRead(&m_currentValue);
            const auto capturePriorValue = ctl::ctMemoryGuardWrite(&m_previousValue, captureCurrentValue);
            return captureCurrentValue - capturePriorValue;
        }
        //
        // Returns the difference (current_value - previous_value)
        // - without modifying either value
        //
        [[nodiscard]] long long ReadValueDifference() const noexcept
        {
            const auto captureCurrentValue = ctl::ctMemoryGuardRead(&m_currentValue);
            const auto capturePriorValue = ctl::ctMemoryGuardRead(&m_previousValue);
            return captureCurrentValue - capturePriorValue;
        }
    };


    struct ctsConnectionStatistics
    {
        ctsStatsTracking m_startTime;
        ctsStatsTracking m_endTime;
        ctsStatsTracking m_activeConnectionCount;
        ctsStatsTracking m_successfulCompletionCount;
        ctsStatsTracking m_connectionErrorCount;
        ctsStatsTracking m_protocolErrorCount;

        explicit ctsConnectionStatistics(long long start_time = 0LL) noexcept :
            m_startTime(start_time)
        {
        }
        ~ctsConnectionStatistics() noexcept = default;
        ctsConnectionStatistics(const ctsConnectionStatistics&) = default;
        ctsConnectionStatistics(ctsConnectionStatistics&&) = default;
        // not implementing the assignment operator
        // only implemeting the copy c'tor (due to maintaining memory barriers)
        ctsConnectionStatistics& operator=(const ctsConnectionStatistics&) = delete;
        ctsConnectionStatistics& operator=(ctsConnectionStatistics&&) = delete;

        //
        // snap_view() will return a statistics object capturing the current values
        // - resetting only the start_time value if the _In_ bool is true
        // - not resetting the other values even when _clear_settings == true since
        //   connection values in status messages always display the aggregate values
        //   (not displaying only changes in connection settings over each time slice)
        //
        ctsConnectionStatistics SnapView(bool clear_settings) noexcept
        {
            const long long currentTime = ctl::ctTimer::SnapQpcInMillis();
            const long long priorTimeRead = clear_settings ?
                m_startTime.SetPriorValue(currentTime) :
                m_startTime.GetPriorValue();

            ctsConnectionStatistics returnStats(priorTimeRead);
            returnStats.m_endTime.SetValue(currentTime);

            returnStats.m_activeConnectionCount.SetValue(m_activeConnectionCount.GetValue());
            returnStats.m_successfulCompletionCount.SetValue(m_successfulCompletionCount.GetValue());
            returnStats.m_connectionErrorCount.SetValue(m_connectionErrorCount.GetValue());
            returnStats.m_protocolErrorCount.SetValue(m_protocolErrorCount.GetValue());

            return returnStats;
        }
    };

    struct ctsUdpStatistics
    {
        ctsStatsTracking m_startTime;
        ctsStatsTracking m_endTime;
        ctsStatsTracking m_bitsReceived;
        ctsStatsTracking m_successfulFrames;
        ctsStatsTracking m_droppedFrames;
        ctsStatsTracking m_duplicateFrames;
        ctsStatsTracking m_errorFrames;
        // unique connection identifier
        char m_connectionIdentifier[ctsStatistics::c_connectionIdLength]{};

        explicit ctsUdpStatistics(long long start_time = 0LL) noexcept :
            m_startTime(start_time)
        {
            m_connectionIdentifier[0] = '\0';
        }
        ~ctsUdpStatistics() noexcept = default;

        ctsUdpStatistics(const ctsUdpStatistics&) noexcept = default;
        ctsUdpStatistics(ctsUdpStatistics&&) noexcept = default;

        ctsUdpStatistics& operator=(const ctsUdpStatistics&) = delete;
        ctsUdpStatistics& operator=(ctsUdpStatistics&&) = delete;

        [[nodiscard]] long long GetBytesReceived() const noexcept
        {
            return m_bitsReceived.GetValue() / 8;
        }

        //
        // snap-view will set the returned start time == last read time to capture the delta
        //
        ctsUdpStatistics SnapView(bool clear_settings) noexcept
        {
            const long long currentTime = ctl::ctTimer::SnapQpcInMillis();
            const long long priorTimeRead = clear_settings ?
                m_startTime.SetPriorValue(currentTime) :
                m_startTime.GetPriorValue();

            ctsUdpStatistics returnStats(priorTimeRead);
            returnStats.m_endTime.SetValue(currentTime);

            if (clear_settings)
            {
                returnStats.m_bitsReceived.SetValue(m_bitsReceived.SnapValueDifference());
                returnStats.m_successfulFrames.SetValue(m_successfulFrames.SnapValueDifference());
                returnStats.m_droppedFrames.SetValue(m_droppedFrames.SnapValueDifference());
                returnStats.m_duplicateFrames.SetValue(m_duplicateFrames.SnapValueDifference());
                returnStats.m_errorFrames.SetValue(m_errorFrames.SnapValueDifference());

            }
            else
            {
                returnStats.m_bitsReceived.SetValue(m_bitsReceived.ReadValueDifference());
                returnStats.m_successfulFrames.SetValue(m_successfulFrames.ReadValueDifference());
                returnStats.m_droppedFrames.SetValue(m_droppedFrames.ReadValueDifference());
                returnStats.m_duplicateFrames.SetValue(m_duplicateFrames.ReadValueDifference());
                returnStats.m_errorFrames.SetValue(m_errorFrames.ReadValueDifference());
            }

            return returnStats;
        }
    };

    struct ctsTcpStatistics
    {
        ctsStatsTracking m_startTime;
        ctsStatsTracking m_endTime;
        ctsStatsTracking m_bytesSent;
        ctsStatsTracking m_bytesRecv;
        // unique connection identifier
        char m_connectionIdentifier[ctsStatistics::c_connectionIdLength]{};

        explicit ctsTcpStatistics(long long current_time = 0LL) noexcept :
            m_startTime(current_time)
        {
            static const char* nullGuidString = "00000000-0000-0000-0000-000000000000";
            strcpy_s(
                m_connectionIdentifier,
                nullGuidString);
        }
        ~ctsTcpStatistics() noexcept = default;

        ctsTcpStatistics(const ctsTcpStatistics&) noexcept = default;
        ctsTcpStatistics(ctsTcpStatistics&&) noexcept = default;

        ctsTcpStatistics operator=(const ctsTcpStatistics&) = delete;
        ctsTcpStatistics operator=(ctsTcpStatistics&&) = delete;

        [[nodiscard]] long long GetBytesReceived() const noexcept
        {
            return m_bytesRecv.GetValue() + m_bytesSent.GetValue();
        }

        //
        // snap-view will set the returned start time == last read time to capture the delta
        // - and end time == current time
        //
        ctsTcpStatistics SnapView(bool clear_settings) noexcept
        {
            const long long currentTime = ctl::ctTimer::SnapQpcInMillis();
            const long long priorTimeRead = clear_settings ?
                m_startTime.SetPriorValue(currentTime) :
                m_startTime.GetPriorValue();

            ctsTcpStatistics returnStats(priorTimeRead);
            returnStats.m_endTime.SetValue(currentTime);

            if (clear_settings)
            {
                returnStats.m_bytesSent.SetValue(m_bytesSent.SnapValueDifference());
                returnStats.m_bytesRecv.SetValue(m_bytesRecv.SnapValueDifference());

            }
            else
            {
                returnStats.m_bytesSent.SetValue(m_bytesSent.ReadValueDifference());
                returnStats.m_bytesRecv.SetValue(m_bytesRecv.ReadValueDifference());
            }

            return returnStats;
        }
    };
}
