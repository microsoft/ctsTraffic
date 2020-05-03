/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// parent header
#include "ctsIOPattern.h"
// cpp headers
#include <vector>
// wil headers
#include <wil/resource.h>
// ctl headers
#include <ctSocketExtensions.hpp>
#include <ctTimer.hpp>
#include <ctString.hpp>
// project headers
#include "ctsMediaStreamProtocol.hpp"
#include "ctsIOBuffers.hpp"


namespace ctsTraffic {

    using namespace ctl;
    using namespace std;

    constexpr unsigned long c_BufferPatternSize = 0xffff + 0x1; // fill from 0x0000 to 0xffff
    static unsigned char s_BufferPattern[c_BufferPatternSize * 2]; // * 2 as unsigned short values are twice as large as unsigned char

    /// SharedBuffer is a larger buffer with many copies of BufferPattern in it. This is what the various IO patterns
    /// will be memcmp'ing against for validity checks.
    ///
    /// The buffers' sizes will be the constant "BufferPatternSize + ctsConfig::GetMaxBufferSize()", but we
    /// need to wait for input parsing before we can set that.

    static INIT_ONCE s_IoPatternInitializer = INIT_ONCE_STATIC_INIT;
    static char* s_WriteableSharedBuffer = nullptr;
    static char* s_ProtectedSharedBuffer = nullptr;
    static unsigned long s_SharedBufferSize = 0;
    static RIO_BUFFERID s_SharedBufferId = RIO_INVALID_BUFFERID;  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)

    const char* s_CompletionMessage = "DONE";
    constexpr unsigned long c_CompletionMessageSize = 4;
    constexpr unsigned long c_FinBufferSize = 4; // just 4 bytes for the FIN
    static char s_FinBuffer[c_FinBufferSize];

    BOOL CALLBACK InitOnceIoPatternCallback(PINIT_ONCE, PVOID, PVOID*) noexcept
    {
        // first create the buffer pattern
        for (unsigned long fill_slot = 0; fill_slot < c_BufferPatternSize; ++fill_slot)
        {
            *reinterpret_cast<unsigned short*>(&s_BufferPattern[fill_slot * 2]) = static_cast<unsigned short>(fill_slot);
        }

        s_SharedBufferSize = c_BufferPatternSize + ctsConfig::GetMaxBufferSize() + c_CompletionMessageSize;

        s_ProtectedSharedBuffer = static_cast<char*>(VirtualAlloc(nullptr, s_SharedBufferSize, MEM_COMMIT, PAGE_READWRITE));
        if (!s_ProtectedSharedBuffer)
        {
            FAIL_FAST_MSG("VirtualAlloc alloc failed: %u", GetLastError());
        }

        s_WriteableSharedBuffer = static_cast<char*>(VirtualAlloc(nullptr, s_SharedBufferSize, MEM_COMMIT, PAGE_READWRITE));
        if (!s_WriteableSharedBuffer)
        {
            FAIL_FAST_MSG("VirtualAlloc alloc failed: %u", GetLastError());
        }

        // fill in this allocated buffer while we can write to it
        char* protected_destination = s_ProtectedSharedBuffer;
        char* writeable_destination = s_WriteableSharedBuffer;
        unsigned long write_size_remaining = s_SharedBufferSize;
        while (write_size_remaining > 0)
        {
            const unsigned long bytes_to_write = (write_size_remaining > c_BufferPatternSize) ? c_BufferPatternSize : write_size_remaining;

            auto memerror = memcpy_s(protected_destination, write_size_remaining, s_BufferPattern, bytes_to_write);
            FAIL_FAST_IF_MSG(
                memerror != 0,
                "memcpy_s(%p, %lu, %p, %lu) failed : %d",
                protected_destination, write_size_remaining, s_BufferPattern, bytes_to_write, memerror);

            memerror = memcpy_s(writeable_destination, write_size_remaining, s_BufferPattern, bytes_to_write);
            FAIL_FAST_IF_MSG(
                memerror != 0,
                "memcpy_s(%p, %lu, %p, %lu) failed : %d",
                writeable_destination, write_size_remaining, s_BufferPattern, bytes_to_write, memerror);

            protected_destination += bytes_to_write;
            writeable_destination += bytes_to_write;
            write_size_remaining -= bytes_to_write;
        }
        // set the final 4 bytes to the DONE message for the send buffer
        memcpy_s(
            s_ProtectedSharedBuffer + s_SharedBufferSize - c_CompletionMessageSize,
            c_CompletionMessageSize,
            s_CompletionMessage,
            c_CompletionMessageSize);
        memcpy_s(
            s_WriteableSharedBuffer + s_SharedBufferSize - c_CompletionMessageSize,
            c_CompletionMessageSize,
            s_CompletionMessage,
            c_CompletionMessageSize);

        // guarantee noone will write to our s_ProtectedSharedBuffer
        DWORD old_setting;
        if (!VirtualProtect(s_ProtectedSharedBuffer, s_SharedBufferSize, PAGE_READONLY, &old_setting))
        {
            FAIL_FAST_MSG("VirtualProtect failed: %u", GetLastError());
        }

        // establish a RIO ID for the writable shared buffer if we're using RIO APIs
        if (ctsConfig::Settings->SocketFlags & WSA_FLAG_REGISTERED_IO)
        {
            s_SharedBufferId = ctRIORegisterBuffer(s_WriteableSharedBuffer, s_SharedBufferSize);
            if (RIO_INVALID_BUFFERID == s_SharedBufferId)
            {  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
                FAIL_FAST_MSG("RIORegisterBuffer failed: %d", WSAGetLastError());
            }
        }

        return TRUE;
    }

    ///
    /// Helper factory to build known patterns
    /// - can throw ctException on a Win32 error
    /// - can throw exception on allocation failure
    ///
    shared_ptr<ctsIOPattern> ctsIOPattern::MakeIOPattern()
    {
        switch (ctsConfig::Settings->IoPattern)
        {
            case ctsConfig::IoPatternType::Pull:
                return make_shared<ctsIOPatternPull>();

            case ctsConfig::IoPatternType::Push:
                return make_shared<ctsIOPatternPush>();

            case ctsConfig::IoPatternType::PushPull:
                return make_shared<ctsIOPatternPushPull>();

            case ctsConfig::IoPatternType::Duplex:
                return make_shared<ctsIOPatternDuplex>();

            case ctsConfig::IoPatternType::MediaStream:
                if (ctsConfig::IsListening())
                {
                    return make_shared<ctsIOPatternMediaStreamServer>();
                }
                else
                {
                    return make_shared<ctsIOPatternMediaStreamClient>();
                }

            default:
                FAIL_FAST_MSG("ctsIOPattern::MakeIOPattern - Unknown IoPattern specified (%d)", ctsConfig::Settings->IoPattern);
        }
    }
    char* ctsIOPattern::AccessSharedBuffer() noexcept
    {
        // this init-once call is no-fail
        (void)InitOnceExecuteOnce(&s_IoPatternInitializer, InitOnceIoPatternCallback, nullptr, nullptr);
        return s_ProtectedSharedBuffer;
    }

    ctsIOPattern::ctsIOPattern(unsigned long recv_count) :
        // (bytes/sec) * (1 sec/1000 ms) * (x ms/Quantum) == (bytes/quantum)
        m_bytesSendingPerQuantum(ctsConfig::GetTcpBytesPerSecond()* static_cast<unsigned long long>(ctsConfig::Settings->TcpBytesPerSecondPeriod) / 1000LL),
        m_quantumStartTimeMs(ctTimer::ctSnapQpcInMillis())
    {
        FAIL_FAST_IF_MSG(
            ctsConfig::Settings->UseSharedBuffer && ctsConfig::Settings->ShouldVerifyBuffers,
            "Cannot use a shared buffer across connections and still verify buffers");

        // this init-once call is no-fail
        (void)InitOnceExecuteOnce(&s_IoPatternInitializer, InitOnceIoPatternCallback, nullptr, nullptr);

        // if TCP, will always need a recv buffer for the final FIN 
        if ((recv_count > 0) || (ctsConfig::Settings->Protocol == ctsConfig::ProtocolType::TCP))
        {
            // recv will only use the same shared buffer when the user specified to do so on the cmdline
            if (ctsConfig::Settings->UseSharedBuffer)
            {
                if (recv_count > 0)
                {
                    for (unsigned long free_list = 0; free_list < recv_count; ++free_list)
                    {
                        m_recvBufferFreeList.push_back(s_WriteableSharedBuffer);
                    }
                    // if using RIO, can share the same BufferId when not needing to validate the buffer
                    m_recvRioBufferid = s_SharedBufferId;
                }
                else
                {
                    // just use the shared buffer to capture the ACK's since recv_count == 0
                    m_recvBufferFreeList.push_back(s_WriteableSharedBuffer);
                    m_recvRioBufferid = s_SharedBufferId;
                }
            }
            else
            {
                if (recv_count > 0)
                {
                    m_recvBufferContainer.resize(ctsConfig::GetMaxBufferSize() * recv_count);
                    char* raw_recv_buffer = &m_recvBufferContainer[0];
                    for (unsigned long free_list = 0; free_list < recv_count; ++free_list)
                    {
                        m_recvBufferFreeList.push_back(raw_recv_buffer + static_cast<size_t>(free_list * ctsConfig::GetMaxBufferSize()));
                    }
                }
                else
                {
                    // just use the shared buffer to capture the FIN since recv_count == 0
                    m_recvBufferFreeList.push_back(s_WriteableSharedBuffer);
                    m_recvRioBufferid = s_SharedBufferId;
                }
            }

            if (ctsConfig::Settings->SocketFlags & WSA_FLAG_REGISTERED_IO &&
                m_recvRioBufferid != s_SharedBufferId)
            {
                FAIL_FAST_IF_MSG(recv_count > 1, "Currently not supporting >1 concurrent IO requests with RIO");
                m_recvRioBufferid = ctRIORegisterBuffer(m_recvBufferFreeList[0], static_cast<DWORD>(ctsConfig::GetMaxBufferSize()));
                if (RIO_INVALID_BUFFERID == m_recvRioBufferid)
                {  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
                    throw ctException(WSAGetLastError(), L"RIORegisterBuffer", L"ctsIOPattern", false);
                }
            }
        }
    }


    ctsIOPattern::~ctsIOPattern() noexcept  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
    {
        if (m_recvRioBufferid != RIO_INVALID_BUFFERID && m_recvRioBufferid != s_SharedBufferId)
        {  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
            ctRIODeregisterBuffer(m_recvRioBufferid);
        }
    }

    ctsIOTask ctsIOPattern::initiate_io() noexcept
    {
        // make sure stats starts tracking IO at the first IO request
        this->start_stats();

        const auto local_cs = m_cs.lock();
        ctsIOTask return_task;
        switch (m_patternState.get_next_task())
        {
            case ctsIOPatternProtocolTask::MoreIo:
                return_task = this->next_task();
                break;

            case ctsIOPatternProtocolTask::NoIo:
                break;

            case ctsIOPatternProtocolTask::SendConnectionId: {
                return_task = ctsIOBuffers::NewConnectionIdBuffer(this->connection_id());
                return_task.ioAction = IOTaskAction::Send;
                break;
            }

            case ctsIOPatternProtocolTask::RecvConnectionId:
                return_task = ctsIOBuffers::NewConnectionIdBuffer(this->connection_id());
                return_task.ioAction = IOTaskAction::Recv;
                break;

            case ctsIOPatternProtocolTask::SendCompletion:
                // end-stats as early as possible after the actual IO finished
                this->end_stats();

                // using the static buffer - identical for both RIO and non-RIO
                // - currently won't be validating the completion message
                return_task.ioAction = IOTaskAction::Send;
                return_task.buffer = s_ProtectedSharedBuffer;
                return_task.rio_bufferid = s_SharedBufferId;
                return_task.buffer_length = c_CompletionMessageSize;
                return_task.buffer_offset = s_SharedBufferSize - c_CompletionMessageSize;
                return_task.track_io = false;
                return_task.buffer_type = ctsIOTask::BufferType::Static;
                break;

            case ctsIOPatternProtocolTask::RecvCompletion:
                // end-stats as early as possible after the actual IO finished
                this->end_stats();

                // using the static buffer - identical for both RIO and non-RIO
                // - currently won't be validating the completion message
                return_task.ioAction = IOTaskAction::Recv;
                return_task.buffer = s_WriteableSharedBuffer;
                return_task.rio_bufferid = s_SharedBufferId;
                return_task.buffer_length = c_CompletionMessageSize;
                return_task.buffer_offset = s_SharedBufferSize - c_CompletionMessageSize;
                return_task.track_io = false;
                return_task.buffer_type = ctsIOTask::BufferType::Static;
                break;

            case ctsIOPatternProtocolTask::HardShutdown:
                // end-stats as early as possible after the actual IO finished
                this->end_stats();

                return_task.ioAction = IOTaskAction::HardShutdown;
                return_task.buffer = nullptr;
                return_task.buffer_length = 0;
                return_task.buffer_offset = 0;
                return_task.track_io = false;
                return_task.buffer_type = ctsIOTask::BufferType::Null;
                break;

            case ctsIOPatternProtocolTask::GracefulShutdown:
                // end-stats as early as possible after the actual IO finished
                this->end_stats();

                return_task.ioAction = IOTaskAction::GracefulShutdown;
                return_task.buffer = nullptr;
                return_task.buffer_length = 0;
                return_task.buffer_offset = 0;
                return_task.track_io = false;
                return_task.buffer_type = ctsIOTask::BufferType::Null;
                break;

            case ctsIOPatternProtocolTask::RequestFIN:
                // post one final recv for the zero byte FIN
                // end-stats as early as possible after the actual IO finished
                this->end_stats();

                FAIL_FAST_IF_MSG(
                    m_recvBufferFreeList.empty(),
                    "ctsIOPattern::initiate_io : (%p) recv_buffer_free_list is empty", this);

                if (m_recvRioBufferid != RIO_INVALID_BUFFERID)
                {  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
                    // RIO must always use the allocated buffers which were registered
                    return_task.buffer = *m_recvBufferFreeList.rbegin();
                    m_recvBufferFreeList.pop_back();
                    return_task.rio_bufferid = m_recvRioBufferid;
                    return_task.buffer_type = ctsIOTask::BufferType::Tracked;
                }
                else
                {
                    return_task.buffer = s_FinBuffer;
                    return_task.buffer_type = ctsIOTask::BufferType::Static;
                }

                return_task.ioAction = IOTaskAction::Recv;
                return_task.buffer_length = c_FinBufferSize;
                return_task.buffer_offset = 0;
                return_task.track_io = false;
                break;

            default:
                FAIL_FAST_MSG("ctsIOPattern::initiate_io was called in an invalid state: dt %p ctsTraffic!ctsTraffic::ctsIOPattern", this);
        }

        m_patternState.notify_next_task(return_task);
        return return_task;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// complete_io
    ///
    /// updates its internal counters to prepare for the next IO request
    /// - the fact that complete_io was called assumes that the IO was successful
    /// 
    /// _original_task : the task provided to the caller from initiate_io (or a copy of)
    /// _current_transfer : the number of bytes successfully transferred from the task
    /// _status_code: the return code from the prior IO operation [assumes a Win32 error code]
    ///
    /// Returns the current status of the IO operation on this socket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOStatus ctsIOPattern::complete_io(const ctsIOTask& original_task, unsigned long current_transfer, unsigned long status_code) noexcept
    {
        const auto lock = m_cs.lock();

        // Only add the recv buffer back if it was one of our listed recv buffers
        if (ctsIOTask::BufferType::Tracked == original_task.buffer_type)
        {
            m_recvBufferFreeList.push_back(original_task.buffer);
        }

        // preserve the previous task
        const bool task_was_more_io = m_patternState.is_current_task_more_io();

        switch (original_task.ioAction)
        {
            case IOTaskAction::None:
                // ignore completions for tasks on None
                break;

            case IOTaskAction::FatalAbort:
                PrintDebugInfo(L"\t\tctsIOPattern : completing a FatalAbort\n");
                this->update_last_error(ctsStatusErrorNotAllDataTransferred);
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
                //
                // Fall-through to Send - where the IO will be processed
                //
            case IOTaskAction::Send:
                bool verify_io = true;
                if (ctsIOTask::BufferType::TcpConnectionId == original_task.buffer_type)
                {
                    //
                    // not verifying the IO buffer if this is the connection id request
                    // - but must complete the task to update the protocol
                    //
                    verify_io = false;

                    if (status_code != NO_ERROR)
                    {
                        this->update_last_error(status_code);
                    }
                    else
                    {
                        if (IOTaskAction::Recv == original_task.ioAction)
                        {
                            // save off the connection ID when we receive it
                            if (!ctsIOBuffers::SetConnectionId(this->connection_id(), original_task, current_transfer))
                            {
                                this->update_last_error(ctsStatusErrorDataDidNotMatchBitPattern);
                            }
                        }

                        // process the TCP protocol state machine in pattern_state after receiving the connection id
                        this->update_last_protocol_error(m_patternState.completed_task(original_task, current_transfer));

                        if (original_task.ioAction == IOTaskAction::Send)
                        {
                            ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(current_transfer);
                        }
                        else
                        {
                            ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(current_transfer);
                        }
                    }
                    ctsIOBuffers::ReleaseConnectionIdBuffer(original_task);

                }
                else if (status_code != NO_ERROR)
                {
                    //
                    // if the IO task failed, the entire IO pattern is now failed
                    // - unless this is an extra recv that was canceled once we completed the transfer
                    //
                    if (IOTaskAction::Recv == original_task.ioAction && m_patternState.is_completed())
                    {
                        PrintDebugInfo(L"\t\tctsIOPattern : Recv failed after the pattern completed (error %u)\n", status_code);
                    }
                    else
                    {
                        const auto current_status = this->update_last_error(status_code);
                        if (current_status != ctsStatusIORunning)
                        {
                            PrintDebugInfo(L"\t\tctsIOPattern : Recv failed before the pattern completed (error %u, current status %u)\n", status_code, current_status);
                            verify_io = false;
                        }
                    }
                }

                if (verify_io)
                {
                    //
                    // IO succeeded - update state machine with the completed task if this task had IO
                    //
                    const auto pattern_status = m_patternState.completed_task(original_task, current_transfer);
                    // update the last_error if the pattern_state detected an error
                    this->update_last_protocol_error(pattern_status);
                    //
                    // if this is a TCP receive completion
                    // and no IO or protocol errors
                    // and the user requested to verify buffers
                    // then actually validate the received completion
                    //
                    if (ctsConfig::Settings->Protocol == ctsConfig::ProtocolType::TCP &&
                        ctsConfig::Settings->ShouldVerifyBuffers &&
                        original_task.ioAction == IOTaskAction::Recv &&
                        original_task.track_io &&
                        (ctsIOPatternProtocolError::SuccessfullyCompleted == pattern_status || ctsIOPatternProtocolError::NoError == pattern_status))
                    {

                        FAIL_FAST_IF_MSG(
                            original_task.expected_pattern_offset != m_recvPatternOffset,
                            "ctsIOPattern::complete_io() : ctsIOTask (%p) expected_pattern_offset (%lu) does not match the current pattern_offset (%Iu)",
                            &original_task, original_task.expected_pattern_offset, static_cast<size_t>(m_recvPatternOffset));

                        if (!VerifyBuffer(original_task, current_transfer))
                        {
                            this->update_last_error(ctsStatusErrorDataDidNotMatchBitPattern);
                        }

                        m_recvPatternOffset += current_transfer;
                        m_recvPatternOffset %= c_BufferPatternSize;
                    }
                }
                break;
        }
        //
        // Notify the derived interface that the task completed
        // - if this wasn't our internal connection id request
        // - if there wasn't an error with the IO and an IO operation completed
        // If the derived interface returns an error,
        // - update the last_error status
        //
        if ((original_task.ioAction != IOTaskAction::None) &&
            (NO_ERROR == status_code))
        {

            if (IOTaskAction::Send == original_task.ioAction)
            {
                ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(current_transfer);
            }
            else
            {
                ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(current_transfer);
            }
            // only complete tasks that were requested
            if (task_was_more_io)
            {
                this->update_last_protocol_error(
                    this->completed_task(original_task, current_transfer));
            }
        }
        //
        // If the state machine has verified the connection has completed, 
        // - set the last error to zero in case it was not already set to an error
        //   but do this *after* the other possible failure points were checked
        //
        if (m_patternState.is_completed())
        {
            this->update_last_error(NO_ERROR);
            this->end_stats();
        }

        return this->current_status();
    }

    ctsIOTask ctsIOPattern::tracked_task(IOTaskAction _action, unsigned long max_transfer) noexcept
    {
        const auto lock = m_cs.lock();
        ctsIOTask return_task(this->new_task(_action, max_transfer));
        return_task.track_io = true;
        return return_task;
    }

    ctsIOTask ctsIOPattern::untracked_task(IOTaskAction _action, unsigned long max_transfer) noexcept
    {
        const auto lock = m_cs.lock();
        ctsIOTask return_task(this->new_task(_action, max_transfer));
        return_task.track_io = false;
        return return_task;
    }

    ctsIOTask ctsIOPattern::new_task(IOTaskAction action, unsigned long max_transfer) noexcept
    {
        //
        // with TCP, we need to calculate the buffer size based off bytes remaining
        // with UDP, we're always posting the same size buffer
        //

        //
        // first: calculate the next buffer size assuming no max ceiling specified by the protocol
        //
        ctsSignedLongLong new_buffer_size = min<ctsUnsignedLongLong>(
            ctsConfig::GetBufferSize(),
            m_patternState.get_remaining_transfer());
        //
        // second: if the protocol specified a ceiling, recalculate given their ceiling
        //
        if ((max_transfer > 0) && (max_transfer < new_buffer_size))
        {
            new_buffer_size = max_transfer;
        }
        //
        // guard against hitting a 32-bit overflow
        //
        FAIL_FAST_IF_MSG(
            new_buffer_size > MAXDWORD,
            "ctsIOPattern internal error: next buffer size (%llu) is greater than MAXDWORD (%u)",
            static_cast<ULONGLONG>(new_buffer_size), MAXDWORD);
        //
        // build the next IO request with a properly calculated buffer size
        // Send must specify the offset because we must align the patterns that we send
        // Recv must not specify an offset because will always use the entire buffer for the recv
        //
        ctsIOTask return_task;
        if (IOTaskAction::Send == action)
        {
            //
            // check to see if the send needs to be deferred into the future
            //
            if (m_bytesSendingPerQuantum > 0)
            {
                const auto current_time_ms(ctTimer::ctSnapQpcInMillis());
                if (m_bytesSendingThisQuantum < m_bytesSendingPerQuantum)
                {
                    // adjust bytes_sending_this_quantum
                    m_bytesSendingThisQuantum += new_buffer_size;

                    // no need to adjust quantum_start_time_ms unless we skipped into a new quantum
                    // (meaning the previous quantum had not filled the max bytes for that quantum)
                    if (current_time_ms > (m_quantumStartTimeMs + ctsConfig::Settings->TcpBytesPerSecondPeriod))
                    {
                        // current time shows it's now beyond this quantum timeframe
                        // - once we see how many quantums we have skipped forward, move our quantum start time to the quantum we are actually in
                        // - then adjust the number of bytes we are to send this quantum by how many quantum we just skipped
                        const auto quantums_skipped_since_last_send = (current_time_ms - m_quantumStartTimeMs) / ctsConfig::Settings->TcpBytesPerSecondPeriod;
                        m_quantumStartTimeMs += quantums_skipped_since_last_send * ctsConfig::Settings->TcpBytesPerSecondPeriod;

                        // we have to be careful making this adjustment since the remainingbytes this quantum could be very small
                        // - we only subtract out if the number of bytes skipped is >= bytes actually skipped
                        const auto bytes_to_adjust = m_bytesSendingPerQuantum * quantums_skipped_since_last_send;
                        if (bytes_to_adjust > m_bytesSendingThisQuantum)
                        {
                            m_bytesSendingThisQuantum = 0;
                        }
                        else
                        {
                            m_bytesSendingThisQuantum -= bytes_to_adjust;
                        }
                    }
                    // update the return task for when to schedule the send
                    return_task.time_offset_milliseconds = 0LL;
                }
                else
                {
                    // we have sent more than required for this quantum
                    // - check if this fullfilled future quantums as well
                    const auto quantum_ahead_to_schedule = static_cast<unsigned long>(m_bytesSendingThisQuantum / m_bytesSendingPerQuantum);

                    // ms_for_quantums_to_skip = the # of quantum beyond the current quantum that will be skipped
                    // - when we have already sent at least 1 additional quantum of bytes
                    const ctsSignedLongLong ms_for_quantums_to_skip = (quantum_ahead_to_schedule - 1) * ctsConfig::Settings->TcpBytesPerSecondPeriod;

                    // carry forward extra bytes from quantums that will be filled by the bytes we have already sent
                    // (including the current quantum)
                    // then adding the bytes we're about to send
                    m_bytesSendingThisQuantum -= m_bytesSendingPerQuantum * quantum_ahead_to_schedule;
                    m_bytesSendingThisQuantum += new_buffer_size;

                    // update the return task for when to schedule the send
                    // first, calculate the time to get to the end of this time quantum
                    // - only adjust if the current time isn't already outside this quantum
                    if (current_time_ms < m_quantumStartTimeMs + ctsConfig::Settings->TcpBytesPerSecondPeriod)
                    {
                        return_task.time_offset_milliseconds = (m_quantumStartTimeMs + ctsConfig::Settings->TcpBytesPerSecondPeriod) - current_time_ms;
                    }
                    // then add in any quantum we need to skip
                    return_task.time_offset_milliseconds += ms_for_quantums_to_skip;

                    // finally, adjust quantum_start_time_ms to the next quantum which IO will complete
                    m_quantumStartTimeMs += ms_for_quantums_to_skip + ctsConfig::Settings->TcpBytesPerSecondPeriod;
                }
            }
            else
            {
                return_task.time_offset_milliseconds = 0LL;
            }

            return_task.ioAction = IOTaskAction::Send;
            return_task.buffer = s_ProtectedSharedBuffer;
            return_task.rio_bufferid = s_SharedBufferId;
            return_task.buffer_length = static_cast<unsigned long>(new_buffer_size);
            return_task.buffer_offset = static_cast<unsigned long>(m_sendPatternOffset);
            return_task.expected_pattern_offset = 0; // The sender shouldn't be validating this
            return_task.buffer_type = ctsIOTask::BufferType::Static;

            // now that we are indicating this buffer to send, increment the offset for the next send request
            m_sendPatternOffset += new_buffer_size;
            m_sendPatternOffset %= c_BufferPatternSize;

            FAIL_FAST_IF_MSG(
                m_sendPatternOffset >= c_BufferPatternSize,
                "this->pattern_offset being too large (larger than BufferPatternSize %lu) means we might walk off the end of our shared buffer (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                c_BufferPatternSize, this);
            FAIL_FAST_IF_MSG(
                return_task.buffer_length + return_task.buffer_offset > s_SharedBufferSize,
                "return_task (%p) for a Send request is specifying a buffer that is larger than the static SharedBufferSize (%lu) (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                &return_task, s_SharedBufferSize, this);

        }
        else
        {
            FAIL_FAST_IF_MSG(
                m_recvBufferFreeList.empty(),
                "recv_buffer_free_list is empty for a new Recv task  (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)", this);

            return_task.ioAction = IOTaskAction::Recv;
            return_task.buffer = *m_recvBufferFreeList.rbegin();
            m_recvBufferFreeList.pop_back();
            return_task.buffer_type = ctsIOTask::BufferType::Tracked;

            return_task.rio_bufferid = m_recvRioBufferid;
            return_task.buffer_length = static_cast<unsigned long>(new_buffer_size);
            return_task.buffer_offset = 0; // always recv to the beginning of the buffer
            return_task.expected_pattern_offset = static_cast<unsigned long>(m_recvPatternOffset);

            FAIL_FAST_IF_MSG(
                m_recvPatternOffset >= c_BufferPatternSize,
                "pattern_offset being too large means we might walk off the end of our shared buffer (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)", this);
            FAIL_FAST_IF_MSG(
                return_task.buffer_length + return_task.buffer_offset > new_buffer_size,
                "return_task (%p) for a Recv request is specifying a buffer that is larger than buffer_size (%lu) (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                &return_task, static_cast<unsigned long>(new_buffer_size), this);
        }

        return return_task;
    }

    // static
    bool ctsIOPattern::VerifyBuffer(const ctsIOTask& original_task, unsigned long transferred_bytes) noexcept
    {
        // only doing deep verification if the user asked us to
        if (!ctsConfig::Settings->ShouldVerifyBuffers)
        {
            return true;
        }
        //
        // We're using RtlCompareMemory instead of memcmp because it returns the first offset at which the buffers differ,
        // which is more useful than memcmp's "sign of the difference between the first two differing elements"
        //
        const auto pattern_buffer = s_ProtectedSharedBuffer + original_task.expected_pattern_offset;
        const size_t length_matched = RtlCompareMemory(
            pattern_buffer,
            original_task.buffer + original_task.buffer_offset,
            transferred_bytes);
        if (length_matched != transferred_bytes)
        {
            try
            {
                ctsConfig::PrintErrorInfo(
                    ctString::ctFormatString(
                        "ctsIOPattern found data corruption: detected an invalid byte pattern in the returned buffer (length %u): "
                        "buffer received (%p), expected buffer pattern (%p) - mismatch from expected pattern at offset (%Iu) [expected 32-bit value '0x%x' didn't match '0x%x']",
                        transferred_bytes,
                        original_task.buffer + original_task.buffer_offset,
                        pattern_buffer,
                        length_matched,
                        pattern_buffer[length_matched],
                        *(original_task.buffer + original_task.buffer_offset + length_matched)).c_str());
            }
            catch (...)
            {
            }
        }

        return (length_matched == transferred_bytes);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///     - Pull Pattern
    ///    -- TCP-only
    ///    -- The server pushes data (sends)
    ///    -- The client pulls data (receives)
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOPatternPull::ctsIOPatternPull() :
        ctsIOPatternStatistics(ctsConfig::IsListening() ? 0 : ctsConfig::Settings->PrePostRecvs),
        m_ioAction(ctsConfig::IsListening() ? IOTaskAction::Send : IOTaskAction::Recv),
        m_recvNeeded(ctsConfig::IsListening() ? 0 : ctsConfig::Settings->PrePostRecvs),
        m_sendBytesInflight(0)
    {
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// virtual methods from the base class:
    /// - assumes will be called under a CS from the base class
    ///
    /// tracking # of outstanding IO requests (configurable through the c'tor)
    ///
    /// Return an empty task when no more IO is needed
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOTask ctsIOPatternPull::next_task() noexcept
    {
        if (m_ioAction == IOTaskAction::Recv && m_recvNeeded > 0)
        {
            --m_recvNeeded;
            return this->tracked_task(m_ioAction);
        }

        if (m_ioAction == IOTaskAction::Send && this->get_ideal_send_backlog() > m_sendBytesInflight)
        {
            const auto max_bytes_to_send = this->get_ideal_send_backlog() - m_sendBytesInflight;
            const auto return_task(this->tracked_task(m_ioAction, max_bytes_to_send));
            m_sendBytesInflight += return_task.buffer_length;
            return return_task;
        }

        return ctsIOTask();
    }
    ctsIOPatternProtocolError ctsIOPatternPull::completed_task(const ctsIOTask& task, unsigned long completed_bytes) noexcept
    {
        if (IOTaskAction::Send == task.ioAction)
        {
            this->stats.bytes_sent.add(completed_bytes);
            m_sendBytesInflight -= completed_bytes;
        }
        else
        {
            this->stats.bytes_recv.add(completed_bytes);
            ++m_recvNeeded;
        }

        return ctsIOPatternProtocolError::NoError;
    }


    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  - Push Pattern
    ///    -- TCP-only
    ///    -- The client pushes data (send)
    ///    -- The server pulls data (recv)
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOPatternPush::ctsIOPatternPush() :
        ctsIOPatternStatistics(ctsConfig::IsListening() ? ctsConfig::Settings->PrePostRecvs : 0),
        m_ioAction(ctsConfig::IsListening() ? IOTaskAction::Recv : IOTaskAction::Send),
        m_recvNeeded(ctsConfig::IsListening() ? ctsConfig::Settings->PrePostRecvs : 0),
        m_sendBytesInflight(0)
    {
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// virtual methods from the base class:
    /// - assumes will be called under a CS from the base class
    ///
    /// tracking # of outstanding IO requests (configurable through the c'tor)
    ///
    /// Return an empty task when no more IO is needed
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOTask ctsIOPatternPush::next_task() noexcept
    {
        if (m_ioAction == IOTaskAction::Recv && m_recvNeeded > 0)
        {
            --m_recvNeeded;
            return this->tracked_task(m_ioAction);
        }

        if (m_ioAction == IOTaskAction::Send && this->get_ideal_send_backlog() > m_sendBytesInflight)
        {
            const auto max_bytes_to_send = this->get_ideal_send_backlog() - m_sendBytesInflight;
            const auto return_task(this->tracked_task(m_ioAction, max_bytes_to_send));
            m_sendBytesInflight += return_task.buffer_length;
            return return_task;
        }

        return ctsIOTask();
    }
    ctsIOPatternProtocolError ctsIOPatternPush::completed_task(const ctsIOTask& task, unsigned long completed_bytes) noexcept
    {
        if (IOTaskAction::Send == task.ioAction)
        {
            this->stats.bytes_sent.add(completed_bytes);
            m_sendBytesInflight -= completed_bytes;
        }
        else
        {
            this->stats.bytes_recv.add(completed_bytes);
            ++m_recvNeeded;
        }

        return ctsIOPatternProtocolError::NoError;
    }


    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///     - PushPull Pattern
    ///    -- TCP-only
    ///    -- The client pushes data in 'segments'
    ///    -- The server pulls data in 'segments'
    ///    -- At each segment, roles swap (pusher/puller)
    ///
    ///    -- Currently not supporting concurrent IO via ctsConfig::GetConcurrentIoCount()
    ///       as we need precise controls when to flip from send -> recv -> send
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOPatternPushPull::ctsIOPatternPushPull() :
        ctsIOPatternStatistics(1), // currently not supporting >1 concurrent IO requests
        m_pushSegmentSize(ctsConfig::Settings->PushBytes),
        m_pullSegmentSize(ctsConfig::Settings->PullBytes),
        m_intraSegmentTransfer(0ULL),
        m_listening(ctsConfig::IsListening()),
        m_ioNeeded(true),
        m_sending(!ctsConfig::IsListening()) // start with clients sending, servers receiving
    {
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// virtual methods from the base class:
    /// - assumes will be called under a CS from the base class
    ///
    /// tracks if sending or receiving in the IO flow
    ///
    /// Return an empty task when no more IO is needed
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOTask ctsIOPatternPushPull::next_task() noexcept
    {
        ctsUnsignedLong segment_size;
        if (m_listening)
        {
            // server role is opposite client
            segment_size = (m_sending) ? m_pullSegmentSize : m_pushSegmentSize;
        }
        else
        {
            segment_size = (m_sending) ? m_pushSegmentSize : m_pullSegmentSize;
        }

        FAIL_FAST_IF_MSG(
            m_intraSegmentTransfer >= segment_size,
            "Invalid ctsIOPatternPushPull state: intra_segment_transfer (%lu), segment_size (%lu)",
            static_cast<unsigned long>(m_intraSegmentTransfer), static_cast<unsigned long>(segment_size));

        if (m_ioNeeded)
        {
            m_ioNeeded = false;

            if (m_sending)
            {
                return this->tracked_task(
                    IOTaskAction::Send,
                    segment_size - m_intraSegmentTransfer);
            }
            else
            {
                return this->tracked_task(
                    IOTaskAction::Recv,
                    segment_size - m_intraSegmentTransfer);
            }
        }
        else
        {
            return ctsIOTask();
        }
    }
    ctsIOPatternProtocolError ctsIOPatternPushPull::completed_task(const ctsIOTask& task, unsigned long current_transfer) noexcept
    {
        if (IOTaskAction::Send == task.ioAction)
        {
            this->stats.bytes_sent.add(current_transfer);
        }
        else
        {
            this->stats.bytes_recv.add(current_transfer);
        }

        m_ioNeeded = true;
        m_intraSegmentTransfer += current_transfer;

        ctsUnsignedLong segment_size;
        if (m_listening)
        {
            // server role is opposite client
            segment_size = (m_sending) ? m_pullSegmentSize : m_pushSegmentSize;
        }
        else
        {
            segment_size = (m_sending) ? m_pushSegmentSize : m_pullSegmentSize;
        }

        FAIL_FAST_IF_MSG(
            m_intraSegmentTransfer > segment_size,
            "Invalid ctsIOPatternPushPull state: intra_segment_transfer (%lu), segment_size (%lu)",
            static_cast<unsigned long>(m_intraSegmentTransfer), static_cast<unsigned long>(segment_size));

        if (segment_size == m_intraSegmentTransfer)
        {
            m_sending = !m_sending;
            m_intraSegmentTransfer = 0;
        }

        return ctsIOPatternProtocolError::NoError;
    }


    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///     - Concurrent Pattern
    ///    -- TCP-only
    ///    -- The client and server both send and receive data concurrently
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOPatternDuplex::ctsIOPatternDuplex() :
        ctsIOPatternStatistics(ctsConfig::Settings->PrePostRecvs),
        m_remainingSendBytes(0),
        m_remainingRecvBytes(0),
        m_recvNeeded(ctsConfig::Settings->PrePostRecvs),
        m_sendBytesInflight(0)
    {
        // max transfer bytes must be an even # so send bytes and recv bytes are balanced
        auto current_max_transfer = this->get_total_transfer();
        if (current_max_transfer % 2 != 0)
        {
            this->set_total_transfer(++current_max_transfer);
        }

        m_remainingSendBytes = current_max_transfer / 2;
        m_remainingRecvBytes = m_remainingSendBytes;

        FAIL_FAST_IF_MSG(
            (m_remainingSendBytes + m_remainingRecvBytes) != this->get_total_transfer(),
            "ctsIOPatternDuplex: internal failure - send_bytes (%llu) + recv_bytes (%llu) must equal total bytes (%llu)",
            static_cast<ULONGLONG>(m_remainingSendBytes),
            static_cast<ULONGLONG>(m_remainingRecvBytes),
            static_cast<ULONGLONG>(this->get_total_transfer()));
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// virtual methods from the base class:
    /// - assumes will be called under a CS from the base class
    ///
    /// tracks if sending or receiving in the IO flow
    ///
    /// Return an empty task when no more IO is needed
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOTask ctsIOPatternDuplex::next_task() noexcept
    {
        ctsIOTask return_task;

        // since we can have multiple receives in flight, must also check that we have remaining_recv_bytes
        if (m_remainingRecvBytes > 0 && m_recvNeeded > 0)
        {
            // for very large transfers, we need to ensure our SafeInt<long long> doesn't overflow when it's cast 
            // to unsigned long when passed to tracked_task()
            const ctsUnsignedLong max_remaining_bytes = m_remainingRecvBytes > MAXLONG ?
                MAXLONG :
                static_cast<unsigned long>(m_remainingRecvBytes);
            return_task = this->tracked_task(IOTaskAction::Recv, max_remaining_bytes);
            // for tracking purposes, assume that this recv *might* end up receiving the entire buffer size
            // - only on completion will we adjust to the actual # of bytes received
            m_remainingRecvBytes -= return_task.buffer_length;
            --m_recvNeeded;

        }
        else if (m_remainingSendBytes > 0 && this->get_ideal_send_backlog() > m_sendBytesInflight)
        {
            // for very large transfers, we need to ensure our SafeInt<long long> doesn't overflow when it's cast 
            // to unsigned long when passed to tracked_task()
            const ctsUnsignedLong max_remaining_bytes = m_remainingSendBytes > MAXLONG ?
                MAXLONG :
                static_cast<unsigned long>(m_remainingSendBytes);
            ctsUnsignedLong max_send = this->get_ideal_send_backlog() - m_sendBytesInflight;
            if (max_send > max_remaining_bytes)
            {
                max_send = max_remaining_bytes;
            }
            return_task = this->tracked_task(IOTaskAction::Send, max_send);
            m_remainingSendBytes -= return_task.buffer_length;
            m_sendBytesInflight += return_task.buffer_length;
        }
        else
        {
            // no IO needed now: return the default task
        }

        return return_task;
    }
    ctsIOPatternProtocolError ctsIOPatternDuplex::completed_task(const ctsIOTask& task, unsigned long completed_bytes) noexcept
    {
        // ReSharper disable once CppIncompleteSwitchStatement
        switch (task.ioAction)
        {
            case IOTaskAction::Send:
                this->stats.bytes_sent.add(completed_bytes);
                m_sendBytesInflight -= completed_bytes;

                // first, we need to adjust the total back from our over-subscription guard when this task was created
                m_remainingSendBytes += task.buffer_length;
                // then we need to subtract back out the actual number of bytes sent
                m_remainingSendBytes -= completed_bytes;
                break;

            case IOTaskAction::Recv:
                this->stats.bytes_recv.add(completed_bytes);
                ++m_recvNeeded;

                // first, we need to adjust the total back from our over-subscription guard when this task was created
                m_remainingRecvBytes += task.buffer_length;
                // then we need to subtract back out the actual number of bytes received
                m_remainingRecvBytes -= completed_bytes;
                break;

            case IOTaskAction::None:
            case IOTaskAction::GracefulShutdown:
            case IOTaskAction::HardShutdown:
            case IOTaskAction::Abort:
            case IOTaskAction::FatalAbort:
            default:;
            // fall through to return NoError
        }

        return ctsIOPatternProtocolError::NoError;
    }


    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///     - ctsIOPatternMediaStream (Server) Pattern
    ///    -- UDP-only
    ///    -- The server sends data at a specified rate
    ///    -- The client receives data continuously
    ///       After a 'buffer period' of data has been received,
    ///       The client starts as timer to 'process' a time-slice of data
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOPatternMediaStreamServer::ctsIOPatternMediaStreamServer() :
        ctsIOPatternStatistics(1), // the pattern will use the recv writeable-buffer for sending a connection ID
        m_frameSizeBytes(ctsConfig::GetMediaStream().FrameSizeBytes),
        m_currentFrameRequested(0UL),
        m_currentFrameCompleted(0UL),
        m_frameRateFps(ctsConfig::GetMediaStream().FramesPerSecond),
        m_currentFrame(1UL),
        m_baseTimeMilliseconds(0LL),
        m_state(ServerState::NotStarted)
    {
        PrintDebugInfo(L"\t\tctsIOPatternMediaStreamServer - frame rate in milliseconds per frame : %lld\n", static_cast<long long>(1000UL / m_frameRateFps));
    }
    // required virtual functions
    ctsIOTask ctsIOPatternMediaStreamServer::next_task() noexcept
    {
        ctsIOTask return_task;
        switch (m_state)
        {
            case ServerState::NotStarted:
                // get a writable buffer (ie. Recv), then update the fields in the task for the connection_id
                return_task = ctsMediaStreamMessage::MakeConnectionIdTask(
                    this->untracked_task(IOTaskAction::Recv, UdpDatagramConnectionIdHeaderLength),
                    this->connection_id());
                m_state = ServerState::IdSent;
                break;

            case ServerState::IdSent:
                m_baseTimeMilliseconds = ctTimer::ctSnapQpcInMillis();
                m_state = ServerState::IoStarted;
                // fall-through
            case ServerState::IoStarted:
                if (m_currentFrameRequested < m_frameSizeBytes)
                {
                    return_task = this->tracked_task(IOTaskAction::Send, m_frameSizeBytes);
                    // calculate the future time to initiate the IO
                    // - then subtract the start time to give the difference
                    return_task.time_offset_milliseconds =
                        m_baseTimeMilliseconds
                        + static_cast<long long>(m_currentFrame) * 1000LL / static_cast<long long>(m_frameRateFps)
                        - ctTimer::ctSnapQpcInMillis();

                    m_currentFrameRequested += return_task.buffer_length;
                }
                break;
        }
        return return_task;
    }
    ctsIOPatternProtocolError ctsIOPatternMediaStreamServer::completed_task(const ctsIOTask& task, unsigned long current_transfer) noexcept
    {
        if (task.buffer_type != ctsIOTask::BufferType::UdpConnectionId)
        {
            const ctsUnsignedLong current_transfer_bits = current_transfer * 8UL;

            ctsConfig::Settings->UdpStatusDetails.bits_received.add(current_transfer_bits);
            this->stats.bits_received.add(current_transfer_bits);

            m_currentFrameCompleted += current_transfer;
            if (m_currentFrameCompleted == m_frameSizeBytes)
            {
                ++m_currentFrame;
                m_currentFrameRequested = 0UL;
                m_currentFrameCompleted = 0UL;
            }
        }
        return ctsIOPatternProtocolError::NoError;
    }
} //namespace
