/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/


// cpp headers
#include <vector>
// os headers
#include <Windows.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include <ctException.hpp>
#include <ctTimer.hpp>
// project headers
#include "ctsIOPattern.h"
#include "ctsStatistics.hpp"
#include "ctsConfig.h"
#include "ctsIOTask.hpp"
#include "ctsSafeInt.hpp"
#include "ctsMediaStreamProtocol.hpp"

using namespace ctl;
using std::vector;

namespace ctsTraffic {
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///     - ctsIOPatternMediaStream (Client) Pattern
    ///    -- UDP-only
    ///    -- The server sends data at a specified rate
    ///    -- The client receives data continuously
    ///       After a 'buffer period' of data has been received,
    ///       The client starts as timer to 'process' a time-slice of data
    ///    -- e.g. FrameRate = 60 frames/sec
    ///            FrameSize = 4096 byte frames
    ///            BufferDepth = 81920 bytes (2 seconds)
    ///
    ///   -- The client must maintain a vector of up to ExtraBufferDepthFactor * the buffer depth requested
    ///      - after the initial BufferDepth is received, 
    ///        it will start its timer to access the next frame's data
    ///
    ///   -- The client is only using untracked_task requests from the base
    ///      since the correctness and lifetime of the session is only known from this instance
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOPatternMediaStreamClient::ctsIOPatternMediaStreamClient() :
        ctsIOPatternStatistics(ctsConfig::Settings->PrePostRecvs),
        frame_rate_ms_per_frame(1000.0 / static_cast<unsigned long>(ctsConfig::GetMediaStream().FramesPerSecond))
    {
        // if the entire session fits in the inital buffer, update accordingly
        if (final_frame < initial_buffer_frames) {
            initial_buffer_frames = final_frame;
        }
        timer_wheel_offset_frames = initial_buffer_frames;

        const static long ExtraBufferDepthFactor = 2;
        // queue_size is intentionally a signed long: will catch overflows
        const ctsSignedLong queue_size = ExtraBufferDepthFactor * initial_buffer_frames;
        if (queue_size < ExtraBufferDepthFactor) {
            throw ctException(
                ERROR_INVALID_DATA,
                L"BufferDepth & FrameSize don't allow for enough buffered stream",
                L"ctsIOPatternMediaStreamClient",
                false);
        }

        PrintDebugInfo(L"\t\tctsIOPatternMediaStreamClient - queue size for this new connection is %d\n", static_cast<long>(queue_size));
        PrintDebugInfo(L"\t\tctsIOPatternMediaStreamClient - frame rate in milliseconds per frame : %f\n", frame_rate_ms_per_frame);

        frame_entries.resize(queue_size);
        head_entry = frame_entries.begin();

        // pre-populate the queue of frames with the initial seq numbers
        ctsSignedLong last_used_sequence_number = 1;
        for (auto& entry : frame_entries) {
            entry.sequence_number = last_used_sequence_number;
            ++last_used_sequence_number;
        }

        // after creating, refer to the timers under the lock
        renderer_timer = ::CreateThreadpoolTimer(TimerCallback, this, nullptr);
        if (!renderer_timer) {
            throw ctException(::GetLastError(), L"CreateThreadpoolTimer", L"ctsIOPatternMediaStreamClient", false);
        }
        auto deleteTimerCallbackOnError = wil::scope_exit([&]()
        {
            ::SetThreadpoolTimer(renderer_timer, nullptr, 0, 0);
            ::WaitForThreadpoolTimerCallbacks(renderer_timer, FALSE);
            ::CloseThreadpoolTimer(renderer_timer);
        });

        start_timer = ::CreateThreadpoolTimer(StartCallback, this, nullptr);
        if (!start_timer) {
            throw ctException(::GetLastError(), L"CreateThreadpoolTimer", L"ctsIOPatternMediaStreamClient", false);
        }
        // no errors, dismiss the scope guard
        deleteTimerCallbackOnError.release();
    }
    
    ctsIOPatternMediaStreamClient::~ctsIOPatternMediaStreamClient() noexcept
    {
        PTP_TIMER original_timer = nullptr;
        auto lock = this->base_lock();
        // ReSharper disable once CppLocalVariableMayBeConst
        original_timer = this->renderer_timer;
        this->renderer_timer = nullptr;
        lock.reset();
        // stop both timers
        ::SetThreadpoolTimer(this->start_timer, nullptr, 0, 0);
        ::WaitForThreadpoolTimerCallbacks(this->start_timer, FALSE);
        ::CloseThreadpoolTimer(this->start_timer);

        ::SetThreadpoolTimer(original_timer, nullptr, 0, 0);
        ::WaitForThreadpoolTimerCallbacks(original_timer, FALSE);
        ::CloseThreadpoolTimer(original_timer);
    }

    ctsIOTask ctsIOPatternMediaStreamClient::next_task() noexcept
    {
        if (0 == this->base_time_milliseconds) {
            // initiate the timers the first time the object is used
            this->base_time_milliseconds = ctTimer::snap_qpc_as_msec();
            this->set_next_start_timer();
            (void)this->set_next_timer(true);
        }

        // defaulting to an empty task (do nothing)
        ctsIOTask return_task;
        if (this->recv_needed > 0) {
            // don't try posting more than UdpDatagramMaximumSizeBytes at a time
            unsigned long max_size_buffer;
            if (this->frame_size_bytes > UdpDatagramMaximumSizeBytes) {
                max_size_buffer = UdpDatagramMaximumSizeBytes;
            } else {
                max_size_buffer = this->frame_size_bytes;
            }

            return_task = this->untracked_task(IOTaskAction::Recv, max_size_buffer);
            // always write in a zero for the seq number to initialize the buffer
            *(reinterpret_cast<long long*>(return_task.buffer)) = 0LL;
            --this->recv_needed;
        }
        return return_task;
    }

    ctsIOPatternProtocolError ctsIOPatternMediaStreamClient::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) noexcept
    {
        LARGE_INTEGER qpc;
        ::QueryPerformanceCounter(&qpc);

        if (_task.ioAction == IOTaskAction::Abort) {
            // the stream should now be done
            ctFatalCondition(
                !this->finished_stream,
                L"ctsIOPatternMediaStreamClient (dt %p ctsTraffic!ctsTraffic::ctsIOPatternMediaStreamClient) processed an Abort before the stream was finished", this);
            return ctsIOPatternProtocolError::SuccessfullyCompleted;
        }

        if (_task.ioAction == IOTaskAction::Recv) {
            if (0 == _completed_bytes) {
                if (this->finished_stream) {
                    // the final WSARecvFrom can complete with a zero-byte recv on loopback after the sender closes
                    // TODO: verify on non-loopback
                    return ctsIOPatternProtocolError::NoError;
                } else {
                    ctsConfig::PrintErrorInfo(L"ctsIOPatternMediaStreamClient received a zero-byte datagram");
                    return ctsIOPatternProtocolError::TooFewBytes;
                }
            }

            if (!ctsMediaStreamMessage::ValidateBufferLengthFromTask(_task, _completed_bytes)) {
                ctsConfig::PrintErrorInfo(L"MediaStreamClient received an invalid datagram trying to parse the protocol header");
                return ctsIOPatternProtocolError::TooFewBytes;
            }

            if (ctsMediaStreamMessage::GetProtocolHeaderFromTask(_task) == UdpDatagramProtocolHeaderFlagId) {
                // save off the connection ID when we receive it
                ctsMediaStreamMessage::SetConnectionIdFromTask(this->connection_id(), _task);
                // since a recv completed, will need to request another
                ++this->recv_needed;
                return ctsIOPatternProtocolError::NoError;
            }

            // validate the buffer contents
            ctsIOTask validation_task(_task);
            validation_task.buffer_offset = UdpDatagramDataHeaderLength; // skip the UdpDatagramDataHeaderLength since we use them for our own stuff
            validation_task.buffer_length -= UdpDatagramDataHeaderLength;
            if (!this->verify_buffer(validation_task, _completed_bytes - UdpDatagramDataHeaderLength)) {
                // exit early if the buffers don't match
                return ctsIOPatternProtocolError::CorruptedBytes;
            }

            // track the # of *bits* received
            ctsConfig::Settings->UdpStatusDetails.bits_received.add(_completed_bytes * 8);
            this->stats.bits_received.add(_completed_bytes * 8);

            const long long received_seq_number = ctsMediaStreamMessage::GetSequenceNumberFromTask(_task);
            if (received_seq_number > this->final_frame) {
                ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                this->stats.error_frames.increment();

                PrintDebugInfo(
                    L"\t\tctsIOPatternMediaStreamClient recevieved **an unknown** seq number (%lld) (outside the final frame %lu)\n",
                    received_seq_number,
                    this->final_frame);
            } else {
                //
                // search our circular queue (starting at the head_entry)
                // for the seq number we just received, and if found, tag as received
                //
                const auto found_slot = this->find_sequence_number(received_seq_number);
                if (found_slot != this->frame_entries.end()) {
                    if (found_slot->received != this->frame_size_bytes) {
                        const long long buffered_qpc = *reinterpret_cast<long long*>(_task.buffer + 8);
                        const long long buffered_qpf = *reinterpret_cast<long long*>(_task.buffer + 16);

                        // always overwrite qpc & qpf values with the latest datagram details
                        found_slot->sender_qpc = buffered_qpc;
                        found_slot->sender_qpf = buffered_qpf;
                        found_slot->receiver_qpc = qpc.QuadPart;
                        found_slot->receiver_qpf = ctTimer::snap_qpf();
                        found_slot->received += _completed_bytes;

                        PrintDebugInfo(
                            L"\t\tctsIOPatternMediaStreamClient received seq number %lld (%lu bytes)\n",
                            static_cast<long long>(found_slot->sequence_number),
                            static_cast<unsigned long>(found_slot->received));

                        // stop the timer once we receive the last frame
                        // - it's not perfect (e.g. might have received them out of order)
                        // - but it will be very close for tracking the total bits/sec
                        if (static_cast<unsigned long>(received_seq_number) == this->final_frame) {
                            this->end_stats();
                        }

                    } else {
                        ctsConfig::Settings->UdpStatusDetails.duplicate_frames.increment();
                        this->stats.duplicate_frames.increment();

                        PrintDebugInfo(
                            L"\t\tctsIOPatternMediaStreamClient received **a duplicate frame** for seq number (%lld)\n",
                            received_seq_number);
                    }

                } else {
                    // didn't find a slot for the received seq. number
                    ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                    this->stats.error_frames.increment();

                    if (received_seq_number < this->head_entry->sequence_number) {
                        PrintDebugInfo(
                            L"\t\tctsIOPatternMediaStreamClient received **a stale** seq number (%lld) - current seq number (%lld)\n",
                            received_seq_number,
                            static_cast<long long>(this->head_entry->sequence_number));
                    } else {
                        PrintDebugInfo(
                            L"\t\tctsIOPatternMediaStreamClient recevieved **a future** seq number (%lld) - head of queue (%lld) tail of queue (%lld)\n",
                            received_seq_number,
                            static_cast<long long>(this->head_entry->sequence_number),
                            static_cast<long long>(this->head_entry->sequence_number + this->frame_entries.size() - 1));
                    }
                }
            }

            // since a recv completed successfully, will need to request another
            ++this->recv_needed;
        }
        // else this is the completion of the SEND request

        return ctsIOPatternProtocolError::NoError;
    }

    ///
    /// Returns an iterator within frame_entries pointing to the FrameEntry
    ///   matching the specified sequence number.
    /// If the sequence number was not found, will return end(frame_entries)
    ///
    _Requires_lock_held_(cs)
    vector<ctsConfig::JitterFrameEntry>::iterator ctsIOPatternMediaStreamClient::find_sequence_number(long long _seq_number) noexcept
    {
        const ctsSignedLongLong head_sequence_number = this->head_entry->sequence_number;
        const ctsSignedLongLong tail_sequence_number = head_sequence_number + this->frame_entries.size() - 1;
        const ctsSignedLongLong vector_end_sequence_number = this->frame_entries.rbegin()->sequence_number;

        if (_seq_number > tail_sequence_number || _seq_number < head_sequence_number) {
            // sequence number was out of range of our circular queue
            // - return end(frame_entries) to indicate it could not be found
            return end(this->frame_entries);
        }

        if (_seq_number <= vector_end_sequence_number) {
            // offset just from the head since it hasn't wrapped around the end
            const auto offset = static_cast<size_t>(_seq_number - head_sequence_number);
            return this->head_entry + offset;
        }
        // offset from the beginning since it wrapped around from the end
        const auto offset = static_cast<size_t>(_seq_number - vector_end_sequence_number - 1LL);
        return this->frame_entries.begin() + offset;
    }

    _Requires_lock_held_(cs)
    bool ctsIOPatternMediaStreamClient::received_buffered_frames() noexcept
    {
        if (this->frame_entries[0].sequence_number > 1) {
            // we've already received enough datagrams to fill one buffer
            return true;
        }
        if (this->head_entry != this->frame_entries.begin()) {
            // we've already moved the head entry after processing a frame
            return true;
        }

        for (const auto& udp_frame : this->frame_entries) {
            if (udp_frame.received > 0UL) {
                return true;
            }
        }
        return false;
    }

    _Requires_lock_held_(cs)
    bool ctsIOPatternMediaStreamClient::set_next_timer(bool initial_timer) const noexcept
    {
        bool timer_scheduled = false;
        // only schedule the next timer instance if the d'tor hasn't indicated it's wanting to exit
        if (this->renderer_timer != nullptr) {
            // calculate when that time should be relative to base_time_milliseconds 
            // (base_time_milliseconds is the start milliseconds from ctTimer::snap_qpc_msec())
            long long timer_offset = this->base_time_milliseconds;
            // offset to the time when we need to check the next frame
            // - we'll also render a frame at the same time if the initial buffer is full
            timer_offset += static_cast<long long>(static_cast<double>(this->timer_wheel_offset_frames) * this->frame_rate_ms_per_frame);
            // subtract out the current time to get the delta # of milliseconds
            timer_offset -= ctTimer::snap_qpc_as_msec();
            // only set the timer if we have time to wait
            if (initial_timer || timer_offset > 2) {
                // convert to filetime from milliseconds
                // - make a 'relative' for SetThreadpoolTimer
                FILETIME file_time(ctTimer::convert_msec_relative_filetime(timer_offset));
                // TP Timer APIs work off of the UTC time
                ::SetThreadpoolTimer(this->renderer_timer, &file_time, 0, 0);
                timer_scheduled = true;
            }
        }

        return timer_scheduled;
    }

    _Requires_lock_held_(cs)
    void ctsIOPatternMediaStreamClient::set_next_start_timer() const noexcept
    {
        if (this->start_timer != nullptr) {
            // convert to filetime from milliseconds
            // - make a 'relative' for SetThreadpoolTimer
            FILETIME file_time(ctTimer::convert_msec_relative_filetime(static_cast<long long>(frame_rate_ms_per_frame) + 500LL));
            // TP Timer APIs work off of the UTC time
            ::SetThreadpoolTimer(this->start_timer, &file_time, 0, 0);
        }
    }

    // "render the current frame"
    // - update the current frame as "read" and move the head to the next frame
    _Requires_lock_held_(cs)
    void ctsIOPatternMediaStreamClient::render_frame() noexcept
    {
        if (this->head_entry->received == this->frame_size_bytes) {
            ctsConfig::Settings->UdpStatusDetails.successful_frames.increment();
            this->stats.successful_frames.increment();

            PrintDebugInfo(
                L"\t\tctsIOPatternMediaStreamClient rendered frame %lld\n",
                static_cast<long long>(this->head_entry->sequence_number));

            // Directly write this status update if jitter is enabled
            ctsConfig::PrintJitterUpdate(*this->head_entry, this->previous_frame, this->first_frame);

            // if this is the first frame, capture it
            if (this->first_frame.receiver_qpc == 0) {
                this->first_frame = *this->head_entry;
            }
            // always keep the most recently received frame for jitter
            this->previous_frame = *this->head_entry;

        } else {
            ctsConfig::Settings->UdpStatusDetails.dropped_frames.increment();
            this->stats.dropped_frames.increment();

            PrintDebugInfo(
                L"\t\tctsIOPatternMediaStreamClient **dropped** frame %lld\n",
                static_cast<long long>(this->head_entry->sequence_number));

            // track the dropped frame
            // indicate zero's for the other values so we won't calculate jitter for a dropped datagram
            ctsConfig::JitterFrameEntry droppedFrame;
            droppedFrame.sequence_number = this->head_entry->sequence_number;
            ctsConfig::PrintJitterUpdate(droppedFrame, ctsConfig::JitterFrameEntry(), ctsConfig::JitterFrameEntry());
        }

        // update the current sequence number so it's now the "end" sequence number of the queue (the new max value)
        this->head_entry->sequence_number = this->head_entry->sequence_number + this->frame_entries.size();
        this->head_entry->received = 0;

        // move the head entry to the next sequence number
        ++this->head_entry;
        if (this->head_entry == this->frame_entries.end()) {
            this->head_entry = this->frame_entries.begin();
        }
    }

    VOID CALLBACK ctsIOPatternMediaStreamClient::StartCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER) noexcept
    {
        static const char StartBuffer[] = "START";

        ctsIOPatternMediaStreamClient* this_ptr = static_cast<ctsIOPatternMediaStreamClient*>(_context);

        // take the base lock before touching any internal members
        const auto lock = this_ptr->base_lock();

        if (this_ptr->finished_stream) {
            return;
        }

        if (!this_ptr->received_buffered_frames()) {
            // send another start message
            PrintDebugInfo(L"\t\tctsIOPatternMediaStreamClient re-requesting START\n");

            ctsIOTask resend_task;
            resend_task.ioAction = IOTaskAction::Send;
            resend_task.track_io = false;
            resend_task.buffer = const_cast<char*>(StartBuffer);
            resend_task.buffer_offset = 0;
            resend_task.buffer_length = static_cast<unsigned long>(::strlen(StartBuffer));
            resend_task.buffer_type = ctsIOTask::BufferType::Static; // this is our own buffer: the base class should not mess with it

            this_ptr->set_next_start_timer();
            this_ptr->send_callback(resend_task);
        }
        // else, don't schedule this timer anymore
    }

    VOID CALLBACK ctsIOPatternMediaStreamClient::TimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER) noexcept
    {
        auto* this_ptr = static_cast<ctsIOPatternMediaStreamClient*>(_context);

        // process frames until the timer is scheduled in the future to process more frames
        bool timer_scheduled = false;
        while (!timer_scheduled) {
            // take the base lock before touching any internal members
            const auto lock = this_ptr->base_lock();

            if (this_ptr->finished_stream) {
                return;
            }

            ++this_ptr->timer_wheel_offset_frames;

            bool fatal_aborted = false;
            if (this_ptr->timer_wheel_offset_frames >= this_ptr->initial_buffer_frames &&
                this_ptr->head_entry->sequence_number <= this_ptr->final_frame) {
                // if we haven't yet received *anything* from the server, abort this connection
                if (!this_ptr->received_buffered_frames()) {
                    ctsConfig::PrintErrorInfo(L"ctsIOPatternMediaStreamClient - issuing a FATALABORT to close the connection - have received nothing from the server");

                    // indicate all frames were dropped
                    ctsConfig::Settings->UdpStatusDetails.dropped_frames.add(this_ptr->final_frame);
                    this_ptr->stats.dropped_frames.add(this_ptr->final_frame);

                    this_ptr->finished_stream = true;
                    ctsIOTask abort_task;
                    abort_task.ioAction = IOTaskAction::FatalAbort;
                    this_ptr->send_callback(abort_task);
                    fatal_aborted = true;

                } else {
                    // if the initial buffer has already been filled, "render" the frame
                    this_ptr->render_frame();
                }
            }

            if (!fatal_aborted) {
                // wait for the precise number of milliseconds for the next frame
                if (this_ptr->head_entry->sequence_number <= this_ptr->final_frame) {
                    timer_scheduled = this_ptr->set_next_timer(false);

                } else {
                    this_ptr->finished_stream = true;
                    ctsIOTask abort_task;
                    abort_task.ioAction = IOTaskAction::Abort;
                    this_ptr->send_callback(abort_task);
                    PrintDebugInfo(L"\t\tctsIOPatternMediaStreamClient - issuing an ABORT to cleanly close the connection\n");
                }
            }
        }
    }
}