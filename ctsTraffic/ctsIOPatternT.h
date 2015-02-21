/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <functional>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctSockaddr.hpp>
#include <ctLocks.hpp>
#include <ctTimer.hpp>
#include <ctVersionConversion.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsIOTask.hpp"
#include "ctsIOPatternProtocolPolicy.hpp"
#include "ctsIOPatternRateLimitPolicy.hpp"


namespace ctsTraffic {

    enum class ctsIOStatus
    {
        ContinueIo,
        CompletedIo,
        FailedIo
    };

    class ctsIOPattern
    {
    public:
        ///
        /// none of these *_io functions can throw
        /// failures are critical and will RaiseException to be debugged
        /// - the task given by initiate_io should be returned through complete_io
        ///   (or a copy of that task)
        ///
        /// Callers access initiate_io() to retrieve a ctsIOTask object for the next IO operation
        /// - they are expected to retain that ctsIOTask object until the IO operation completes
        /// - at which time they pass it back to complete_io()
        ///
        /// initiate_io() can be called repeatedly by the caller if they want overlapping IO calls
        /// - without forced to wait for complete_io() for the next IO request
        ///
        /// complete_io() should be called for every returned initiate_io with the following:
        ///   _task : the ctsIOTask that was provided from initiate_io (or a complete copy)
        ///   _bytes_transferred : the number of bytes successfully transferred from the task
        ///   _status_code: the return code from the prior IO operation [assumes a Win32 error code]
        ///
        virtual ctsIOTask initiate_io() NOEXCEPT = 0;
        virtual ctsIOStatus complete_io(const ctsIOTask& _task, unsigned long _bytes_transferred, unsigned long _status_code) NOEXCEPT = 0;

        ///
        /// Enabling callers to trigger writing statistics via ctsConfig
        ///
        virtual void print_stats(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr) NOEXCEPT = 0;

        ///
        /// Some derived IO types require callbacks to the IO functions
        /// - to request tasks outside the typical initiate_io / complete_io pattern
        ///
        virtual void register_callback(std::function<void(const ctsIOTask&)> _callback) = 0;

        ///
        /// Exposing the last recorded error from the requested IO
        ///
        virtual unsigned long get_last_error() const NOEXCEPT = 0;
    };


    template <typename Stats,
              typename ProtocolPolicy,
              typename RateLimitPolicy>
    class ctsIOPatternT : public ctsIOPattern
    {
    public:
        ctsIOPatternT()
        {
            if (!::InitializeCriticalSectionEx(&cs, 4000, 0)) {
                throw ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsIOPattern", false);
            }
        }

        virtual void print_stats(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr) NOEXCEPT override final
        {
            // before printing the final results, make sure the timers are stopped
            if (0 == this->get_last_error() && 0 == stats.current_bytes()) {
                ctsConfig::PrintDebug(L"ctsIOPattern::print_stats : reporting a successful IO completion but transfered zero bytes");
                this->protocol_policy.update_protocol_error(ctsIOPatternProtocolError::TooFewBytes);
            }
            ctsConfig::PrintConnectionResults(
                _local_addr,
                _remote_addr,
                this->get_last_error(),
                stats);
        }

        virtual void register_callback(std::function<void(const ctsIOTask&)> _callback) override final
        {
            ctl::ctAutoReleaseCriticalSection take_lock(&this->cs);
            this->callback = _callback;
        }

        virtual unsigned long get_last_error() const NOEXCEPT override final
        {
            ctl::ctAutoReleaseCriticalSection auto_lock(&this->cs);
            return this->protocol_policy.get_last_error();
        }

        /// no default c'tor, copy c'tor or copy assignment
        ctsIOPatternT() = delete;
        ctsIOPatternT(const ctsIOPatternT&) = delete;
        ctsIOPatternT& operator= (const ctsIOPatternT&) = delete;

    private:
        // callers can use ctl::ctAutoReleaseCriticalSection on this class-wide lock
        CRITICAL_SECTION cs;
        // optional callback for protocols which need to communicate OOB to the IO function
        std::function<void(const ctsIOTask&)> callback = nullptr;

        Stats stats;
        ctsIOPatternProtocolPolicy<ProtocolPolicy> protocol_policy;
        ctsIOPatternRateLimitPolicy<RateLimitPolicy> ratelimit_policy;

        ///
        /// void start_stats() NOEXCEPT
        /// - has been replaced with ctsStatistics::Start(this->stats)
        ///

        ///
        /// void end_stats() NOEXCEPT
        /// - has been replaced with ctsStatistics::End(this->stats)
        ///

        ///
        /// char* connection_id() NOEXCEPT
        /// - has been replaced with stats.connection_identifier;
        ///
    };
}