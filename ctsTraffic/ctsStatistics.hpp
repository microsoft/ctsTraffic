/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include "rpc.h"
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctTimer.hpp>
#include <ctLocks.hpp>

namespace ctsTraffic {
    namespace ctsStatistics {
        static const unsigned long ConnectionIdLength = 36 + 1; // UUID strings are 36 chars

        template <typename T>
        void GenerateConnectionId(_In_ T& _statistics_object);

        template <typename T>
        void Start(_In_ T& _statistics_object) NOEXCEPT;

        template <typename T>
        void End(_In_ T& _statistics_object) NOEXCEPT;
    }

    struct ctStatsTracking
    {
    private:
        // not allowing assignment operator - must be explicit
        ctStatsTracking& operator=(const ctStatsTracking& _in) NOEXCEPT;
        long long current_value;
        long long previous_value;

    public:
        ctStatsTracking() NOEXCEPT :
            current_value(0),
            previous_value(0)
        {
        }
        explicit ctStatsTracking(long long _initial_value) NOEXCEPT :
            current_value(_initial_value),
            previous_value(_initial_value)
        {
        }
        explicit ctStatsTracking(const ctStatsTracking& _in) NOEXCEPT :
            current_value(ctl::ctMemoryGuardRead(&_in.current_value)),
            previous_value(ctl::ctMemoryGuardRead(&_in.previous_value))
        {
        }

        long long get() const NOEXCEPT
        {
            return ctl::ctMemoryGuardRead(&current_value);
        }
        //
        // Safely writes to the current value, returning the *prior* value
        //
        long long set(long long _new_value) NOEXCEPT
        {
            return ctl::ctMemoryGuardWrite(&current_value, _new_value);
        }
        long long set_conditionally(long long _new_value, long long _if_equals) NOEXCEPT
        {
            return ctl::ctMemoryGuardWriteConditionally(&current_value, _new_value, _if_equals);
        }
        //
        // Adds 1 to the current value, returning the new value
        //
        long long increment() NOEXCEPT
        {
            return ctl::ctMemoryGuardIncrement(&current_value);
        }
        //
        // Subtracts 1 from the current value, returning the new value
        //
        long long decrement() NOEXCEPT
        {
            return ctl::ctMemoryGuardDecrement(&current_value);
        }
        //
        // Adds the [in] value to the current value, returning the original value
        //
        long long add(long long _value) NOEXCEPT
        {
            return ctl::ctMemoryGuardAdd(&current_value, _value);
        }
        //
        // Subtracts the [in] value from the current value, returning the original value
        //
        long long subtract(long long _value) NOEXCEPT
        {
            return ctl::ctMemoryGuardAdd(&current_value, _value);
        }
        //
        // Get / Sets a new value to the 'previous' value, returning the prior 'previous' value
        //
        long long get_prior_value() NOEXCEPT
        {
            return ctl::ctMemoryGuardRead(&previous_value);
        }
        long long set_prior_value(long long _new_value) NOEXCEPT
        {
            return ctl::ctMemoryGuardWrite(&previous_value, _new_value);
        }
        //
        // Updates the previous value with the current value
        // - returning the difference (current_value - previous_value)
        //
        long long snap_value_difference() NOEXCEPT
        {
            long long capture_current_value = ctl::ctMemoryGuardRead(&current_value);
            long long capture_prior_value = ctl::ctMemoryGuardWrite(&previous_value, capture_current_value);
            return capture_current_value - capture_prior_value;
        }
        //
        // Returns the difference (current_value - previous_value)
        // - without modifying either value
        //
        long long read_value_difference() const NOEXCEPT
        {
            long long capture_current_value = ctl::ctMemoryGuardRead(&current_value);
            long long capture_prior_value = ctl::ctMemoryGuardRead(&previous_value);
            return capture_current_value - capture_prior_value;
        }
    };


    struct ctsConnectionHistoritcStatistics
    {
        ctStatsTracking total_time;
        ctStatsTracking active_connections;
        ctStatsTracking successful_connections;
        ctStatsTracking connection_errors;
        ctStatsTracking protocol_errors;
    };
    struct ctsConnectionStatistics
    {
    private:
        // not implementing the assignment operator
        // only implemeting the copy c'tor (due to maintaining memory barriers)
        ctsConnectionStatistics& operator=(const ctsConnectionStatistics& _in) = delete;

    public:
        ctStatsTracking start_time;
        ctStatsTracking end_time;
        ctStatsTracking active_connection_count;
        ctStatsTracking successful_completion_count;
        ctStatsTracking connection_error_count;
        ctStatsTracking protocol_error_count;

        ctsConnectionStatistics(long long _start_time = 0LL) NOEXCEPT :
            start_time(_start_time),
            end_time(0LL),
            active_connection_count(0LL),
            successful_completion_count(0LL),
            connection_error_count(0LL),
            protocol_error_count(0LL)
        {
        }
        //
        // implementing the copy c'tor with memory barriers in place
        //
        ctsConnectionStatistics(const ctsConnectionStatistics& _in) NOEXCEPT :
            start_time(_in.start_time),
            end_time(_in.end_time),
            active_connection_count(_in.active_connection_count),
            successful_completion_count(_in.successful_completion_count),
            connection_error_count(_in.connection_error_count),
            protocol_error_count(_in.protocol_error_count)
        {
        }
        //
        // snap_view() will return a statistics object capturing the current values
        // - resetting only the start_time value if the _In_ bool is true
        // - not resetting the other values even when _clear_settings == true since
        //   connection values in status messages always display the aggregate values
        //   (not displaying only changes in connection settings over each time slice)
        //
        ctsConnectionStatistics snap_view(bool _clear_settings) NOEXCEPT
        {
            long long current_time = ctl::ctTimer::snap_qpc_as_msec();
            long long prior_time_read = (_clear_settings) ?
                this->start_time.set_prior_value(current_time) :
                this->start_time.get_prior_value();

            ctsConnectionStatistics return_stats(prior_time_read);
            return_stats.end_time.set(current_time);

            return_stats.active_connection_count.set(this->active_connection_count.get());
            return_stats.successful_completion_count.set(this->successful_completion_count.get());
            return_stats.connection_error_count.set(this->connection_error_count.get());
            return_stats.protocol_error_count.set(this->protocol_error_count.get());

            return return_stats;
        }
    };

    struct ctsUdpHistoricStatistics
    {
        ctStatsTracking total_time;
        ctStatsTracking bits_received;
        ctStatsTracking successful_frames;
        ctStatsTracking retry_attempts;
        ctStatsTracking dropped_frames;
        ctStatsTracking duplicate_frames;
        ctStatsTracking error_frames;
    };

    struct ctsUdpStatistics
    {
    private:
        ctsUdpStatistics& operator=(const ctsUdpStatistics& _in) = delete;

    public:
        ctStatsTracking start_time;
        ctStatsTracking end_time;
        ctStatsTracking bits_received;
        ctStatsTracking successful_frames;
        ctStatsTracking retry_attempts;
        ctStatsTracking dropped_frames;
        ctStatsTracking duplicate_frames;
        ctStatsTracking error_frames;
        // unique connection identifier
        char connection_identifier[ctsStatistics::ConnectionIdLength];

        ctsUdpStatistics(long long _start_time = 0LL) NOEXCEPT :
            start_time(_start_time),
            end_time(0LL),
            bits_received(0LL),
            successful_frames(0LL),
            retry_attempts(0LL),
            dropped_frames(0LL),
            duplicate_frames(0LL),
            error_frames(0LL)
        {
            connection_identifier[0] = '\0';
        }
        //
        // implementing the copy c'tor with memory barriers in place
        //
        ctsUdpStatistics(const ctsUdpStatistics& _in) NOEXCEPT :
            start_time(_in.start_time),
            end_time(_in.end_time),
            bits_received(_in.bits_received),
            successful_frames(_in.successful_frames),
            retry_attempts(_in.retry_attempts),
            dropped_frames(_in.dropped_frames),
            duplicate_frames(_in.duplicate_frames),
            error_frames(_in.error_frames)
        {
            // not needing to guard this string: it's created exactly once
            ::memcpy_s(connection_identifier, ctsStatistics::ConnectionIdLength, _in.connection_identifier, ctsStatistics::ConnectionIdLength);
            connection_identifier[ctsStatistics::ConnectionIdLength - 1] = '\0';
        }

        long long current_bytes() NOEXCEPT
        {
            return this->bits_received.get() / 8;
        }

        //
        // snap-view will set the returned start time == last read time to capture the delta
        //
        ctsUdpStatistics snap_view(bool _clear_settings) NOEXCEPT
        {
            long long current_time = ctl::ctTimer::snap_qpc_as_msec();
            long long prior_time_read = (_clear_settings) ?
                this->start_time.set_prior_value(current_time) :
                this->start_time.get_prior_value();

            ctsUdpStatistics return_stats(prior_time_read);
            return_stats.end_time.set(current_time);

            if (_clear_settings) {
                return_stats.bits_received.set(this->bits_received.snap_value_difference());
                return_stats.successful_frames.set(this->successful_frames.snap_value_difference());
                return_stats.retry_attempts.set(this->retry_attempts.snap_value_difference());
                return_stats.dropped_frames.set(this->dropped_frames.snap_value_difference());
                return_stats.duplicate_frames.set(this->duplicate_frames.snap_value_difference());
                return_stats.error_frames.set(this->duplicate_frames.snap_value_difference());

            } else {
                return_stats.bits_received.set(this->bits_received.read_value_difference());
                return_stats.successful_frames.set(this->successful_frames.read_value_difference());
                return_stats.retry_attempts.set(this->retry_attempts.read_value_difference());
                return_stats.dropped_frames.set(this->dropped_frames.read_value_difference());
                return_stats.duplicate_frames.set(this->duplicate_frames.read_value_difference());
                return_stats.error_frames.set(this->duplicate_frames.read_value_difference());
            }

            return return_stats;
        }
    };

    struct ctsTcpHistoricStatistics
    {
        ctStatsTracking total_time;
        ctStatsTracking bytes_sent;
        ctStatsTracking bytes_recv;
    };

    struct ctsTcpStatistics
    {
    private:
        ctsTcpStatistics operator=(const ctsTcpStatistics& _in) NOEXCEPT = delete;

    public:
        ctStatsTracking start_time;
        ctStatsTracking end_time;
        ctStatsTracking bytes_sent;
        ctStatsTracking bytes_recv;
        // unique connection identifier
        char connection_identifier[ctsStatistics::ConnectionIdLength];

        ctsTcpStatistics(long long _current_time = 0LL) NOEXCEPT :
            start_time(_current_time),
            end_time(0LL),
            bytes_sent(0LL),
            bytes_recv(0LL)
        {
            connection_identifier[0] = '\0';
        }
        //
        // implementing the copy c'tor with memory barriers in place
        //
        ctsTcpStatistics(const ctsTcpStatistics& _in) NOEXCEPT :
            start_time(_in.start_time),
            end_time(_in.end_time),
            bytes_sent(_in.bytes_sent),
            bytes_recv(_in.bytes_recv)
        {
            // not needing to guard this string: it's created exactly once
            ::memcpy_s(connection_identifier, ctsStatistics::ConnectionIdLength, _in.connection_identifier, ctsStatistics::ConnectionIdLength);
            connection_identifier[ctsStatistics::ConnectionIdLength - 1] = '\0';
        }

        long long current_bytes() NOEXCEPT
        {
            return this->bytes_recv.get() + this->bytes_sent.get();
        }

        //
        // snap-view will set the returned start time == last read time to capture the delta
        // - and end time == current time
        //
        ctsTcpStatistics snap_view(bool _clear_settings) NOEXCEPT
        {
            long long current_time = ctl::ctTimer::snap_qpc_as_msec();
            long long prior_time_read = (_clear_settings) ?
                this->start_time.set_prior_value(current_time) :
                this->start_time.get_prior_value();

            ctsTcpStatistics return_stats(prior_time_read);
            return_stats.end_time.set(current_time);

            if (_clear_settings) {
                return_stats.bytes_sent.set(this->bytes_sent.snap_value_difference());
                return_stats.bytes_recv.set(this->bytes_recv.snap_value_difference());

            } else {
                return_stats.bytes_sent.set(this->bytes_sent.read_value_difference());
                return_stats.bytes_recv.set(this->bytes_recv.read_value_difference());
            }

            return return_stats;
        }
    };
}

//
// including ctsConfig after the above declarations to avoid circular references between the 2 headers
//
#include "ctsConfig.h"
namespace ctsTraffic {
    namespace ctsStatistics {
        template <typename T>
        void GenerateConnectionId(_In_ T& _statistics_object)
        {
            UUID connection_id;
            RPC_STATUS status = ::UuidCreate(&connection_id);
            if (status != RPC_S_OK) {
                throw ctl::ctException(status, L"UuidCreate", L"ctsStatistics", false);
            }
            RPC_CSTR connection_id_string = nullptr;
            status = ::UuidToStringA(&connection_id, &connection_id_string);
            if (status != RPC_S_OK) {
                throw ctl::ctException(status, L"UuidToStringA", L"ctsStatistics", false);
            }

            ctl::ctFatalCondition(
                ::strlen(reinterpret_cast<LPSTR>(connection_id_string)) != (ConnectionIdLength - 1),
                L"UuidToString returned a string not 36 characters long (%Iu)",
                ::strlen(reinterpret_cast<LPSTR>(connection_id_string)));

            auto copy_error = ::memcpy_s(_statistics_object.connection_identifier, ConnectionIdLength, connection_id_string, ConnectionIdLength);
            ctl::ctFatalCondition(
                copy_error != 0,
                L"memcpy_s failed trying to copy a UUID string (%d)", copy_error);

            ::RpcStringFreeA(&connection_id_string);
            _statistics_object.connection_identifier[ConnectionIdLength - 1] = '\0';
        }

        template <typename T>
        void Start(_In_ T& _statistics_object) NOEXCEPT
        {
            // only calculate the QPC the first time
            // - willing to take the cost of 2 interlocked operations the first time this is initialized
            //   versus taking a QPC hit on every IO request
            if (0LL == _statistics_object.start_time.get()) {
                _statistics_object.start_time.set_conditionally(ctl::ctTimer::snap_qpc_msec(), 0LL);
            }
        }

        template <typename T>
        void End(_In_ T& _statistics_object) NOEXCEPT
        {
            long long prior_end_time = _statistics_object.end_time.set_conditionally(ctl::ctTimer::snap_qpc_msec(), 0LL);
            if (0LL == prior_end_time) {
                ctsConfig::UpdateGlobalStats(stats);
            }
        }
    }
}
