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
// using wil::networking to pull in all necessary networking headers
#include "e:/users/kehor/source/repos/wil_keith_horton/include/wil/networking.h"
// project headers
#include "ctsConfig.h"
#include "ctsIOTask.hpp"
#include "ctsIOPatternProtocolPolicy.hpp"

namespace ctsTraffic
{
enum class ctsIoStatus : std::uint8_t
{
    ContinueIo,
    CompletedIo,
    FailedIo
};

class ctsIoPattern
{
public:
    virtual ~ctsIoPattern() = default;

    ctsIoPattern(const ctsIoPattern&) = delete;
    ctsIoPattern& operator=(const ctsIoPattern&) = delete;
    ctsIoPattern(ctsIoPattern&&) = delete;
    ctsIoPattern& operator=(ctsIoPattern&&) = delete;

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
    ctsTask InitiateIo() noexcept;
    virtual ctsIoStatus CompleteIo(const ctsTask& task, uint32_t currentTransfer, uint32_t statusCode) noexcept = 0;

    ///
    /// Enabling callers to trigger writing statistics via ctsConfig
    ///
    virtual void PrintStatistics(const socket_address& localAddr, const socket_address& remoteAddr) noexcept = 0;

    ///
    /// Some derived IO types require callbacks to the IO functions
    /// - to request tasks outside the typical initiate_io / complete_io pattern
    ///
    virtual void RegisterCallback(std::function<void(const ctsTask&)> callback) = 0;

    ///
    /// Exposing the last recorded error from the requested IO
    ///
    [[nodiscard]] virtual uint32_t GetLastPatternError() const noexcept = 0;
};


template <typename Stats,
    typename ProtocolPolicy,
    typename RateLimitPolicy>
class ctsIoPatternT : public ctsIoPattern
{
public:
    ctsIoPatternT() = default;
    ~ctsIoPatternT() override = default;

    void PrintStatistics(const socket_address& localAddr, const socket_address& remoteAddr) noexcept final
    {
        // before printing the final results, make sure the timers are stopped
        if (0 == GetLastPatternError() && 0 == m_stats.current_bytes())
        {
            PRINT_DEBUG_INFO(L"\t\tctsIOPattern::PrintStatistics : reporting a successful IO completion but transfered zero bytes\n");
            m_protocolPolicy.update_protocol_error(ctsIoPatternError::TooFewBytes);
        }
        ctsConfig::PrintConnectionResults(
            localAddr,
            remoteAddr,
            GetLastPatternError(),
            m_stats);
    }

    void RegisterCallback(std::function<void(const ctsTask&)> callback) final
    {
        const auto takeLock = m_cs.lock();
        m_callback = std::move(callback);
    }

    uint32_t GetLastPatternError() const noexcept final
    {
        const auto autoLock = m_cs.lock();
        return m_protocolPolicy.get_last_error();
    }

    ctsIoPatternT(const ctsIoPatternT&) = delete;
    ctsIoPatternT& operator=(const ctsIoPatternT&) = delete;
    ctsIoPatternT(ctsIoPatternT&&) = delete;
    ctsIoPatternT& operator=(ctsIoPatternT&&) = delete;

private:
    mutable wil::critical_section m_cs{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
    // optional callback for protocols which need to communicate OOB to the IO function
    std::function<void(const ctsTask&)> m_callback = nullptr;

    Stats m_stats;
    ctsIoPatternProtocolPolicy<ProtocolPolicy> m_protocolPolicy;
    ctsIoPatternRateLimitPolicy<RateLimitPolicy> m_ratelimitPolicy;

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
