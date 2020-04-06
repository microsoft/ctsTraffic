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
        m_frameRateMsPerFrame(1000.0 / static_cast<unsigned long>(ctsConfig::GetMediaStream().FramesPerSecond))
    {
        // if the entire session fits in the inital buffer, update accordingly
        if (m_finalFrame < m_initialBufferFrames) {
            m_initialBufferFrames = m_finalFrame;
        }
        m_timerWheelOffsetFrames = m_initialBufferFrames;

        constexpr long c_ExtraBufferDepthFactor = 2;
        // queue_size is intentionally a signed long: will catch overflows
        const ctsSignedLong queue_size = c_ExtraBufferDepthFactor * m_initialBufferFrames;
        if (queue_size < c_ExtraBufferDepthFactor) {
            throw ctException(
                ERROR_INVALID_DATA,
                L"BufferDepth & FrameSize don't allow for enough buffered stream",
                L"ctsIOPatternMediaStreamClient",
                false);
        }

        PrintDebugInfo(L"\t\tctsIOPatternMediaStreamClient - queue size for this new connection is %d\n", static_cast<long>(queue_size));
        PrintDebugInfo(L"\t\tctsIOPatternMediaStreamClient - frame rate in milliseconds per frame : %f\n", m_frameRateMsPerFrame);

        m_frameEntries.resize(queue_size);
        m_headEntry = m_frameEntries.begin();

        // pre-populate the queue of frames with the initial seq numbers
        ctsSignedLong last_used_sequence_number = 1;
        for (auto& entry : m_frameEntries) {
            entry.sequence_number = last_used_sequence_number;
            ++last_used_sequence_number;
        }

        // after creating, refer to the timers under the lock
        m_rendererTimer = CreateThreadpoolTimer(TimerCallback, this, nullptr);
        if (!m_rendererTimer) {
            throw ctException(GetLastError(), L"CreateThreadpoolTimer", L"ctsIOPatternMediaStreamClient", false);
        }
        auto deleteTimerCallbackOnError = wil::scope_exit([&]() noexcept
        {
            SetThreadpoolTimer(m_rendererTimer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(m_rendererTimer, FALSE);
            CloseThreadpoolTimer(m_rendererTimer);
        });

        m_startTimer = CreateThreadpoolTimer(StartCallback, this, nullptr);
        if (!m_startTimer) {
            throw ctException(GetLastError(), L"CreateThreadpoolTimer", L"ctsIOPatternMediaStreamClient", false);
        }
        // no errors, dismiss the scope guard
        deleteTimerCallbackOnError.release();
    }
    
    ctsIOPatternMediaStreamClient::~ctsIOPatternMediaStreamClient() noexcept
    {
        PTP_TIMER original_timer = nullptr;
        auto lock = this->base_lock();
        // ReSharper disable once CppLocalVariableMayBeConst
        original_timer = m_rendererTimer;
        m_rendererTimer = nullptr;
        lock.reset();
        // stop both timers
        SetThreadpoolTimer(m_startTimer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(m_startTimer, FALSE);
        CloseThreadpoolTimer(m_startTimer);

        SetThreadpoolTimer(original_timer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(original_timer, FALSE);
        CloseThreadpoolTimer(original_timer);
    }

    ctsIOTask ctsIOPatternMediaStreamClient::next_task() noexcept
    {
        if (0 == m_baseTimeMilliseconds) {
            // initiate the timers the first time the object is used
            m_baseTimeMilliseconds = ctTimer::ctSnapQpcInMillis();
            this->set_next_start_timer();
            (void)this->set_next_timer(true);
        }

        // defaulting to an empty task (do nothing)
        ctsIOTask return_task;
        if (m_recvNeeded > 0) {
            // don't try posting more than UdpDatagramMaximumSizeBytes at a time
            unsigned long max_size_buffer;
            if (m_frameSizeBytes > UdpDatagramMaximumSizeBytes) {
                max_size_buffer = UdpDatagramMaximumSizeBytes;
            } else {
                max_size_buffer = m_frameSizeBytes;
            }

            return_task = this->untracked_task(IOTaskAction::Recv, max_size_buffer);
            // always write in a zero for the seq number to initialize the buffer
            *(reinterpret_cast<long long*>(return_task.buffer)) = 0LL;
            --m_recvNeeded;
        }
        return return_task;
    }

    ctsIOPatternProtocolError ctsIOPatternMediaStreamClient::completed_task(const ctsIOTask& task, unsigned long bytes_received) noexcept
    {
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);

        if (task.ioAction == IOTaskAction::Abort) {
            // the stream should now be done
            ctFatalCondition(
                !m_finishedStream,
                L"ctsIOPatternMediaStreamClient (dt %p ctsTraffic!ctsTraffic::ctsIOPatternMediaStreamClient) processed an Abort before the stream was finished", this);
            return ctsIOPatternProtocolError::SuccessfullyCompleted;
        }

        if (task.ioAction == IOTaskAction::Recv) {
            if (0 == bytes_received) {
                if (m_finishedStream) {
                    // the final WSARecvFrom can complete with a zero-byte recv on loopback after the sender closes
                    // TODO: verify on non-loopback
                    return ctsIOPatternProtocolError::NoError;
                } else {
                    ctsConfig::PrintErrorInfo(L"ctsIOPatternMediaStreamClient received a zero-byte datagram");
                    return ctsIOPatternProtocolError::TooFewBytes;
                }
            }

            if (!ctsMediaStreamMessage::ValidateBufferLengthFromTask(task, bytes_received)) {
                ctsConfig::PrintErrorInfo(L"MediaStreamClient received an invalid datagram trying to parse the protocol header");
                return ctsIOPatternProtocolError::TooFewBytes;
            }

            if (ctsMediaStreamMessage::GetProtocolHeaderFromTask(task) == UdpDatagramProtocolHeaderFlagId) {
                // save off the connection ID when we receive it
                ctsMediaStreamMessage::SetConnectionIdFromTask(this->connection_id(), task);
                // since a recv completed, will need to request another
                ++m_recvNeeded;
                return ctsIOPatternProtocolError::NoError;
            }

            // validate the buffer contents
            ctsIOTask validation_task(task);
            validation_task.buffer_offset = UdpDatagramDataHeaderLength; // skip the UdpDatagramDataHeaderLength since we use them for our own stuff
            validation_task.buffer_length -= UdpDatagramDataHeaderLength;
            if (!VerifyBuffer(validation_task, bytes_received - UdpDatagramDataHeaderLength)) {
                // exit early if the buffers don't match
                return ctsIOPatternProtocolError::CorruptedBytes;
            }

            // track the # of *bits* received
            ctsConfig::Settings->UdpStatusDetails.bits_received.add(bytes_received * 8);
            this->stats.bits_received.add(bytes_received * 8);

            const long long received_seq_number = ctsMediaStreamMessage::GetSequenceNumberFromTask(task);
            if (received_seq_number > m_finalFrame) {
                ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                this->stats.error_frames.increment();

                PrintDebugInfo(
                    L"\t\tctsIOPatternMediaStreamClient recevieved **an unknown** seq number (%lld) (outside the final frame %lu)\n",
                    received_seq_number,
                    m_finalFrame);
            } else {
                //
                // search our circular queue (starting at the head_entry)
                // for the seq number we just received, and if found, tag as received
                //
                const auto found_slot = this->find_sequence_number(received_seq_number);
                if (found_slot != m_frameEntries.end()) {
                    const long long buffered_qpc = *reinterpret_cast<long long*>(task.buffer + 8);
                    const long long buffered_qpf = *reinterpret_cast<long long*>(task.buffer + 16);

                    // always overwrite qpc & qpf values with the latest datagram details
                    found_slot->sender_qpc = buffered_qpc;
                    found_slot->sender_qpf = buffered_qpf;
                    found_slot->receiver_qpc = qpc.QuadPart;
                    found_slot->receiver_qpf = ctTimer::ctSnapQpf();
                    found_slot->bytes_received += bytes_received;

                    PrintDebugInfo(
                        L"\t\tctsIOPatternMediaStreamClient received seq number %lld (%lu received-bytes, %lu frame-bytes)\n",
                        found_slot->sequence_number,
                        bytes_received,
                        found_slot->bytes_received
                        );

                    // stop the timer once we receive the last frame
                    // - it's not perfect (e.g. might have received them out of order)
                    // - but it will be very close for tracking the total bits/sec
                    if (static_cast<unsigned long>(received_seq_number) == m_finalFrame) {
                        this->end_stats();
                    }

                } else {
                    // didn't find a slot for the received seq. number
                    ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                    this->stats.error_frames.increment();

                    if (received_seq_number < m_headEntry->sequence_number) {
                        PrintDebugInfo(
                            L"\t\tctsIOPatternMediaStreamClient received **a stale** seq number (%lld) - current seq number (%lld)\n",
                            received_seq_number,
                            static_cast<long long>(m_headEntry->sequence_number));
                    } else {
                        PrintDebugInfo(
                            L"\t\tctsIOPatternMediaStreamClient recevieved **a future** seq number (%lld) - head of queue (%lld) tail of queue (%lld)\n",
                            received_seq_number,
                            static_cast<long long>(m_headEntry->sequence_number),
                            static_cast<long long>(m_headEntry->sequence_number + m_frameEntries.size() - 1));
                    }
                }
            }

            // since a recv completed successfully, will need to request another
            ++m_recvNeeded;
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
    vector<ctsConfig::JitterFrameEntry>::iterator ctsIOPatternMediaStreamClient::find_sequence_number(long long seq_number) noexcept
    {
        const ctsSignedLongLong head_sequence_number = m_headEntry->sequence_number;
        const ctsSignedLongLong tail_sequence_number = head_sequence_number + m_frameEntries.size() - 1;
        const ctsSignedLongLong vector_end_sequence_number = m_frameEntries.rbegin()->sequence_number;

        if (seq_number > tail_sequence_number || seq_number < head_sequence_number) {
            // sequence number was out of range of our circular queue
            // - return end(frame_entries) to indicate it could not be found
            return end(m_frameEntries);
        }

        if (seq_number <= vector_end_sequence_number) {
            // offset just from the head since it hasn't wrapped around the end
            const auto offset = static_cast<size_t>(seq_number - head_sequence_number);
            return m_headEntry + offset;
        }
        // offset from the beginning since it wrapped around from the end
        const auto offset = static_cast<size_t>(seq_number - vector_end_sequence_number - 1LL);
        return m_frameEntries.begin() + offset;
    }

    _Requires_lock_held_(cs)
    bool ctsIOPatternMediaStreamClient::received_buffered_frames() noexcept
    {
        if (m_frameEntries[0].sequence_number > 1) {
            // we've already received enough datagrams to fill one buffer
            return true;
        }
        if (m_headEntry != m_frameEntries.begin()) {
            // we've already moved the head entry after processing a frame
            return true;
        }

        for (const auto& udp_frame : m_frameEntries) {
            if (udp_frame.bytes_received > 0UL) {
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
        if (m_rendererTimer != nullptr) {
            // calculate when that time should be relative to base_time_milliseconds 
            // (base_time_milliseconds is the start milliseconds from ctTimer::snap_qpc_msec())
            long long timer_offset = m_baseTimeMilliseconds;
            // offset to the time when we need to check the next frame
            // - we'll also render a frame at the same time if the initial buffer is full
            timer_offset += static_cast<long long>(static_cast<double>(m_timerWheelOffsetFrames) * m_frameRateMsPerFrame);
            // subtract out the current time to get the delta # of milliseconds
            timer_offset -= ctTimer::ctSnapQpcInMillis();
            // only set the timer if we have time to wait
            if (initial_timer || timer_offset > 2) {
                // convert to filetime from milliseconds
                // - make a 'relative' for SetThreadpoolTimer
                FILETIME file_time(ctTimer::ctConvertMillisToRelativeFiletime(timer_offset));
                // TP Timer APIs work off of the UTC time
                SetThreadpoolTimer(m_rendererTimer, &file_time, 0, 0);
                timer_scheduled = true;
            }
        }

        return timer_scheduled;
    }

    _Requires_lock_held_(cs)
    void ctsIOPatternMediaStreamClient::set_next_start_timer() const noexcept
    {
        if (m_startTimer != nullptr) {
            // convert to filetime from milliseconds
            // - make a 'relative' for SetThreadpoolTimer
            FILETIME file_time(ctTimer::ctConvertMillisToRelativeFiletime(static_cast<long long>(m_frameRateMsPerFrame) + 500LL));
            // TP Timer APIs work off of the UTC time
            SetThreadpoolTimer(m_startTimer, &file_time, 0, 0);
        }
    }

    // "render the current frame"
    // - update the current frame as "read" and move the head to the next frame
    _Requires_lock_held_(cs)
    void ctsIOPatternMediaStreamClient::render_frame() noexcept
    {
        // estimating time in flight for this frame by determining how much time since the first send was just 'waiting' to send this frame
        // and subtracing that from how much time since the first receive - since time between receives should at least be time between sends
        if (m_headEntry->receiver_qpf != 0 && m_firstFrame.receiver_qpf != 0)
        {
            const double ms_since_first_receive =
                m_headEntry->receiver_qpc * 1000.0 / m_headEntry->receiver_qpf -
                m_firstFrame.receiver_qpc * 1000.0 / m_firstFrame.receiver_qpf;
            const double ms_since_first_send =
                (m_headEntry->sender_qpc * 1000.0 / m_headEntry->sender_qpf) -
                (m_firstFrame.sender_qpc * 1000.0 / m_firstFrame.sender_qpf);
            m_headEntry->estimated_time_in_flight_ms = ms_since_first_receive - ms_since_first_send;
        }

        if (m_headEntry->bytes_received == m_frameSizeBytes)
        {
            ctsConfig::Settings->UdpStatusDetails.successful_frames.increment();
            this->stats.successful_frames.increment();

            PrintDebugInfo(
                L"\t\tctsIOPatternMediaStreamClient rendered frame %lld\n",
                static_cast<long long>(m_headEntry->sequence_number));

            // Directly write this status update if jitter is enabled
            PrintJitterUpdate(*m_headEntry, m_previousFrame);

            // if this is the first frame, capture it
            if (m_firstFrame.receiver_qpc == 0)
            {
                m_firstFrame = *m_headEntry;
            }
            // always keep the most recently received frame for jitter
            m_previousFrame = *m_headEntry;

        }
        else if (m_headEntry->bytes_received < m_frameSizeBytes)
        {
            ctsConfig::Settings->UdpStatusDetails.dropped_frames.increment();
            this->stats.dropped_frames.increment();

            PrintDebugInfo(
                L"\t\tctsIOPatternMediaStreamClient **dropped** frame for seq number (%lld)\n",
                m_headEntry->sequence_number);

            // track the dropped frame
            // indicate zero's for the other values so we won't calculate jitter for a dropped datagram
            ctsConfig::JitterFrameEntry droppedFrame;
            droppedFrame.sequence_number = m_headEntry->sequence_number;
            PrintJitterUpdate(droppedFrame, ctsConfig::JitterFrameEntry());
        }
        else // m_headEntry->bytes_received > m_frameSizeBytes
        {
            ctsConfig::Settings->UdpStatusDetails.duplicate_frames.increment();
            this->stats.duplicate_frames.increment();

            PrintDebugInfo(
                L"\t\tctsIOPatternMediaStreamClient **a duplicate** frame for seq number (%lld)\n",
                m_headEntry->sequence_number);
        }

        // update the current sequence number so it's now the "end" sequence number of the queue (the new max value)
        m_headEntry->sequence_number = m_headEntry->sequence_number + m_frameEntries.size();
        m_headEntry->bytes_received = 0;

        // move the head entry to the next sequence number
        ++m_headEntry;
        if (m_headEntry == m_frameEntries.end())
        {
            m_headEntry = m_frameEntries.begin();
        }
    }

    VOID CALLBACK ctsIOPatternMediaStreamClient::StartCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID context, PTP_TIMER) noexcept
    {
        static const char c_StartBuffer[] = "START";

        auto this_ptr = static_cast<ctsIOPatternMediaStreamClient*>(context);
        // take the base lock before touching any internal members
        const auto lock = this_ptr->base_lock();

        if (this_ptr->m_finishedStream) {
            return;
        }

        if (!this_ptr->received_buffered_frames()) {
            // send another start message
            PrintDebugInfo(L"\t\tctsIOPatternMediaStreamClient re-requesting START\n");

            ctsIOTask resend_task;
            resend_task.ioAction = IOTaskAction::Send;
            resend_task.track_io = false;
            resend_task.buffer = const_cast<char*>(c_StartBuffer);
            resend_task.buffer_offset = 0;
            resend_task.buffer_length = static_cast<unsigned long>(strlen(c_StartBuffer));
            resend_task.buffer_type = ctsIOTask::BufferType::Static; // this is our own buffer: the base class should not mess with it

            this_ptr->set_next_start_timer();
            this_ptr->send_callback(resend_task);
        }
        // else, don't schedule this timer anymore
    }

    VOID CALLBACK ctsIOPatternMediaStreamClient::TimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID context, PTP_TIMER) noexcept
    {
        auto* this_ptr = static_cast<ctsIOPatternMediaStreamClient*>(context);

        // process frames until the timer is scheduled in the future to process more frames
        bool timer_scheduled = false;
        while (!timer_scheduled) {
            // take the base lock before touching any internal members
            const auto lock = this_ptr->base_lock();

            if (this_ptr->m_finishedStream) {
                return;
            }

            ++this_ptr->m_timerWheelOffsetFrames;

            bool fatal_aborted = false;
            if (this_ptr->m_timerWheelOffsetFrames >= this_ptr->m_initialBufferFrames &&
                this_ptr->m_headEntry->sequence_number <= this_ptr->m_finalFrame) {
                // if we haven't yet received *anything* from the server, abort this connection
                if (!this_ptr->received_buffered_frames()) {
                    ctsConfig::PrintErrorInfo(L"ctsIOPatternMediaStreamClient - issuing a FATALABORT to close the connection - have received nothing from the server");

                    // indicate all frames were dropped
                    ctsConfig::Settings->UdpStatusDetails.dropped_frames.add(this_ptr->m_finalFrame);
                    this_ptr->stats.dropped_frames.add(this_ptr->m_finalFrame);

                    this_ptr->m_finishedStream = true;
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
                if (this_ptr->m_headEntry->sequence_number <= this_ptr->m_finalFrame) {
                    timer_scheduled = this_ptr->set_next_timer(false);

                } else {
                    this_ptr->m_finishedStream = true;
                    ctsIOTask abort_task;
                    abort_task.ioAction = IOTaskAction::Abort;
                    this_ptr->send_callback(abort_task);
                    PrintDebugInfo(L"\t\tctsIOPatternMediaStreamClient - issuing an ABORT to cleanly close the connection\n");
                }
            }
        }
    }
}