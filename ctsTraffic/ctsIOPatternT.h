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
#include <ctVersionConversion.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsIOTask.hpp"
#include "ctsIOPatternProtocolPolicy.hpp"
#include "ctsIOPatternRateLimitPolicy.hpp"


namespace ctsTraffic
{
    class ctsIOPattern
    {
    public:
        virtual ~ctsIOPattern() = 0;
        //
        // initiate_io() can be called repeatedly by the caller if they want overlapping IO calls
        //   without being forced to wait for complete_io() for the next IO request
        //
        virtual ctsIOTask initiate_io() NOEXCEPT = 0;
        //
        // complete_io() should be called for every returned initiate_io with the following:
        //   _task : the ctsIOTask that was provided from initiate_io (or a complete copy)
        //   _bytes_transferred : the number of bytes successfully transferred from the task
        //   _status_code: the return code from the prior IO operation [assumes a Win32 error code]
        //
        virtual ctsIOStatus complete_io(const ctsIOTask& _task, unsigned long _bytes_transferred, unsigned long _status_code) NOEXCEPT = 0;
        //
        // Enabling callers to trigger writing statistics via ctsConfig
        //
        virtual void print_stats(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr) NOEXCEPT = 0;
        //
        // Some IO Patterns require callbacks to the IO functions to request tasks outside the normal
        //   initiate_io / complete_io pattern
        //
        virtual void register_callback(std::function<void(const ctsIOTask&)> _callback) = 0;
        //
        // Exposing the last recorded error from the requested IO
        //
        virtual unsigned long get_last_error() const NOEXCEPT = 0;
    };

    template <
        typename StatisticsPolicy,
        typename IOPatternPolicy,
        typename ProtocolPolicy,
        typename RateLimitPolicy,
        typename BufferPolicy>
    class ctsIOPatternT : public ctsIOPattern
    {
    public:
        ctsIOPatternT()
        {
            if (!::InitializeCriticalSectionEx(&cs, 4000, 0)) {
                throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsIOPattern", false);
            }
        }
        ~ctsIOPattern() = default;

        void print_stats(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr) NOEXCEPT override final
        {
            // before printing the final results, make sure the timers are stopped
            if (0 == this->get_last_error() && 0 == stats.current_bytes()) {
                PrintDebugInfo(L"\t\tctsIOPattern::print_stats : reporting a successful IO completion but transfered zero bytes\n");
                this->protocol_policy.update_protocol_error(ctsIOPatternProtocolError::TooFewBytes);
            }
            ctsConfig::PrintConnectionResults(
                _local_addr,
                _remote_addr,
                this->get_last_error(),
                stats);
        }

        void register_callback(std::function<void(const ctsIOTask&)> _callback) override final
        {
            ctl::ctAutoReleaseCriticalSection take_lock(&this->cs);
            this->callback = std::move(_callback);
        }

        unsigned long get_last_error() const NOEXCEPT override final
        {
            ctl::ctAutoReleaseCriticalSection auto_lock(&this->cs);
            return this->protocol_policy.get_last_error();
        }

        /// no copy c'tor or copy assignment
        ctsIOPatternT(const ctsIOPatternT&) = delete;
        ctsIOPatternT& operator= (const ctsIOPatternT&) = delete;

        ctsIOTask initiate_io() NOEXCEPT override final
        {
            // make sure stats starts tracking IO at the first IO request
            ctsStatistics::Start(this->stats_policy);

            ctsIOTask return_task;
            ctAutoReleaseCriticalSection local_cs(&this->cs);

            switch (this->protocol_policy.get_next_task()) {
                case ctsIOPatternProtocolPolicyTask::MoreIo:
                    return_task = this->iopattern_policy.next_task();
                    break;

                case ctsIOPatternProtocolPolicyTask::NoIo:
                    break;

                case ctsIOPatternProtocolPolicyTask::SendConnectionId:
                    return_task = ctsIOBuffers::NewConnectionIdBuffer(stats.connection_identifier);
                    return_task.ioAction = IOTaskAction::Send;
                    break;

                case ctsIOPatternProtocolPolicyTask::RecvConnectionId:
                    return_task = ctsIOBuffers::NewConnectionIdBuffer(stats.connection_identifier);
                    return_task.ioAction = IOTaskAction::Recv;
                    break;

                case ctsIOPatternProtocolPolicyTask::SendCompletion:
                    // end-stats as early as possible after the actual IO finished
                    ctsStatistics::End(this->stats_policy);

                    // using the static buffer - identical for both RIO and non-RIO
                    // - currently won't be validating the completion message
                    return_task.ioAction = IOTaskAction::Send;
                    return_task.buffer = s_ProtectedSharedBuffer;
                    return_task.rio_bufferid = s_SharedBufferId;
                    return_task.buffer_length = s_CompletionMessageSize;
                    return_task.buffer_offset = s_SharedBufferSize - s_CompletionMessageSize;
                    return_task.track_io = false;
                    return_task.buffer_type = ctsIOTask::BufferType::Static;
                    break;

                case ctsIOPatternProtocolPolicyTask::RecvCompletion:
                    // end-stats as early as possible after the actual IO finished
                    ctsStatistics::End(this->stats_policy);

                    // using the static buffer - identical for both RIO and non-RIO
                    // - currently won't be validating the completion message
                    return_task.ioAction = IOTaskAction::Recv;
                    return_task.buffer = s_WriteableSharedBuffer;
                    return_task.rio_bufferid = s_SharedBufferId;
                    return_task.buffer_length = s_CompletionMessageSize;
                    return_task.buffer_offset = s_SharedBufferSize - s_CompletionMessageSize;
                    return_task.track_io = false;
                    return_task.buffer_type = ctsIOTask::BufferType::Static;
                    break;

                case ctsIOPatternProtocolPolicyTask::HardShutdown:
                    // end-stats as early as possible after the actual IO finished
                    ctsStatistics::End(this->stats_policy);

                    return_task.ioAction = IOTaskAction::HardShutdown;
                    return_task.buffer = nullptr;
                    return_task.buffer_length = 0;
                    return_task.buffer_offset = 0;
                    return_task.track_io = false;
                    return_task.buffer_type = ctsIOTask::BufferType::Null;
                    break;

                case ctsIOPatternProtocolPolicyTask::GracefulShutdown:
                    // end-stats as early as possible after the actual IO finished
                    ctsStatistics::End(this->stats_policy);

                    return_task.ioAction = IOTaskAction::GracefulShutdown;
                    return_task.buffer = nullptr;
                    return_task.buffer_length = 0;
                    return_task.buffer_offset = 0;
                    return_task.track_io = false;
                    return_task.buffer_type = ctsIOTask::BufferType::Null;
                    break;

                case ctsIOPatternProtocolPolicyTask::RequestFIN:
                    // post one final recv for the zero byte FIN
                    // end-stats as early as possible after the actual IO finished
                    ctsStatistics::End(this->stats_policy);

                    ctFatalCondition(
                        this->recv_buffer_free_list.empty(),
                        L"ctsIOPattern::initiate_io : (%p) recv_buffer_free_list is empty", this);

                    if (this->recv_rio_bufferid != RIO_INVALID_BUFFERID) {
                        // RIO must always use the allocated buffers which were registered
                        return_task.buffer = *this->recv_buffer_free_list.rbegin();
                        this->recv_buffer_free_list.pop_back();
                        return_task.rio_bufferid = this->recv_rio_bufferid;
                        return_task.buffer_type = ctsIOTask::BufferType::Tracked;
                    } else {
                        return_task.buffer = s_FinBuffer;
                        return_task.buffer_type = ctsIOTask::BufferType::Static;
                    }

                    return_task.ioAction = IOTaskAction::Recv;
                    return_task.buffer_length = s_FinBufferSize;
                    return_task.buffer_offset = 0;
                    return_task.track_io = false;
                    break;

                default:
                    ctAlwaysFatalCondition(L"ctsIOPattern::initiate_io was called in an invalid state: dt %p ctsTraffic!ctsTraffic::ctsIOPattern", this);
            }

            this->protocol_policy.notify_next_task(return_task);
            return return_task;
        }

        ctsIOStatus complete_io(const ctsIOTask& _task, unsigned long _bytes_transferred, unsigned long _status_code) NOEXCEPT override final
        {
            ctAutoReleaseCriticalSection local_cs(&this->cs);

            // Only add the recv buffer back if it was one of our listed recv buffers
            if (ctsIOTask::BufferType::Tracked == _original_task.buffer_type) {
                this->recv_buffer_free_list.push_back(_original_task.buffer);
            }

            // preserve the previous task
            bool notify_iopattern = this->protocol_policy.is_more_io();

            switch (_original_task.ioAction) {
                case IOTaskAction::None:
                    // ignore completions for tasks on None
                    break;

                case IOTaskAction::FatalAbort:
                    PrintDebugInfo(L"\t\tctsIOPattern : completing a FatalAbort\n");
                    this->protocol_policy.update_last_error(ctsStatusErrorNotAllDataTransferred);
                    break;

                case IOTaskAction::Abort:
                    PrintDebugInfo(L"\t\tctsIOPattern : completing an Abort\n");
                    break;

                case IOTaskAction::GracefulShutdown:
                    // Fall-through to be processed like send or recv IO
                    PrintDebugInfo(L"\t\tctsIOPattern : completing a GracefulShutdown\n");
                case IOTaskAction::HardShutdown:
                    // Fall-through to be processed like send or recv IO
                    PrintDebugInfo(L"\t\tctsIOPattern : completing a HardShutdown\n");
                case IOTaskAction::Recv:
                    // Fall-through to Send - where the IO will be processed
                case IOTaskAction::Send:
                {
                    bool verify_io = true;
                    if (ctsIOTask::BufferType::TcpConnectionId == _original_task.buffer_type) {
                        //
                        // not verifying the IO buffer if this is the connection id request
                        // - but must complete the task to update the protocol
                        //
                        verify_io = false;

                        if (_status_code != NO_ERROR) {
                            this->protocol_policy.update_last_error(_status_code);

                        } else {
                            if (IOTaskAction::Recv == _original_task.ioAction) {
                                // save off the connection ID when we receive it
                                if (!ctsIOBuffers::SetConnectionId(this->connection_id(), _original_task, _current_transfer)) {
                                    this->protocol_policy.update_last_error(ctsStatusErrorDataDidNotMatchBitPattern);
                                }
                            }

                            // process the TCP protocol state machine in pattern_state after receiving the connection id
                            this->protocol_policy.completed_task(_original_task, _current_transfer);

                            if (_original_task.ioAction == IOTaskAction::Send) {
                                ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_current_transfer);
                            } else {
                                ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_current_transfer);
                            }
                        }
                        ctsIOBuffers::ReleaseConnectionIdBuffer(_original_task);

                    } else if (_status_code != NO_ERROR) {
                        //
                        // if the IO task failed, the entire IO pattern is now failed
                        // - unless this is an extra recv that was canceled once we completed the transfer
                        //
                        if (IOTaskAction::Recv == _original_task.ioAction && this->protocol_policy.is_completed()) {
                            PrintDebugInfo(L"\t\tctsIOPattern : Recv failed after the pattern completed (error %u)\n", _status_code);
                        } else {
                            this->protocol_policy.update_last_error(_status_code);
                            if (!this->protocol_policy.is_completed()) {
                                PrintDebugInfo(L"\t\tctsIOPattern : Recv failed before the pattern completed (error %u)\n", _status_code);
                                verify_io = false;
                            }
                        }
                    }

                    if (verify_io) {
                        //
                        // IO succeeded - update state machine with the completed task if this task had IO
                        //
                        auto pattern_status = this->protocol_policy.completed_task(_original_task, _current_transfer);
                        //
                        // if this is a TCP receive completion
                        // and no IO or protocol errors
                        // and the user requested to verify buffers
                        // then actually validate the received completion
                        //
                        if (pattern_status != ctsIOStatus::FailedIo) {
                        /*
                        if (ctsConfig::Settings->Protocol == ctsConfig::ProtocolType::TCP &&
                            ctsConfig::Settings->ShouldVerifyBuffers &&
                            _original_task.ioAction == IOTaskAction::Recv &&
                            _original_task.track_io &&
                            (ctsIOPatternProtocolError::SuccessfullyCompleted == pattern_status || ctsIOPatternProtocolError::NoError == pattern_status)) {
                        */
                            if (!this->buffer_policy.verify_buffer(_original_task, _current_transfer)) {
                                this->protocol_policy.update_last_error(ctsStatusErrorDataDidNotMatchBitPattern);
                            }
                        }
                    }
                    break;
                }
            }
            //
            // Notify the protocol policy that the task completed
            // - if this wasn't our internal connection id request
            // - if there wasn't an error with the IO and an IO operation completed
            // If the derived interface returns an error,
            // - update the last_error status
            //
            if ((_original_task.ioAction != IOTaskAction::None) &&
                (NO_ERROR == _status_code)) {

                if (IOTaskAction::Send == _original_task.ioAction) {
                    ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_current_transfer);
                } else {
                    ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_current_transfer);
                }
                // only complete tasks that were requested
                if (notify_iopattern) {
                    this->protocol_policy.update_protocol_error(
                        this->iopattern_policy.completed_task(_original_task, _current_transfer));
                }
            }
            //
            // If the state machine has verified the connection has completed, 
            // - set the last error to zero in case it was not already set to an error
            //   but do this *after* the other possible failure points were checked
            //
            if (this->protocol_policy.is_completed()) {
                this->protocol_policy.update_last_error(NO_ERROR);
                ctsStatistics::End(this->stats_policy);
            }

            return this->protocol_policy.get_status();
        }


    private:
        // callers can use ctl::ctAutoReleaseCriticalSection on this class-wide lock
        CRITICAL_SECTION cs;
        // optional callback for protocols which need to communicate OOB to the IO function
        std::function<void(const ctsIOTask&)> callback = nullptr;

        StatisticsPolicy stats_policy;
        IOPatternPolicy iopattern_policy;
        ctsIOPatternProtocolPolicy<ProtocolPolicy> protocol_policy;
        ctsIOPatternRateLimitPolicy<RateLimitPolicy> ratelimit_policy;
        ctsIOPatternBufferPolicy<BufferPolicy> buffer_policy;
        ///
        /// void start_stats() NOEXCEPT
        /// - has been replaced with ctsStatistics::Start(this->stats_policy)
        ///

        ///
        /// void end_stats() NOEXCEPT
        /// - has been replaced with ctsStatistics::End(this->stats_policy)
        ///

        ///
        /// char* connection_id() NOEXCEPT
        /// - has been replaced with stats.connection_identifier;
        ///

        /// class ctsIOPatternProtocolPolicy
        ///
        /// ctsUnsignedLongLong get_remaining_transfer() const NOEXCEPT
        /// ctsUnsignedLongLong get_max_transfer() const NOEXCEPT
        /// void set_max_transfer(const ctsUnsignedLongLong& _new_max_transfer) NOEXCEPT
        /// bool is_completed() const NOEXCEPT
        //  bool is_current_task_more_io() const NOEXCEPT
        /// unsigned long update_protocol_error(ctsIOPatternProtocolError _protocol_error) NOEXCEPT
        /// unsigned long update_last_error(unsigned long _error_code) NOEXCEPT
        /// unsigned long get_last_error() const NOEXCEPT
        /// 
        /// callers are expected to follow this pattern when working with tasks:
        ///
        /// get_next_task() : returns what is expected next in the protocol
        /// notify_next_task() : updates the state machine with what task is about to be processed
        /// completed_task() : updates the state machine with the result of the processed task
        ///
        /// ctsIOPatternProtocolPolicyTask get_next_task() const NOEXCEPT;
        /// void notify_next_task(const ctsIOTask& _next_task) NOEXCEPT;
        /// void completed_task(const ctsIOTask& _completed_task, unsigned long _completed_transfer_bytes) NOEXCEPT;

    };
}