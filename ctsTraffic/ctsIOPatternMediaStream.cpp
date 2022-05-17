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
// ctl headers
#include <ctTimer.hpp>
// project headers
#include "ctsIOPattern.h"
#include "ctsStatistics.hpp"
#include "ctsConfig.h"
#include "ctsIOTask.hpp"
#include "ctsMediaStreamProtocol.hpp"
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>

using namespace ctl;
using std::vector;

namespace ctsTraffic
{
// - ctsIOPatternMediaStream (Client) Pattern
//    -- UDP-only
//    -- The server sends data at a specified rate
//    -- The client receives data continuously
//       After a 'buffer period' of data has been received,
//       The client starts as timer to 'process' a time-slice of data
//    -- e.g. FrameRate = 60 frames/sec
//            FrameSize = 4096 byte frames
//            BufferDepth = 81920 bytes (2 seconds)
///
//   -- The client must maintain a vector of up to ExtraBufferDepthFactor * the buffer depth requested
//      - after the initial BufferDepth is received, 
//        it will start its timer to access the next frame's data
///
//   -- The client is only using untracked_task requests from the base
//      since the correctness and lifetime of the session is only known from this instance

ctsIoPatternMediaStreamClient::ctsIoPatternMediaStreamClient() :
    ctsIoPatternStatistics(ctsConfig::g_configSettings->PrePostRecvs),
    m_frameRateMsPerFrame(1000.0 / static_cast<uint32_t>(ctsConfig::GetMediaStream().FramesPerSecond))
{
    // if the entire session fits in the inital buffer, update accordingly
    if (m_finalFrame < m_initialBufferFrames)
    {
        m_initialBufferFrames = m_finalFrame;
    }
    m_timerWheelOffsetFrames = m_initialBufferFrames;

    constexpr long extraBufferDepthFactor = 2;
    // queue_size is intentionally a signed long: will catch overflows
    const auto queueSize = extraBufferDepthFactor * m_initialBufferFrames;
    if (queueSize < extraBufferDepthFactor)
    {
        THROW_WIN32_MSG(
            ERROR_INVALID_DATA,
            "BufferDepth & FrameSize don't allow for enough buffered stream");
    }

    PRINT_DEBUG_INFO(L"\t\tctsIOPatternMediaStreamClient - queue size for this new connection is %u\n", queueSize);
    PRINT_DEBUG_INFO(L"\t\tctsIOPatternMediaStreamClient - frame rate in milliseconds per frame : %f\n", m_frameRateMsPerFrame);

    m_frameEntries.resize(queueSize);
    m_headEntry = m_frameEntries.begin();

    // pre-populate the queue of frames with the initial seq numbers
    int64_t lastUsedSequenceNumber = 1;
    // ReSharper disable once CppUseStructuredBinding
    for (auto& entry : m_frameEntries)
    {
        entry.m_sequenceNumber = lastUsedSequenceNumber;
        ++lastUsedSequenceNumber;
    }

    // after creating, refer to the timers under the lock
    m_rendererTimer = CreateThreadpoolTimer(TimerCallback, this, nullptr);
    THROW_LAST_ERROR_IF(!m_rendererTimer);

    auto deleteTimerCallbackOnError = wil::scope_exit([&]() noexcept {
        SetThreadpoolTimer(m_rendererTimer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(m_rendererTimer, FALSE);
        CloseThreadpoolTimer(m_rendererTimer);
    });

    m_startTimer = CreateThreadpoolTimer(StartCallback, this, nullptr);
    THROW_LAST_ERROR_IF(!m_startTimer);
    // no errors, dismiss the scope guard
    deleteTimerCallbackOnError.release();
}

ctsIoPatternMediaStreamClient::~ctsIoPatternMediaStreamClient() noexcept
{
    // stop both timers
    SetThreadpoolTimer(m_startTimer, nullptr, 0, 0);
    WaitForThreadpoolTimerCallbacks(m_startTimer, FALSE);
    CloseThreadpoolTimer(m_startTimer);

    SetThreadpoolTimer(m_rendererTimer, nullptr, 0, 0);
    WaitForThreadpoolTimerCallbacks(m_rendererTimer, FALSE);
    CloseThreadpoolTimer(m_rendererTimer);
}

ctsTask ctsIoPatternMediaStreamClient::GetNextTaskFromPattern() noexcept
{
    if (0 == m_baseTimeMilliseconds)
    {
        // initiate the timers the first time the object is used
        m_baseTimeMilliseconds = ctTimer::snap_qpc_as_msec();
        SetNextStartTimer();
        std::ignore = SetNextTimer(true);
    }

    // defaulting to an empty task (do nothing)
    ctsTask returnTask;
    if (m_recvNeeded > 0)
    {
        // don't try posting more than UdpDatagramMaximumSizeBytes at a time
        uint32_t maxSizeBuffer{};
        if (m_frameSizeBytes > c_udpDatagramMaximumSizeBytes)
        {
            maxSizeBuffer = c_udpDatagramMaximumSizeBytes;
        }
        else
        {
            maxSizeBuffer = m_frameSizeBytes;
        }

        returnTask = CreateUntrackedTask(ctsTaskAction::Recv, maxSizeBuffer);
        // always write in a zero for the seq number to initialize the buffer
        *reinterpret_cast<int64_t*>(returnTask.m_buffer) = 0LL;
        --m_recvNeeded;
    }
    return returnTask;
}

ctsIoPatternError ctsIoPatternMediaStreamClient::CompleteTaskBackToPattern(const ctsTask& task, uint32_t completedBytes) noexcept
{
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);

    if (task.m_ioAction == ctsTaskAction::Abort)
    {
        // the stream should now be done
        FAIL_FAST_IF_MSG(
            !m_finishedStream,
            "ctsIOPatternMediaStreamClient (dt %p ctsTraffic!ctsTraffic::ctsIOPatternMediaStreamClient) processed an Abort before the stream was finished", this);
        return ctsIoPatternError::SuccessfullyCompleted;
    }

    if (task.m_ioAction == ctsTaskAction::Recv)
    {
        if (0 == completedBytes)
        {
            if (m_finishedStream)
            {
                // the final WSARecvFrom can complete with a zero-byte recv on loopback after the sender closes
                // TODO: verify on non-loopback
                return ctsIoPatternError::NoError;
            }

            ctsConfig::PrintErrorInfo(L"ctsIOPatternMediaStreamClient received a zero-byte datagram");
            return ctsIoPatternError::TooFewBytes;
        }

        if (!ctsMediaStreamMessage::ValidateBufferLengthFromTask(task, completedBytes))
        {
            ctsConfig::PrintErrorInfo(L"ctsIoPatternMediaStreamClient received an invalid datagram trying to parse the protocol header");
            return ctsIoPatternError::TooFewBytes;
        }

        if (ctsMediaStreamMessage::GetProtocolHeaderFromTask(task) == c_udpDatagramProtocolHeaderFlagId)
        {
            // save off the connection ID when we receive it
            ctsMediaStreamMessage::SetConnectionIdFromTask(GetConnectionIdentifier(), task);
            // since a recv completed, will need to request another
            ++m_recvNeeded;
            return ctsIoPatternError::NoError;
        }

        // validate the buffer contents
        ctsTask validationTask(task);
        validationTask.m_bufferOffset = c_udpDatagramDataHeaderLength; // skip the UdpDatagramDataHeaderLength since we use them for our own stuff
        validationTask.m_bufferLength -= c_udpDatagramDataHeaderLength;
        if (!VerifyBuffer(validationTask, completedBytes - c_udpDatagramDataHeaderLength))
        {
            // exit early if the buffers don't match
            return ctsIoPatternError::CorruptedBytes;
        }

        // track the # of *bits* received
        ctsConfig::g_configSettings->UdpStatusDetails.m_bitsReceived.Add(static_cast<int64_t>(completedBytes) * 8LL);
        m_statistics.m_bitsReceived.Add(static_cast<int64_t>(completedBytes) * 8LL);

        const auto receivedsequenceNumber = ctsMediaStreamMessage::GetSequenceNumberFromTask(task);
        if (receivedsequenceNumber > m_finalFrame)
        {
            ctsConfig::g_configSettings->UdpStatusDetails.m_errorFrames.Increment();
            m_statistics.m_errorFrames.Increment();

            PRINT_DEBUG_INFO(
                L"\t\tctsIOPatternMediaStreamClient recevieved **an unknown** seq number (%lld) (outside the final frame %lu)\n",
                receivedsequenceNumber,
                m_finalFrame);
        }
        else
        {
            //
            // search our circular queue (starting at the head_entry)
            // for the seq number we just received, and if found, tag as received
            //
            const auto foundSlot = FindSequenceNumber(receivedsequenceNumber);
            if (foundSlot != m_frameEntries.end())
            {
                const auto bufferedQpc = *reinterpret_cast<int64_t*>(task.m_buffer + 8);
                const auto bufferedQpf = *reinterpret_cast<int64_t*>(task.m_buffer + 16);

                // always overwrite qpc & qpf values with the latest datagram details
                foundSlot->m_senderQpc = bufferedQpc;
                foundSlot->m_senderQpf = bufferedQpf;
                foundSlot->m_receiverQpc = qpc.QuadPart;
                foundSlot->m_receiverQpf = ctTimer::snap_qpf();
                foundSlot->m_bytesReceived += completedBytes;

                PRINT_DEBUG_INFO(
                    L"\t\tctsIOPatternMediaStreamClient received seq number %lld (%lu received-bytes, %lu frame-bytes)\n",
                    foundSlot->m_sequenceNumber,
                    completedBytes,
                    foundSlot->m_bytesReceived);

                // stop the timer once we receive the last frame
                // - it's not perfect (e.g. might have received them out of order)
                // - but it will be very close for tracking the total bits/sec
                if (static_cast<uint32_t>(receivedsequenceNumber) == m_finalFrame)
                {
                    EndStatistics();
                }
            }
            else
            {
                // didn't find a slot for the received seq. number
                ctsConfig::g_configSettings->UdpStatusDetails.m_errorFrames.Increment();
                m_statistics.m_errorFrames.Increment();

                if (receivedsequenceNumber < m_headEntry->m_sequenceNumber)
                {
                    PRINT_DEBUG_INFO(
                        L"\t\tctsIOPatternMediaStreamClient received **a stale** seq number (%lld) - current seq number (%lld)\n",
                        receivedsequenceNumber,
                        m_headEntry->m_sequenceNumber);
                }
                else
                {
                    PRINT_DEBUG_INFO(
                        L"\t\tctsIOPatternMediaStreamClient recevieved **a future** seq number (%lld) - head of queue (%lld) tail of queue (%llu)\n",
                        receivedsequenceNumber,
                        m_headEntry->m_sequenceNumber,
                        m_headEntry->m_sequenceNumber + m_frameEntries.size() - 1);
                }
            }
        }

        // since a recv completed successfully, will need to request another
        ++m_recvNeeded;
    }
    // else this is the completion of the SEND request

    return ctsIoPatternError::NoError;
}

// Returns an iterator within frame_entries pointing to the FrameEntry
//   matching the specified sequence number.
// If the sequence number was not found, will return end(frame_entries)
//
// _Requires_lock_held_(m_lock)
vector<ctsConfig::JitterFrameEntry>::iterator ctsIoPatternMediaStreamClient::FindSequenceNumber(int64_t sequenceNumber) noexcept
{
    const auto headSequenceNumber = m_headEntry->m_sequenceNumber;
    const auto tailSequenceNumber = headSequenceNumber + static_cast<int64_t>(m_frameEntries.size()) - 1;
    const auto vectorEndSequenceNumber = m_frameEntries.rbegin()->m_sequenceNumber;

    if (sequenceNumber > tailSequenceNumber || sequenceNumber < headSequenceNumber)
    {
        // sequence number was out of range of our circular queue
        // - return end(frame_entries) to indicate it could not be found
        return end(m_frameEntries);
    }

    if (sequenceNumber <= vectorEndSequenceNumber)
    {
        // offset just from the head since it hasn't wrapped around the end
        const auto offset = static_cast<int>(sequenceNumber - headSequenceNumber);
        return m_headEntry + offset;
    }
    // offset from the beginning since it wrapped around from the end
    const auto offset = static_cast<int>(sequenceNumber - vectorEndSequenceNumber - 1);
    return m_frameEntries.begin() + offset;
}

// _Requires_lock_held_(m_lock)
bool ctsIoPatternMediaStreamClient::ReceivedBufferedFrames() noexcept
{
    if (m_frameEntries[0].m_sequenceNumber > 1)
    {
        // we've already received enough datagrams to fill one buffer
        return true;
    }
    if (m_headEntry != m_frameEntries.begin())
    {
        // we've already moved the head entry after processing a frame
        return true;
    }

    return std::ranges::any_of(m_frameEntries, [](const auto udpFrame) { return udpFrame.m_bytesReceived > 0UL; });
}

// _Requires_lock_held_(m_lock)
bool ctsIoPatternMediaStreamClient::SetNextTimer(bool initialTimer) const noexcept
{
    auto timerScheduled = false;
    // only schedule the next timer instance if the d'tor hasn't indicated it's wanting to exit
    if (m_rendererTimer != nullptr)
    {
        // calculate when that time should be relative to base_time_milliseconds 
        // (base_time_milliseconds is the start milliseconds from ctTimer::snap_qpc_msec())
        auto timerOffset = m_baseTimeMilliseconds;
        // offset to the time when we need to check the next frame
        // - we'll also render a frame at the same time if the initial buffer is full
        timerOffset += static_cast<int64_t>(static_cast<double>(m_timerWheelOffsetFrames) * m_frameRateMsPerFrame);
        // subtract out the current time to get the delta # of milliseconds
        timerOffset -= ctTimer::snap_qpc_as_msec();
        // only set the timer if we have time to wait
        if (initialTimer || timerOffset > 2)
        {
            // convert to filetime from milliseconds
            // - make a 'relative' for SetThreadpoolTimer
            FILETIME relativeFileTime(ctTimer::convert_ms_to_relative_filetime(timerOffset));
            // TP Timer APIs work off of the UTC time
            SetThreadpoolTimer(m_rendererTimer, &relativeFileTime, 0, 0);
            timerScheduled = true;
        }
    }

    return timerScheduled;
}

// _Requires_lock_held_(m_lock)
void ctsIoPatternMediaStreamClient::SetNextStartTimer() const noexcept
{
    if (m_startTimer != nullptr)
    {
        // convert to filetime from milliseconds
        // - make a 'relative' for SetThreadpoolTimer
        FILETIME relativeFileTime(ctTimer::convert_ms_to_relative_filetime(static_cast<int64_t>(m_frameRateMsPerFrame) + 500LL));
        // TP Timer APIs work off of the UTC time
        SetThreadpoolTimer(m_startTimer, &relativeFileTime, 0, 0);
    }
}

// "render the current frame"
// - update the current frame as "read" and move the head to the next frame
// _Requires_lock_held_(m_lock)
void ctsIoPatternMediaStreamClient::RenderFrame() noexcept
{
    // estimating time in flight for this frame by determining how much time since the first send was just 'waiting' to send this frame
    // and subtracing that from how much time since the first receive - since time between receives should at least be time between sends
    if (m_headEntry->m_receiverQpf != 0 && m_firstFrame.m_receiverQpf != 0)
    {
        // ReSharper disable CppRedundantParentheses
        const double msSinceFirstReceive =
            (static_cast<double>(m_headEntry->m_receiverQpc) * 1000.0f / static_cast<double>(m_headEntry->m_receiverQpf)) -
            (static_cast<double>(m_firstFrame.m_receiverQpc) * 1000.0f / static_cast<double>(m_firstFrame.m_receiverQpf));
        const double msSinceFirstSend =
            (static_cast<double>(m_headEntry->m_senderQpc) * 1000.0f / static_cast<double>(m_headEntry->m_senderQpf)) -
            (static_cast<double>(m_firstFrame.m_senderQpc) * 1000.0f / static_cast<double>(m_firstFrame.m_senderQpf));
        // ReSharper restore CppRedundantParentheses
        m_headEntry->m_estimatedTimeInFlightMs = msSinceFirstReceive - msSinceFirstSend;
    }

    if (m_headEntry->m_bytesReceived == m_frameSizeBytes)
    {
        ctsConfig::g_configSettings->UdpStatusDetails.m_successfulFrames.Increment();
        m_statistics.m_successfulFrames.Increment();

        PRINT_DEBUG_INFO(
            L"\t\tctsIOPatternMediaStreamClient rendered frame %lld\n",
            m_headEntry->m_sequenceNumber);

        // Directly write this status update if jitter is enabled
        PrintJitterUpdate(*m_headEntry, m_previousFrame);

        // if this is the first frame, capture it
        if (m_firstFrame.m_receiverQpc == 0)
        {
            m_firstFrame = *m_headEntry;
        }
        // always keep the most recently received frame for jitter
        m_previousFrame = *m_headEntry;
    }
    else if (m_headEntry->m_bytesReceived < m_frameSizeBytes)
    {
        ctsConfig::g_configSettings->UdpStatusDetails.m_droppedFrames.Increment();
        m_statistics.m_droppedFrames.Increment();

        PRINT_DEBUG_INFO(
            L"\t\tctsIOPatternMediaStreamClient **dropped** frame for seq number (%lld)\n",
            m_headEntry->m_sequenceNumber);

        // track the dropped frame
        // indicate zero's for the other values so we won't calculate jitter for a dropped datagram
        ctsConfig::JitterFrameEntry droppedFrame;
        droppedFrame.m_sequenceNumber = m_headEntry->m_sequenceNumber;
        PrintJitterUpdate(droppedFrame, ctsConfig::JitterFrameEntry());
    }
    else // m_headEntry->bytes_received > m_frameSizeBytes
    {
        ctsConfig::g_configSettings->UdpStatusDetails.m_duplicateFrames.Increment();
        m_statistics.m_duplicateFrames.Increment();

        PRINT_DEBUG_INFO(
            L"\t\tctsIOPatternMediaStreamClient **a duplicate** frame for seq number (%lld)\n",
            m_headEntry->m_sequenceNumber);
    }

    // update the current sequence number so it's now the "end" sequence number of the queue (the new max value)
    m_headEntry->m_sequenceNumber = m_headEntry->m_sequenceNumber + static_cast<int64_t>(m_frameEntries.size());
    m_headEntry->m_bytesReceived = 0;

    // move the head entry to the next sequence number
    ++m_headEntry;
    if (m_headEntry == m_frameEntries.end())
    {
        m_headEntry = m_frameEntries.begin();
    }
}

VOID CALLBACK ctsIoPatternMediaStreamClient::StartCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID pContext, PTP_TIMER) noexcept
{
    static constexpr char c_startBuffer[] = "START";

    auto* thisPtr = static_cast<ctsIoPatternMediaStreamClient*>(pContext);
    // take the base lock before touching any internal members
    const auto lock = thisPtr->AcquireIoPatternLock();

    if (thisPtr->m_finishedStream)
    {
        return;
    }

    if (!thisPtr->ReceivedBufferedFrames())
    {
        // send another start message
        PRINT_DEBUG_INFO(L"\t\tctsIOPatternMediaStreamClient re-requesting START\n");

        ctsTask resendTask;
        resendTask.m_ioAction = ctsTaskAction::Send;
        resendTask.m_trackIo = false;
        resendTask.m_buffer = const_cast<char*>(c_startBuffer);
        resendTask.m_bufferOffset = 0;
        // ReSharper disable once CppRedundantParentheses
        resendTask.m_bufferLength = sizeof(c_startBuffer) - 1; // minus null at the end of the string
        resendTask.m_bufferType = ctsTask::BufferType::Static; // this is our own buffer: the base class should not mess with it

        thisPtr->SetNextStartTimer();
        thisPtr->SendTaskToCallback(resendTask);
    }
    // else, don't schedule this timer anymore
}

VOID CALLBACK ctsIoPatternMediaStreamClient::TimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID pContext, PTP_TIMER) noexcept
{
    auto* thisPtr = static_cast<ctsIoPatternMediaStreamClient*>(pContext);

    // process frames until the timer is scheduled in the future to process more frames
    auto timerScheduled = false;
    while (!timerScheduled)
    {
        // take the base lock before touching any internal members
        const auto lock = thisPtr->AcquireIoPatternLock();

        if (thisPtr->m_finishedStream)
        {
            return;
        }

        ++thisPtr->m_timerWheelOffsetFrames;

        auto fatalAborted = false;
        if (thisPtr->m_timerWheelOffsetFrames >= thisPtr->m_initialBufferFrames &&
            thisPtr->m_headEntry->m_sequenceNumber <= thisPtr->m_finalFrame)
        {
            // if we haven't yet received *anything* from the server, abort this connection
            if (!thisPtr->ReceivedBufferedFrames())
            {
                ctsConfig::PrintErrorInfo(L"ctsIOPatternMediaStreamClient - issuing a FATALABORT to close the connection - have received nothing from the server");

                // indicate all frames were dropped
                ctsConfig::g_configSettings->UdpStatusDetails.m_droppedFrames.Add(thisPtr->m_finalFrame);
                thisPtr->m_statistics.m_droppedFrames.Add(thisPtr->m_finalFrame);

                thisPtr->m_finishedStream = true;
                ctsTask abortTask;
                abortTask.m_ioAction = ctsTaskAction::FatalAbort;
                thisPtr->SendTaskToCallback(abortTask);
                fatalAborted = true;
            }
            else
            {
                // if the initial buffer has already been filled, "render" the frame
                thisPtr->RenderFrame();
            }
        }

        if (!fatalAborted)
        {
            // wait for the precise number of milliseconds for the next frame
            if (thisPtr->m_headEntry->m_sequenceNumber <= thisPtr->m_finalFrame)
            {
                timerScheduled = thisPtr->SetNextTimer(false);
            }
            else
            {
                thisPtr->m_finishedStream = true;
                ctsTask abortTask;
                abortTask.m_ioAction = ctsTaskAction::Abort;
                thisPtr->SendTaskToCallback(abortTask);
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternMediaStreamClient - issuing an ABORT to cleanly close the connection\n");
            }
        }
    }
}
}
