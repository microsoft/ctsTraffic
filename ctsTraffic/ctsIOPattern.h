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
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>

// ctl header
#include <ctLocks.hpp>
#include <ctString.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsIOTask.hpp"
#include "ctsPrintStatus.hpp"


namespace ctsTraffic {

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Possible status values returned to the caller upon completing IO
    /// - on failure, these codes can be reported as errors back to the caller
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    enum ctsIOPatternStatus : unsigned long {
        MoreData = 0,
        RequestFIN,    // TCP: next ask for IO will be a recv for the zero-byte FIN
        VerifyFIN,     // TCP: final recv has been posted for the zero-byte FIN
        CompletedTransfer,
        ErrorIOFailed,
        // Protocol errors need unique error codes (not a Win32 error)
        ErrorNotAllDataTransferred = MAXINT - 1,
        ErrorTooMuchDataTransferred = MAXINT - 2,
        ErrorDataDidNotMatchBitPattern = MAXINT - 3,
        // next should always be the lowest error value
        ErrorPatternMinimumValue = MAXINT - 4
    };
    // MAXINT is reserved for internal status of IO continuing
    static const unsigned long ctsIOPatternStatusIORunning = MAXINT;

    inline
    bool ctsIOPatternContinueIO(ctsIOPatternStatus _status)  throw()
    {
        return (_status < CompletedTransfer);
    }
    inline
    bool ctsIOPatternError(ctsIOPatternStatus _status) throw()
    {
        return (_status > CompletedTransfer);
    }
    inline
    bool ctsIOPatternProtocolError(ctsIOPatternStatus _status) throw()
    {
        return (_status > ErrorPatternMinimumValue);
    }
    inline
    const wchar_t* ctsIOPatternProtocolErrorString(ctsIOPatternStatus _status) throw()
    {
        switch (_status) {
            case ErrorNotAllDataTransferred:
                return L"ErrorNotAllDataTransferred";

            case ErrorTooMuchDataTransferred:
                return L"ErrorTooMuchDataTransferred";

            case ErrorDataDidNotMatchBitPattern:
                return L"ErrorDataDidNotMatchBitPattern";

            default:
                ctl::ctAlwaysFatalCondition(
                    L"ctsIOPattern: internal inconsistency - expecting a protocol error ctsIOPatternStatus (%u)", _status);
                return nullptr;
        }
    }

    class ctsIOPattern {
    public:
        ///
        /// Helper factory to build known patterns
        ///
        static std::shared_ptr<ctsIOPattern> MakeIOPattern();
        ///
        /// Making available the shared buffer used for sends and recvs
        ///
        static char* AccessSharedBuffer() throw();

        virtual ~ctsIOPattern();
        virtual void print_io_results(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error) const = 0;
        virtual void end_pattern() throw() = 0;

        ///
        /// none of these *_io functions can throw
        /// failures are critical and will RaiseException to be debugged
        /// - the task given by initiate_io should be returned through complete_io
        ///   (or a copy of that task)
        ///
        ctsIOTask initiate_io() throw();
        ctsIOPatternStatus complete_io(const ctsIOTask& _task, unsigned long _bytes_transferred, unsigned long _status_code) throw();
        unsigned long verify_io() throw();

        ///
        /// Some derived IO types require callbacks to the IO functions
        /// - to request tasks from the normal initiate_io / complete_io pattern
        ///
        void register_callback(std::function<void(const ctsIOTask&)> _callback);

        /// hide the default c'tor
        ctsIOPattern() = delete;
        /// hide the copy c'tor and copy assignment
        ctsIOPattern(const ctsIOPattern&) = delete;
        ctsIOPattern& operator= (const ctsIOPattern&) = delete;

    private:
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Private method to return a pre-populated task
        /// - *not* setting the private ctsIOTask::tracked_io property
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ctsIOTask new_task(ctsIOTask::IOAction _action, unsigned long _max_transfer);

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Private method which must be implemented by the derived interface
        ///
        /// ctsIOTask next_task()
        /// - must return a ctsIOTask returned from tracked_task or untracked_task
        ///
        /// ctsIOPatternStatus completed_task(const ctsIOTask&, unsigned long _current_transfer) throw()
        /// - a notification to the derived class over what task completed
        ///   - ctsIOTask argument: the ctsIOTask which it previously returned from next_task()
        ///   - unsigned long argument:  the # of bytes actually transferred
        /// - cannot throw [if it fails, it must RaiseException to debug]
        /// - returns a ctsIOPatternStatus back to the base class to indicate errors
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        virtual ctsIOTask next_task() = 0;
        virtual ctsIOPatternStatus completed_task(const ctsIOTask&, unsigned long _current_transfer) throw() = 0;

        // CS memory guard for data within this object
        CRITICAL_SECTION cs;
        // recv buffers to return to the caller
        // - tracking sending buffers separate from receiving buffers
        //   since sending buffers will have a test pattern written to it (thus send buffers can be static)
        // For supporting multiple recv calls, allocating a larger buffer to contain all recv requests
        // - as well as a vector to contain the multiple ptrs to each buffer
        // When needing to dynamically allocate, containing a vector to hold the bytes
        std::vector<char*> recv_buffer_free_list;
        std::vector<char> recv_buffer_container;
        // optional callback for protocols which need to communicate OOB to the IO function
        std::function<void(const ctsIOTask&)> callback;

        // tracking current bytes 
        ctsUnsignedLongLong current_transfer;
        // need to know when to stop
        ctsUnsignedLongLong max_transfer;
        // need to know buffer size for each transfer
        ctsSizeT buffer_size;
        // need to know in-flight bytes
        ctsSizeT inflight_bytes;
        // need to track the current offset into the buffer pattern
        // these are separate as we could have both sends and receive operations on the same connection
        ctsSizeT send_pattern_offset;
        ctsSizeT recv_pattern_offset;

        // RIO buffer Id
        RIO_BUFFERID recv_rio_bufferid;
        // track the status
        ctsIOPatternStatus protocol_status;
        // tracking time information for scheduling IO at time offsets
        ctsUnsignedLongLong bytes_sending_per_quantum;
        ctsUnsignedLongLong bytes_sending_this_quantum;
        ctsUnsignedLongLong quantum_start_time_ms;

    protected:
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// protected constructor
        ///
        /// - only applicable for the derived types to indicate if will need send or recv buffers
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ctsIOPattern(unsigned long _recv_count);

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// tracked_task(ctsIOTask::IOAction, unsigned long _max_transfer)
        /// untracked_task(ctsIOTask::IOAction, unsigned long _max_transfer)
        ///
        /// - returns a ctsIOTask for the next transfer based on the IOAction
        /// - the returned buffer can be contained to maximum size with _max_transfer
        ///
        /// tracked_tasks will count that IO towards the max_transfer
        /// untracked_tasks will *not* count the IO towards the max_transfer
        /// untracked_tasks will *not* have their buffers validated on complete_io
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ctsIOTask tracked_task(ctsIOTask::IOAction, unsigned long _max_transfer = 0);
        ctsIOTask untracked_task(ctsIOTask::IOAction, unsigned long _max_transfer = 0);

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Exposing to the derived class the # of bytes to be transferred as tracked in the base class
        /// Make it possible for the derived type to also override the total transfer 
        ///  - to meet its requirements (e.g. must be an even total # for balanced send & recv's)
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ctsUnsignedLongLong get_total_transfer() const throw();
        void set_total_transfer(const ctsUnsignedLongLong& _new_total) throw();

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Expose to the derived class the option to verify the buffers in their ctsIOTask which
        /// - they created through untracked_task
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        bool verify_buffer(const ctsIOTask& _original_task, unsigned long _transferred_bytes);

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Expose to the derived class the option to have a ctsIOTask sent OOB to the IO caller
        /// - not that this will take the same internal lock as the publicly exposed functions
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        void send_callback(const ctsIOTask& _task) const throw()
        {
            if (this->callback) {
                callback(_task);
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
        _Acquires_lock_(cs)
        void base_lock() throw()
        {
            ::EnterCriticalSection(&this->cs);
        }
        _Releases_lock_(cs)
        void base_unlock() throw()
        {
            ::LeaveCriticalSection(&this->cs);
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
    class ctsIOPatternImpl : public ctsIOPattern {
    public:
        ctsIOPatternImpl(unsigned long _recv_count) : ctsIOPattern(_recv_count)
        {
        }
        virtual ~ctsIOPatternImpl() throw()
        {
            // guarantee that end_pattern has been called at least once
            end_pattern();
        }
        ///
        /// Printing of results is controlled by the applicable statistics type
        ///
        void print_io_results(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error) const
        {
            ctsConfig::PrintConnectionResults(
                _local_addr,
                _remote_addr,
                _error,
                stats);
        }
        ///
        /// Exposed to the caller to control when to set the end_time
        /// - if the end_time was previously zero, will also update historic stats
        ///
        void end_pattern() throw()
        {
            long long prior_end_time = stats.end_time.set_conditionally(ctl::ctTimer::snap_qpc_msec(), 0LL);
            if (0LL == prior_end_time) {
                ctsConfig::UpdateGlobalStats(stats);
            }
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
    class ctsIOPatternPull : public ctsIOPatternImpl<ctsTcpStatistics> {
    public:
        ctsIOPatternPull();
        ~ctsIOPatternPull() throw();

        // required virtual functions
        ctsIOTask next_task();
        ctsIOPatternStatus completed_task(const ctsIOTask& _task, unsigned long _current_transfer) throw();

    private:
        ctsUnsignedLong io_needed;
        bool sending;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  - Push Pattern
    ///    -- TCP-only
    ///    -- The client pushes data in 'segments' (the size of which is defined in the class)
    ///    -- The server pulls data in 'segments'
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsIOPatternPush : public ctsIOPatternImpl<ctsTcpStatistics> {
    public:
        ctsIOPatternPush();
        ~ctsIOPatternPush() throw();

        // required virtual functions
        ctsIOTask next_task();
        ctsIOPatternStatus completed_task(const ctsIOTask& _task, unsigned long _current_transfer) throw();

    private:
        ctsUnsignedLong io_needed;
        bool sending;
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
    class ctsIOPatternPushPull : public ctsIOPatternImpl<ctsTcpStatistics> {
    public:
        ctsIOPatternPushPull();
        ~ctsIOPatternPushPull() throw();

        ctsIOTask next_task();
        ctsIOPatternStatus completed_task(const ctsIOTask& _task, unsigned long _current_transfer) throw();

    private:
        const unsigned long push_segment_size;
        const unsigned long pull_segment_size;

        const bool listening;

        ctsUnsignedLong intra_segment_transfer;
        bool io_needed;
        bool sending;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  - Duplex Pattern
    ///    -- TCP-only
    ///    -- The client both pushes and pulls data concurrently
    ///    -- The server both pushes and pulls data concurrently
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsIOPatternDuplex : public ctsIOPatternImpl<ctsTcpStatistics> {
    public:
        ctsIOPatternDuplex();
        ~ctsIOPatternDuplex() throw();

        // required virtual functions
        ctsIOTask next_task();
        ctsIOPatternStatus completed_task(const ctsIOTask& _task, unsigned long _current_transfer) throw();

    private:
        // need to know when to stop sending
        ctsUnsignedLongLong remaining_send_bytes;
        ctsUnsignedLongLong remaining_recv_bytes;
        ctsUnsignedLong send_needed;
        ctsUnsignedLong recv_needed;
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
    class ctsIOPatternMediaStreamServer : public ctsIOPatternImpl<ctsUdpStatistics> {
    public:
        ctsIOPatternMediaStreamServer();
        ~ctsIOPatternMediaStreamServer() throw();

        // required virtual functions
        ctsIOTask next_task();
        ctsIOPatternStatus completed_task(const ctsIOTask& _task, unsigned long _current_transfer) throw();

    private:
        ctsUnsignedLong frame_size_bytes;
        ctsUnsignedLong current_frame_requested;
        ctsUnsignedLong current_frame_completed;
        ctsUnsignedLong frame_rate_fps;
        ctsUnsignedLong current_frame;
        ctsSignedLongLong base_time_milliseconds;
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
    class ctsIOPatternMediaStreamClient : public ctsIOPatternImpl<ctsUdpStatistics> {
    public:
        ctsIOPatternMediaStreamClient();
        ~ctsIOPatternMediaStreamClient() throw();

        // required virtual functions
        ctsIOTask next_task();
        ctsIOPatternStatus completed_task(const ctsIOTask& _task, unsigned long _current_transfer) throw();

    private:
        struct FrameEntry {
            FrameEntry() : sequence_number(0LL), retried(false), received(false)
            {
            }

            ctsSignedLongLong sequence_number;
            ctsSignedLongLong sender_qpc;
            ctsSignedLongLong sender_qpf;
            ctsSignedLongLong receiver_qpc;
            ctsSignedLongLong receiver_qpf;
            ctsUnsignedLong received;
            bool retried;
        };

        PTP_TIMER renderer_timer;
        PTP_TIMER start_timer;

        const unsigned long frame_size_bytes;
        const unsigned long final_frame;

        unsigned long initial_buffer_frames;

        ctsUnsignedLong timer_wheel_offset_frames;
        ctsUnsignedLong recv_needed;

        ctsSignedLongLong base_time_milliseconds;
        ctsSignedLongLong tracking_resend_sequence_number;

        const double frame_rate_ms_per_frame;

        // these must be protected by the base class cs
        // - the base lock is always taken before our virtual functions are called
        // - so this is most important to know in our timer callback

        // member variables that require the base lock
        _Requires_lock_held_(cs)
        std::vector<FrameEntry> frame_entries;

        _Requires_lock_held_(cs)
        std::vector<FrameEntry>::iterator head_entry;

        _Requires_lock_held_(cs)
        std::vector<std::unique_ptr<std::string>> send_buffers;

        // member functions which require the base lock
        _Requires_lock_held_(cs)
        std::vector<FrameEntry>::iterator find_sequence_number(long long _seq_number) throw();

        _Requires_lock_held_(cs)
        bool received_buffered_frames() throw();

        _Requires_lock_held_(cs)
        void set_next_timer() throw();

        _Requires_lock_held_(cs)
        void set_next_start_timer() throw();

        _Requires_lock_held_(cs)
        void render_frame() throw();

        /// The "Renderer" processes frames at the specified frame rate
        static
        VOID CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER);
        /// Callback to track when the server has actually started sending
        static
        VOID CALLBACK StartCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER);
    };

} //namespace
