/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

#include <ctVersionConversion.hpp>
#include <ctTimer.hpp>

#include "ctsConfig.h"
#include "ctsSafeInt.hpp"

namespace ctsTraffic {

    typedef struct ctsIOPatternRateLimitThrottle_t ctsIOPatternRateLimitThrottle;
    typedef struct ctsIOPatternRateLimitDontThrottle_t ctsIOPatternRateLimitDontThrottle;

    template <typename Protocol>
    struct ctsIOPatternRateLimitPolicy
    {
        ctsIOPatternRateLimitPolicy() NOEXCEPT
        : bytes_sending_per_quantum(ctsConfig::GetTcpBytesPerSecond() * static_cast<long long>(ctsConfig::Settings->TcpBytesPerSecondPeriod) / 1000LL),
          bytes_sending_this_quantum(0LL);
          quantum_start_time_ms(ctl::ctTimer::snap_qpc_msec())
        {
        }

        long long operator() (const ctsUnsignedLongLong& _buffer_size) NOEXCEPT;

        const ctsSignedLongLong bytes_sending_per_quantum;
        ctsSignedLongLong bytes_sending_this_quantum;
        ctsSignedLongLong quantum_start_time_ms;
    };

    template <>
    long long ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitDontThrottle>::operator() (const ctsUnsignedLongLong&) NOEXCEPT
    {
        // effectively a no-opt
        return 0LL;
    }
    
    template <>
    long long ctsIOPatternRateLimitPolicy<ctsIOPatternRateLimitThrottle>::operator() (const ctsUnsignedLongLong& _buffer_size) NOEXCEPT
    {
        long long time_offset_milliseconds = 0LL;

        auto current_time_ms(ctl::ctTimer::snap_qpc_msec());
        if (this->bytes_sending_this_quantum < this->bytes_sending_per_quantum) {
            // adjust bytes_sending_this_quantum
            this->bytes_sending_this_quantum += _buffer_size;

            // no need to adjust quantum_start_time_ms unless we skipped into a new quantum
            // (meaning the previous quantum had not filled the max bytes for that quantum)
            if (current_time_ms > (this->quantum_start_time_ms + ctsConfig::Settings->TcpBytesPerSecondPeriod)) {
                // current time shows it's now beyond this quantum timeframe
                // - once we see how many quantums we have skipped forward, move our quantum start time to the quantum we are actually in
                // - then adjust the number of bytes we are to send this quantum by how many quantum we just skipped
                auto quantums_skipped_since_last_send = (current_time_ms - this->quantum_start_time_ms) / ctsConfig::Settings->TcpBytesPerSecondPeriod;
                this->quantum_start_time_ms += quantums_skipped_since_last_send * ctsConfig::Settings->TcpBytesPerSecondPeriod;

                // we have to be careful making this adjustment since the remainingbytes this quantum could be very small
                // - we only subtract out if the number of bytes skipped is >= bytes actually skipped
                auto bytes_to_adjust = this->bytes_sending_per_quantum * quantums_skipped_since_last_send;
                if (bytes_to_adjust > this->bytes_sending_this_quantum) {
                    this->bytes_sending_this_quantum = 0;
                } else {
                    this->bytes_sending_this_quantum -= bytes_to_adjust;
                }
            }

        } else {
            // we have sent more than required for this quantum
            // - check if this fullfilled future quantums as well

            auto quantum_ahead_to_schedule = this->bytes_sending_this_quantum / this->bytes_sending_per_quantum;

            // ms_for_quantums_to_skip = the # of quantum beyond the current quantum that will be skipped
            // - when we have already sent at least 1 additional quantum of bytes
            ctsSignedLongLong ms_for_quantums_to_skip = (quantum_ahead_to_schedule - 1LL) * ctsConfig::Settings->TcpBytesPerSecondPeriod;

            // carry forward extra bytes from quantums that will be filled by the bytes we have already sent
            // (including the current quantum)
            // then adding the bytes we're about to send
            this->bytes_sending_this_quantum -= this->bytes_sending_per_quantum * quantum_ahead_to_schedule;
            this->bytes_sending_this_quantum += _buffer_size;

            // update the return task for when to schedule the send
            // first, calculate the time to get to the end of this time quantum
            // - only adjust if the current time isn't already outside this quantum
            if (current_time_ms < this->quantum_start_time_ms + ctsConfig::Settings->TcpBytesPerSecondPeriod) {
                time_offset_milliseconds = this->quantum_start_time_ms + ctsConfig::Settings->TcpBytesPerSecondPeriod - current_time_ms;
            }

            // then add in any quantum we need to skip
            time_offset_milliseconds += ms_for_quantums_to_skip;

            // finally, adjust quantum_start_time_ms to the next quantum which IO will complete
            this->quantum_start_time_ms += ms_for_quantums_to_skip + ctsConfig::Settings->TcpBytesPerSecondPeriod;
        }

        return time_offset_milliseconds;
    }
}

