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
#include <ctSockaddr.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsIOTask.hpp"
#include "ctsIOPatternProtocolPolicy.hpp"
#include "ctsIOPatternRateLimitPolicy.hpp"

namespace ctsTraffic
{

    enum class ctsIOStatus
    {
        ContinueIo,
        CompletedIo,
        FailedIo
    };

    class ctsIOPattern
    {
    public:
        virtual ~ctsIOPattern() = default;

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
        ctsIOTask initiate_io() noexcept;
        virtual ctsIOStatus complete_io(const ctsIOTask& task, unsigned long bytes_transferred, unsigned long status_code) noexcept = 0;

        ///
        /// Enabling callers to trigger writing statistics via ctsConfig
        ///
        virtual void print_stats(const ctl::ctSockaddr& local_addr, const ctl::ctSockaddr& remote_addr) noexcept = 0;

        ///
        /// Some derived IO types require callbacks to the IO functions
        /// - to request tasks outside the typical initiate_io / complete_io pattern
        ///
        virtual void register_callback(std::function<void(const ctsIOTask&)> callback) = 0;

        ///
        /// Exposing the last recorded error from the requested IO
        ///
        [[nodiscard]] virtual unsigned long get_last_error() const noexcept = 0;
    };


    template <typename Stats,
        typename ProtocolPolicy,
        typename RateLimitPolicy>
        class ctsIOPatternT : public ctsIOPattern
    {
    public:
        ctsIOPatternT() = default;
        ~ctsIOPatternT() = default;

        void print_stats(const ctl::ctSockaddr& local_addr, const ctl::ctSockaddr& remote_addr) noexcept final
        {
            // before printing the final results, make sure the timers are stopped
            if (0 == this->get_last_error() && 0 == m_stats.current_bytes())
            {
                PRINT_DEBUG_INFO(L"\t\tctsIOPattern::print_stats : reporting a successful IO completion but transfered zero bytes\n");
                this->m_protocolPolicy.update_protocol_error(ctsIOPatternProtocolError::TooFewBytes);
            }
            ctsConfig::PrintConnectionResults(
                local_addr,
                remote_addr,
                this->get_last_error(),
                m_stats);
        }

        void register_callback(std::function<void(const ctsIOTask&)> callback) final
        {
            const auto take_lock = m_cs.lock();
            this->m_callback = std::move(callback);
        }

        unsigned long get_last_error() const noexcept final
        {
            const auto auto_lock = m_cs.lock();
            return this->m_protocolPolicy.get_last_error();
        }

        /// no copy c'tor or copy assignment
        ctsIOPatternT(const ctsIOPatternT&) = delete;
        ctsIOPatternT& operator= (const ctsIOPatternT&) = delete;
        ctsIOPatternT(ctsIOPatternT&&) = delete;
        ctsIOPatternT& operator= (ctsIOPatternT&&) = delete;

    private:
        mutable wil::critical_section m_cs;
        // optional callback for protocols which need to communicate OOB to the IO function
        std::function<void(const ctsIOTask&)> m_callback = nullptr;

        Stats m_stats;
        ctsIOPatternProtocolPolicy<ProtocolPolicy> m_protocolPolicy;
        ctsIOPatternRateLimitPolicy<RateLimitPolicy> m_ratelimitPolicy;

        ///
        /// void start_stats() noexcept
        /// - has been replaced with ctsStatistics::Start(this->stats)
        ///

        ///
        /// void end_stats() noexcept
        /// - has been replaced with ctsStatistics::End(this->stats)
        ///

        ///
        /// char* connection_id() noexcept
        /// - has been replaced with stats.connection_identifier;
        ///
    };
}