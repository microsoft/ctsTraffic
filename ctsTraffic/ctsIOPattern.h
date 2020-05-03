/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <memory>
#include <algorithm>
// os headers
#include <windows.h>
// project headers
#include "ctsConfig.h"
#include "ctsIOTask.hpp"
#include "ctsSafeInt.hpp"
#include "ctsIOPatternState.hpp"
#include "ctsStatistics.hpp"
#include <mswsock.h>

namespace ctsTraffic {

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Possible status values returned to the caller upon completing IO
    /// - on failure, these codes can be reported as errors back to the caller
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    enum class ctsIOStatus
    {
        ContinueIo,
        CompletedIo,
        FailedIo
    };

    constexpr int ctsStatusIORunning = MAXINT;
    constexpr int ctsStatusErrorNotAllDataTransferred = MAXINT - 1;
    constexpr int ctsStatusErrorTooMuchDataTransferred = MAXINT - 2;
    constexpr int ctsStatusErrorDataDidNotMatchBitPattern = MAXINT - 3;
    constexpr int ctsStatusMinimumValue = MAXINT - 3;

    class ctsIOPattern {
    public:
        constexpr static bool IsProtocolError(unsigned long _status) noexcept
        {
            return _status >= ctsStatusMinimumValue && _status < ctsStatusIORunning;
        }
        static const wchar_t* BuildProtocolErrorString(unsigned long _status) noexcept
        {
            switch (_status)
            {
                case ctsStatusErrorNotAllDataTransferred:
                    return L"ErrorNotAllDataTransferred";

                case ctsStatusErrorTooMuchDataTransferred:
                    return L"ErrorTooMuchDataTransferred";

                case ctsStatusErrorDataDidNotMatchBitPattern:
                    return L"ErrorDataDidNotMatchBitPattern";

                default:
                    FAIL_FAST_MSG(
                        "ctsIOPattern: internal inconsistency - expecting a protocol error ctsIOProtocolState (%u)", _status);
            }
        }

        ///
        /// Helper factory to build known patterns
        ///
        static std::shared_ptr<ctsIOPattern> MakeIOPattern();
        ///
        /// Making available the shared buffer used for sends and recvs
        ///
        static char* AccessSharedBuffer() noexcept;
        ///
        /// d'tor must be virtual as this is a base pure virtual class
        ///
        virtual ~ctsIOPattern() noexcept;

        ///
        /// Exposing statistics members publicly to ctsSocket
        ///
        virtual void print_stats(const ctl::ctSockaddr& local_addr, const ctl::ctSockaddr& remote_addr) noexcept = 0;

        ///
        /// Some derived IO types require callbacks to the IO functions
        /// - to request tasks from the normal initiate_io / complete_io pattern
        ///
        virtual void register_callback(std::function<void(const ctsIOTask&)> callback) noexcept
        {
            const auto lock = m_cs.lock();
            m_callback = std::move(callback);
        }

        virtual unsigned long get_last_error() const noexcept
        {
            const auto lock = m_cs.lock();
            return m_lastError;
        }

        ctsUnsignedLong get_ideal_send_backlog() const noexcept
        {
            const auto lock = m_cs.lock();
            return m_patternState.get_ideal_send_backlog();
        }
        void set_ideal_send_backlog(const ctsUnsignedLong& new_isb) noexcept
        {
            const auto lock = m_cs.lock();
            m_patternState.set_ideal_send_backlog(new_isb);
        }

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
        ///   _task : the ctsIOTask that was provided to perform
        ///   _current_transfer : the number of bytes successfully transferred from the task
        ///   _status_code: the return code from the prior IO operation [assumes a Win32 error code]
        ///
        ctsIOTask initiate_io() noexcept;
        ctsIOStatus complete_io(const ctsIOTask& original_task, unsigned long current_transfer, unsigned long status_code) noexcept;

        /// no default c'tor
        ctsIOPattern() = delete;
        /// no copy c'tor or copy assignment
        ctsIOPattern(const ctsIOPattern&) = delete;
        ctsIOPattern& operator= (const ctsIOPattern&) = delete;
        ctsIOPattern(ctsIOPattern&&) = delete;
        ctsIOPattern& operator= (ctsIOPattern&&) = delete;

    private:
        ctsIOStatus current_status() const noexcept
        {
            if (ctsStatusIORunning == m_lastError)
            {
                return ctsIOStatus::ContinueIo;
            }

            if (NO_ERROR == m_lastError)
            {
                return ctsIOStatus::CompletedIo;
            }

            // all other values indicate failure
            return ctsIOStatus::FailedIo;
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Private method to return a pre-populated task
        /// - *not* setting the private ctsIOTask::tracked_io property
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ctsIOTask new_task(IOTaskAction action, unsigned long max_transfer) noexcept;

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Private method which must be implemented by the derived interface
        ///
        /// ctsIOTask next_task()
        /// - must return a ctsIOTask returned from tracked_task or untracked_task
        ///
        /// ctsIOPatternProtocolError completed_task(const ctsIOTask&, unsigned long _current_transfer) noexcept
        /// - a notification to the derived class over what task completed
        ///   - ctsIOTask argument: the ctsIOTask which it previously returned from next_task()
        ///   - unsigned long argument:  the # of bytes actually transferred
        /// - cannot throw [if it fails, it must RaiseException to debug]
        /// - returns a unsigned long back to the base class to indicate errors
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        virtual ctsIOTask next_task() = 0;
        virtual ctsIOPatternProtocolError completed_task(const ctsIOTask&, unsigned long current_transfer) noexcept = 0;

        // CS memory guard for data within this object
        // - it's mutable to allow us to take the CS in const methods
        mutable wil::critical_section m_cs;

        // recv buffers to return to the caller
        // - tracking sending buffers separate from receiving buffers
        //   since sending buffers will have a test pattern written to it (thus send buffers can be static)
        // For supporting multiple recv calls, allocating a larger buffer to contain all recv requests
        // - as well as a vector to contain the multiple ptrs to each buffer
        // When needing to dynamically allocate, containing a vector to hold the bytes
        std::vector<char*> m_recvBufferFreeList;
        std::vector<char> m_recvBufferContainer;
        // optional callback for protocols which need to communicate OOB to the IO function
        std::function<void(const ctsIOTask&)> m_callback;

        // track the state of the L4 protocol (TCP or UDP)
        ctsIOPatternState m_patternState;

        // need to track the current offset into the buffer pattern
        // these are separate as we could have both sends and receive operations on the same connection
        ctsSizeT m_sendPatternOffset = 0;
        ctsSizeT m_recvPatternOffset = 0;

        // RIO buffer Id
        RIO_BUFFERID m_recvRioBufferid = RIO_INVALID_BUFFERID;  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
        // tracking time information for scheduling IO at time offsets
        const ctsSignedLongLong m_bytesSendingPerQuantum;
        ctsSignedLongLong m_bytesSendingThisQuantum = 0LL;
        ctsSignedLongLong m_quantumStartTimeMs;

        unsigned long m_lastError = ctsStatusIORunning;

    protected:
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// protected constructor
        ///
        /// - only applicable for the derived types to indicate if will need send or recv buffers
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        explicit ctsIOPattern(unsigned long recv_count);
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// The derived template class for tracking statistics must implement these pure virtual functions
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        virtual void start_stats() noexcept = 0;
        virtual void end_stats() noexcept = 0;
        virtual char* connection_id() noexcept = 0;

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// tracked_task(IOTaskAction, unsigned long _max_transfer)
        /// untracked_task(IOTaskAction, unsigned long _max_transfer)
        ///
        /// - returns a ctsIOTask for the next transfer based on the IOAction
        /// - the returned buffer can be contained to maximum size with _max_transfer
        ///
        /// tracked_tasks will count that IO towards the max_transfer
        /// untracked_tasks will *not* count the IO towards the max_transfer
        /// untracked_tasks will *not* have their buffers validated on complete_io
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ctsIOTask tracked_task(IOTaskAction, unsigned long max_transfer = 0) noexcept;
        ctsIOTask untracked_task(IOTaskAction, unsigned long max_transfer = 0) noexcept;

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Exposing to the derived class the # of bytes to be transferred as tracked in the base class
        /// Make it possible for the derived type to also override the total transfer 
        ///  - to meet its requirements (e.g. must be an even total # for balanced send & recv's)
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ctsUnsignedLongLong get_total_transfer() const noexcept
        {
            return m_patternState.get_max_transfer();
        }
        void set_total_transfer(const ctsUnsignedLongLong& new_total) noexcept
        {
            m_patternState.set_max_transfer(new_total);
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Expose to the derived class the option to verify the buffers in their ctsIOTask which
        /// - they created through untracked_task
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        static bool VerifyBuffer(const ctsIOTask& original_task, unsigned long transferred_bytes) noexcept;

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Expose to the derived class the option to have a ctsIOTask sent OOB to the IO caller
        /// - not that this will take the same internal lock as the publicly exposed functions
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        void send_callback(const ctsIOTask& task) const noexcept
        {
            if (m_callback)
            {
                m_callback(task);
            }
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Enabling derived types to update the internally tracked last-error
        ///
        /// update_last_error will attempt to keep the first error reported
        /// - this will only update the value if an error has not yet been report for this state
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        unsigned long update_last_error(const DWORD error) noexcept
        {
            const auto lock = m_cs.lock();
            if (ctsStatusIORunning == m_lastError)
            {
                const auto status_error = m_patternState.update_error(error);
                if (NO_ERROR == error)
                {
                    if (status_error != ctsIOPatternProtocolError::ErrorIOFailed)
                    {
                        m_lastError = NO_ERROR;
                    }
                }
                else
                {
                    if (status_error == ctsIOPatternProtocolError::ErrorIOFailed)
                    {
                        m_lastError = error;
                    }
                }
            }
            return m_lastError;
        }
        void update_last_protocol_error(ctsIOPatternProtocolError protocol_error) noexcept
        {
            switch (protocol_error)
            {
                case ctsIOPatternProtocolError::CorruptedBytes:
                    this->update_last_error(ctsStatusErrorDataDidNotMatchBitPattern);
                    break;

                case ctsIOPatternProtocolError::TooFewBytes:
                    this->update_last_error(ctsStatusErrorNotAllDataTransferred);
                    break;

                case ctsIOPatternProtocolError::TooManyBytes:
                    this->update_last_error(ctsStatusErrorTooMuchDataTransferred);
                    break;

                case ctsIOPatternProtocolError::SuccessfullyCompleted:
                    this->update_last_error(NO_ERROR);
                    break;

                case ctsIOPatternProtocolError::NoError: break;  // NOLINT(bugprone-branch-clone)
                case ctsIOPatternProtocolError::ErrorIOFailed: break;
                default: break;
            }
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Complex derived patterns that need to obtain a lock can encounter complexities if they have
        ///   their own locks, given that the interface between the base and derived classes allow for
        ///   each to call the other. Base-calling-Drived && Drived-calling-Base patterns have the
        ///   inherant risk of deadlocks.
        ///
        /// Exposing the base class lock for these complex derived types. Most derived types will never
        ///    need this since the lock is always held before a derived interface is invoked by the base
        ///    class.
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        auto base_lock() const noexcept
        {
            return m_cs.lock();
        }
    };
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The ctsIOPattern tracks what IO should be conducted on the socket
    /// * All public methods protect against concurrent calls by taking an object lock
    /// * Templated based off of the type of statistics being tracked by the object
    ///   Currently supporting
    ///   - ctsTcpStatistics
    ///   - ctsUdpStatistics
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename S>
    class ctsIOPatternStatistics : public ctsIOPattern {
    public:
        explicit ctsIOPatternStatistics(unsigned long _recv_count) : ctsIOPattern(_recv_count)
        {
            // servers need to generate a unique connection ID
            if (ctsConfig::IsListening())
            {
                ctsStatistics::GenerateConnectionId(this->stats);
            }
        }

        ~ctsIOPatternStatistics() noexcept
        {
            // guarantee that end_pattern has been called at least once
            ctsIOPatternStatistics<S>::end_stats();
        }

        ctsIOPatternStatistics(const ctsIOPatternStatistics&) = delete;
        ctsIOPatternStatistics& operator=(const ctsIOPatternStatistics&) = delete;
        ctsIOPatternStatistics(ctsIOPatternStatistics&&) = delete;
        ctsIOPatternStatistics& operator=(ctsIOPatternStatistics&&) = delete;

        //
        // Printing of results is controlled by the applicable statistics type
        //
        void print_stats(const ctl::ctSockaddr& local_addr, const ctl::ctSockaddr& remote_addr) noexcept override
        {
            // before printing the final results, make sure the timers are stopped
            if (0 == this->get_last_error() && 0 == stats.current_bytes())
            {
                PrintDebugInfo(L"\t\tctsIOPattern::print_stats : reporting a successful IO completion but transfered zero bytes\n");
                this->update_last_protocol_error(ctsIOPatternProtocolError::TooFewBytes);
            }

            ctsConfig::PrintConnectionResults(
                local_addr,
                remote_addr,
                this->get_last_error(),
                stats);
        }
        ///
        /// ensures that the pattern has started
        /// - provides the method in this way so the caller doesn't have to track if it has started yet
        ///
        void start_stats() noexcept override
        {
            if (0LL == stats.start_time.get())
            {
                // only calculate the QPC the first time
                // - willing to take the cost of 2 interlocked operations the first time this is initialized
                //   versus taking a QPC hit on every IO request
                stats.start_time.set_conditionally(ctl::ctTimer::ctSnapQpcInMillis(), 0LL);
            }
        }
        ///
        /// Exposed to the caller to control when to set the end_time
        /// - if the end_time was previously zero, will also update historic stats
        ///
        void end_stats() noexcept override
        {
            stats.end_time.set_conditionally(ctl::ctTimer::ctSnapQpcInMillis(), 0LL);
        }
        ///
        /// Access the ConnectionId stored in the Stats object
        ///
        char* connection_id() noexcept override
        {
            return stats.connection_identifier;
        }
        ///
        /// Statistics for this object is protected to be accessible from the derived class
        /// - the type is controlled by the caller as the class template type
        ///
        S stats;
    };



    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///     - Pull Pattern
    ///    -- TCP-only
    ///    -- The server pushes data in 'segments' (the size of which is defined in the class)
    ///    -- The client pulls data in 'segments'
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsIOPatternPull final : public ctsIOPatternStatistics<ctsTcpStatistics> {
    public:
        ctsIOPatternPull();
        ~ctsIOPatternPull() noexcept override = default;

        ctsIOPatternPull(const ctsIOPatternPull&) = delete;
        ctsIOPatternPull& operator=(const ctsIOPatternPull&) = delete;
        ctsIOPatternPull(ctsIOPatternPull&&) = delete;
        ctsIOPatternPull& operator=(ctsIOPatternPull&&) = delete;

        // required virtual functions
        ctsIOTask next_task() noexcept override;
        ctsIOPatternProtocolError completed_task(const ctsIOTask& task, unsigned long completed_bytes) noexcept override;

    private:
        const IOTaskAction m_ioAction;
        ctsUnsignedLong m_recvNeeded;
        ctsUnsignedLong m_sendBytesInflight;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  - Push Pattern
    ///    -- TCP-only
    ///    -- The client pushes data in 'segments' (the size of which is defined in the class)
    ///    -- The server pulls data in 'segments'
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsIOPatternPush final : public ctsIOPatternStatistics<ctsTcpStatistics> {
    public:
        ctsIOPatternPush();
        ~ctsIOPatternPush() noexcept override = default;

        ctsIOPatternPush(const ctsIOPatternPush&) = delete;
        ctsIOPatternPush& operator=(const ctsIOPatternPush&) = delete;
        ctsIOPatternPush(ctsIOPatternPush&&) = delete;
        ctsIOPatternPush& operator=(ctsIOPatternPush&&) = delete;

        // required virtual functions
        ctsIOTask next_task() noexcept override;
        ctsIOPatternProtocolError completed_task(const ctsIOTask& task, unsigned long completed_bytes) noexcept override;

    private:
        const IOTaskAction m_ioAction;
        ctsUnsignedLong m_recvNeeded;
        ctsUnsignedLong m_sendBytesInflight;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///     - PushPull Pattern
    ///    -- TCP-only
    ///    -- The client pushes data in 'segments'
    ///    -- The server pulls data in 'segments'
    ///    -- At each segment, roles swap (pusher/puller)
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsIOPatternPushPull final : public ctsIOPatternStatistics<ctsTcpStatistics> {
    public:
        ctsIOPatternPushPull();
        ~ctsIOPatternPushPull() noexcept override = default;

        ctsIOPatternPushPull(const ctsIOPatternPushPull&) = delete;
        ctsIOPatternPushPull& operator=(const ctsIOPatternPushPull&) = delete;
        ctsIOPatternPushPull(ctsIOPatternPushPull&&) = delete;
        ctsIOPatternPushPull& operator=(ctsIOPatternPushPull&&) = delete;

        ctsIOTask next_task() noexcept override;
        ctsIOPatternProtocolError completed_task(const ctsIOTask& task, unsigned long current_transfer) noexcept override;

    private:
        const unsigned long m_pushSegmentSize;
        const unsigned long m_pullSegmentSize;

        ctsUnsignedLong m_intraSegmentTransfer;

        const bool m_listening;
        bool m_ioNeeded;
        bool m_sending;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  - Duplex Pattern
    ///    -- TCP-only
    ///    -- The client both pushes and pulls data concurrently
    ///    -- The server both pushes and pulls data concurrently
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsIOPatternDuplex final : public ctsIOPatternStatistics<ctsTcpStatistics> {
    public:
        ctsIOPatternDuplex();
        ~ctsIOPatternDuplex() noexcept override = default;

        ctsIOPatternDuplex(const ctsIOPatternDuplex&) = delete;
        ctsIOPatternDuplex& operator=(const ctsIOPatternDuplex&) = delete;
        ctsIOPatternDuplex(ctsIOPatternDuplex&&) = delete;
        ctsIOPatternDuplex& operator=(ctsIOPatternDuplex&&) = delete;

        // required virtual functions
        ctsIOTask next_task() noexcept override;
        ctsIOPatternProtocolError completed_task(const ctsIOTask& task, unsigned long completed_bytes) noexcept override;

    private:
        // need to know when to stop sending
        ctsUnsignedLongLong m_remainingSendBytes;
        ctsUnsignedLongLong m_remainingRecvBytes;
        ctsUnsignedLong m_recvNeeded;
        ctsUnsignedLong m_sendBytesInflight;
    };


    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  - UDP Media server
    ///    -- Receives a START message from a client to establish a 'connection'
    ///    -- Streams datagrams at the specified BitRate and FrameRate
    ///    -- Responds to RESEND requests out-of-band from the normal stream
    ///    -- Remains alive until the DONE message is sent from the client
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsIOPatternMediaStreamServer final : public ctsIOPatternStatistics<ctsUdpStatistics> {
    public:
        ctsIOPatternMediaStreamServer();
        ~ctsIOPatternMediaStreamServer() noexcept override = default;

        ctsIOPatternMediaStreamServer(const ctsIOPatternMediaStreamServer&) = delete;
        ctsIOPatternMediaStreamServer& operator=(const ctsIOPatternMediaStreamServer&) = delete;
        ctsIOPatternMediaStreamServer(ctsIOPatternMediaStreamServer&&) = delete;
        ctsIOPatternMediaStreamServer& operator=(ctsIOPatternMediaStreamServer&&) = delete;

        // required virtual functions
        ctsIOTask next_task() noexcept override;
        ctsIOPatternProtocolError completed_task(const ctsIOTask& task, unsigned long current_transfer) noexcept override;

    private:
        ctsUnsignedLong m_frameSizeBytes;
        ctsUnsignedLong m_currentFrameRequested;
        ctsUnsignedLong m_currentFrameCompleted;
        ctsUnsignedLong m_frameRateFps;
        ctsUnsignedLong m_currentFrame;
        ctsSignedLongLong m_baseTimeMilliseconds;
        enum class ServerState
        {
            NotStarted,
            IdSent,
            IoStarted
        } m_state;
    };


    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  - UDP Media client
    ///    -- Sends a START message to the server to establish a 'connection'
    ///    -- Receives a stream of datagrams at the specified BitRate and FrameRate
    ///    -- Sends a RESEND requests out-of-band from the normal stream if peeks ahead 
    ///       and sees a missing frame
    ///    -- Processes frames after a Buffering period of time
    ///    -- Sends a DONE message to the server after processing all frames
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsIOPatternMediaStreamClient final : public ctsIOPatternStatistics<ctsUdpStatistics> {
    public:
        ctsIOPatternMediaStreamClient();
        ~ctsIOPatternMediaStreamClient() noexcept override;

        ctsIOPatternMediaStreamClient(const ctsIOPatternMediaStreamClient&) = delete;
        ctsIOPatternMediaStreamClient& operator=(const ctsIOPatternMediaStreamClient&) = delete;
        ctsIOPatternMediaStreamClient(ctsIOPatternMediaStreamClient&&) = delete;
        ctsIOPatternMediaStreamClient& operator=(ctsIOPatternMediaStreamClient&&) = delete;

        // required virtual functions
        ctsIOTask next_task() noexcept override;
        ctsIOPatternProtocolError completed_task(const ctsIOTask& task, unsigned long completed_bytes) noexcept override;

    private:
        // private member variables
        PTP_TIMER m_rendererTimer = nullptr;
        PTP_TIMER m_startTimer = nullptr;

        long long m_baseTimeMilliseconds = 0LL;
        const double m_frameRateMsPerFrame = 0LL;
        const unsigned long m_frameSizeBytes = ctsConfig::GetMediaStream().FrameSizeBytes;
        const unsigned long m_finalFrame = ctsConfig::GetMediaStream().StreamLengthFrames;

        unsigned long m_initialBufferFrames = ctsConfig::GetMediaStream().BufferedFrames;
        unsigned long m_timerWheelOffsetFrames = 0UL;
        unsigned long m_recvNeeded = ctsConfig::Settings->PrePostRecvs;

        // these must be protected by the base class cs
        // - the base lock is always taken before our virtual functions are called
        // - so this is most important to know in our timer callback

        // member variables that require the base lock
        _Requires_lock_held_(cs)
            std::vector<ctsConfig::JitterFrameEntry> m_frameEntries;

        _Requires_lock_held_(cs)
            std::vector<ctsConfig::JitterFrameEntry>::iterator m_headEntry;

        // tracking for jitter information
        ctsConfig::JitterFrameEntry m_firstFrame;
        ctsConfig::JitterFrameEntry m_previousFrame;

        bool m_finishedStream = false;

        // member functions - all require the base lock
        _Requires_lock_held_(cs)
            std::vector<ctsConfig::JitterFrameEntry>::iterator find_sequence_number(long long seq_number) noexcept;

        _Requires_lock_held_(cs)
            bool received_buffered_frames() noexcept;

        _Requires_lock_held_(cs)
            bool set_next_timer(bool initial_timer) const noexcept;

        _Requires_lock_held_(cs)
            void set_next_start_timer() const noexcept;

        _Requires_lock_held_(cs)
            void render_frame() noexcept;

        /// The "Renderer" processes frames at the specified frame rate
        static
            VOID CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID context, PTP_TIMER) noexcept;
        /// Callback to track when the server has actually started sending
        static
            VOID CALLBACK StartCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID context, PTP_TIMER) noexcept;
    };

} //namespace
