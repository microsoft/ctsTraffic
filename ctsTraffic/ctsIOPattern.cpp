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

// ctl headers
#include <ctSocketExtensions.hpp>
#include <ctScopeGuard.hpp>
#include <ctLocks.hpp>
#include <ctTimer.hpp>

// project headers
#include "ctsMediaStreamProtocol.hpp"
#include "ctsIOBuffers.hpp"


namespace ctsTraffic {

    using namespace ctl;
    using namespace std;

    static const unsigned long BufferPatternSize = 0xffff + 0x1; // fill from 0x0000 to 0xffff
    static unsigned char BufferPattern[BufferPatternSize * 2]; // * 2 as unsigned short values are twice as large as unsigned char

    /// SharedBuffer is a larger buffer with many copies of BufferPattern in it. This is what the various IO patterns
    /// will be memcmp'ing against for validity checks.
    ///
    /// The buffers' sizes will be the constant "BufferPatternSize + ctsConfig::GetMaxBufferSize()", but we
    /// need to wait for input parsing before we can set that.
    // TODO: expand the comment to explain the logic working with offsets

    static INIT_ONCE s_IOPatternInitializer = INIT_ONCE_STATIC_INIT;
    static char* s_WriteableSharedBuffer = nullptr;
    static char* s_ProtectedSharedBuffer = nullptr;
    static unsigned long s_SharedBufferSize = 0;
    static RIO_BUFFERID s_SharedBufferId = RIO_INVALID_BUFFERID;

    static const char* s_CompletionMessage = "DONE";
    static const unsigned long s_CompletionMessageSize = 4;
    static const unsigned long s_FinBufferSize = 4; // just 4 bytes for the FIN
    static char s_FinBuffer[s_FinBufferSize];

    BOOL CALLBACK InitOnceIOPatternCallback(PINIT_ONCE, PVOID, PVOID *) NOEXCEPT
    {
        // first create the buffer pattern
        for (unsigned long fill_slot = 0; fill_slot < BufferPatternSize; ++fill_slot)
        {
            *reinterpret_cast<unsigned short*>(&BufferPattern[fill_slot * 2]) = static_cast<unsigned short>(fill_slot);
        }

        s_SharedBufferSize = BufferPatternSize + ctsConfig::GetMaxBufferSize() + s_CompletionMessageSize;

        s_ProtectedSharedBuffer = reinterpret_cast<char*>(::VirtualAlloc(nullptr, s_SharedBufferSize, MEM_COMMIT, PAGE_READWRITE));
        if (!s_ProtectedSharedBuffer) {
            ctAlwaysFatalCondition(L"VirtualAlloc alloc failed: %u", ::GetLastError());
        }

        s_WriteableSharedBuffer = reinterpret_cast<char*>(::VirtualAlloc(nullptr, s_SharedBufferSize, MEM_COMMIT, PAGE_READWRITE));
        if (!s_WriteableSharedBuffer) {
            ctAlwaysFatalCondition(L"VirtualAlloc alloc failed: %u", ::GetLastError());
        }

        // fill in this allocated buffer while we can write to it
        char* protected_destination = s_ProtectedSharedBuffer;
        char* writeable_destination = s_WriteableSharedBuffer;
        unsigned long write_size_remaining = s_SharedBufferSize;
        while (write_size_remaining > 0) {
            unsigned long bytes_to_write = (write_size_remaining > BufferPatternSize) ? BufferPatternSize : write_size_remaining;

            auto memerror = ::memcpy_s(protected_destination, write_size_remaining, BufferPattern, bytes_to_write);
            ctFatalCondition(
                memerror != 0,
                L"memcpy_s(%p, %lu, %p, %lu) failed : %d",
                protected_destination, write_size_remaining, BufferPattern, bytes_to_write, memerror);

            memerror = ::memcpy_s(writeable_destination, write_size_remaining, BufferPattern, bytes_to_write);
            ctFatalCondition(
                memerror != 0,
                L"memcpy_s(%p, %lu, %p, %lu) failed : %d",
                writeable_destination, write_size_remaining, BufferPattern, bytes_to_write, memerror);

            protected_destination += bytes_to_write;
            writeable_destination += bytes_to_write;
            write_size_remaining -= bytes_to_write;
        }
        // set the final 4 bytes to the DONE message for the send buffer
        ::memcpy_s(
            s_ProtectedSharedBuffer + s_SharedBufferSize - s_CompletionMessageSize,
            s_CompletionMessageSize,
            s_CompletionMessage,
            s_CompletionMessageSize);
        ::memcpy_s(
            s_WriteableSharedBuffer + s_SharedBufferSize - s_CompletionMessageSize,
            s_CompletionMessageSize,
            s_CompletionMessage,
            s_CompletionMessageSize);

        // guarantee noone will write to our s_ProtectedSharedBuffer
        DWORD old_setting;
        if (!::VirtualProtect(s_ProtectedSharedBuffer, s_SharedBufferSize, PAGE_READONLY, &old_setting)) {
            ctAlwaysFatalCondition(L"VirtualProtect failed: %u", ::GetLastError());
        }

        // establish a RIO ID for the writable shared buffer if we're using RIO APIs
        if (ctsConfig::Settings->SocketFlags & WSA_FLAG_REGISTERED_IO) {
            s_SharedBufferId = ctRIORegisterBuffer(s_WriteableSharedBuffer, s_SharedBufferSize);
            if (RIO_INVALID_BUFFERID == s_SharedBufferId) {
                ctAlwaysFatalCondition(L"RIORegisterBuffer failed: %d", ::WSAGetLastError());
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
        switch (ctsConfig::Settings->IoPattern) {
        case ctsConfig::IoPatternType::Pull:
            return make_shared<ctsIOPatternPull>();

        case ctsConfig::IoPatternType::Push:
            return make_shared<ctsIOPatternPush>();

        case ctsConfig::IoPatternType::PushPull:
            return make_shared<ctsIOPatternPushPull>();

        case ctsConfig::IoPatternType::Duplex:
            return make_shared<ctsIOPatternDuplex>();

        case ctsConfig::IoPatternType::MediaStream:
            if (ctsConfig::IsListening()) {
                return make_shared<ctsIOPatternMediaStreamServer>();
            } else {
                // if is not listening, running as a client
                return make_shared<ctsIOPatternMediaStreamClient>();
            }

        default:
            ctAlwaysFatalCondition(L"ctsIOPattern::MakeIOPattern - Unknown IoPattern specified (%d)", ctsConfig::Settings->IoPattern);
            return nullptr;
        }
    }
    char* ctsIOPattern::AccessSharedBuffer() NOEXCEPT
    {
        // this init-once call is no-fail
        (void) ::InitOnceExecuteOnce(&s_IOPatternInitializer, InitOnceIOPatternCallback, nullptr, nullptr);
        return s_ProtectedSharedBuffer;
    }

    ctsIOPattern::ctsIOPattern(unsigned long _recv_count) :
        cs(),
        recv_buffer_free_list(),
        recv_buffer_container(),
        callback(nullptr),
        pattern_state(),
        send_pattern_offset(0),
        recv_pattern_offset(0),
        recv_rio_bufferid(RIO_INVALID_BUFFERID),
        // (bytes/sec) * (1 sec/1000 ms) * (x ms/Quantum) == (bytes/quantum)
        bytes_sending_per_quantum(ctsConfig::GetTcpBytesPerSecond() * static_cast<unsigned long long>(ctsConfig::Settings->TcpBytesPerSecondPeriod) / 1000LL),
        bytes_sending_this_quantum(0LL),
        quantum_start_time_ms(ctTimer::snap_qpc_as_msec()),
        last_error(ctsStatusIORunning)
    {
        ctFatalCondition(
            ctsConfig::Settings->UseSharedBuffer && ctsConfig::Settings->ShouldVerifyBuffers,
            L"Cannot use a shared buffer across connections and still verify buffers");

        // this init-once call is no-fail
        (void) ::InitOnceExecuteOnce(&s_IOPatternInitializer, InitOnceIOPatternCallback, nullptr, nullptr);

        if (!::InitializeCriticalSectionEx(&cs, 4000, 0)) {
            throw ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsIOPattern", false);
        }
        ctlScopeGuard(deleteCSonError, { ::DeleteCriticalSection(&cs); });

        // if TCP, will always need a recv buffer for the final FIN 
        if ((_recv_count > 0) || (ctsConfig::Settings->Protocol == ctsConfig::ProtocolType::TCP)) {
            // recv will only use the same shared buffer when the user specified to do so on the cmdline
            if (ctsConfig::Settings->UseSharedBuffer) {
                if (_recv_count > 0) {
                    for (unsigned long free_list = 0; free_list < _recv_count; ++free_list) {
                        recv_buffer_free_list.push_back(s_WriteableSharedBuffer);
                    }
                    // if using RIO, can share the same BufferId when not needing to validate the buffer
                    recv_rio_bufferid = s_SharedBufferId;
                } else {
                    // just use the shared buffer to capture the ACK's since recv_count == 0
                    recv_buffer_free_list.push_back(s_WriteableSharedBuffer);
                    recv_rio_bufferid = s_SharedBufferId;
                }
            } else {
                if (_recv_count > 0) {
                    recv_buffer_container.resize(ctsConfig::GetMaxBufferSize() * _recv_count);
                    char* raw_recv_buffer = &recv_buffer_container[0];
                    for (unsigned long free_list = 0; free_list < _recv_count; ++free_list) {
                        recv_buffer_free_list.push_back(raw_recv_buffer + static_cast<size_t>(free_list * ctsConfig::GetMaxBufferSize()));
                    }
                } else {
                    // just use the shared buffer to capture the FIN since recv_count == 0
                    recv_buffer_free_list.push_back(s_WriteableSharedBuffer);
                    recv_rio_bufferid = s_SharedBufferId;
                }
            }

            if (ctsConfig::Settings->SocketFlags & WSA_FLAG_REGISTERED_IO &&
                recv_rio_bufferid != s_SharedBufferId) {
                ctFatalCondition(_recv_count > 1, L"Current not supporting >1 concurrent IO requests with RIO");
                recv_rio_bufferid = ctRIORegisterBuffer(recv_buffer_free_list[0], static_cast<DWORD>(ctsConfig::GetMaxBufferSize()));
                if (RIO_INVALID_BUFFERID == recv_rio_bufferid) {
                    throw ctException(::WSAGetLastError(), L"RIORegisterBuffer", L"ctsIOPattern", false);
                }
            }
        }

        // init was successful - don't delete
        deleteCSonError.dismiss();
    }


    ctsIOPattern::~ctsIOPattern() NOEXCEPT
    {
        if (recv_rio_bufferid != RIO_INVALID_BUFFERID && recv_rio_bufferid != s_SharedBufferId) {
            ctRIODeregisterBuffer(recv_rio_bufferid);
        }

        ::DeleteCriticalSection(&cs);
    }

    ctsIOTask ctsIOPattern::initiate_io() NOEXCEPT
    {
        // make sure stats starts tracking IO at the first IO request
        this->start_stats();

        ctAutoReleaseCriticalSection local_cs(&this->cs);
        ctsIOTask return_task;
        switch (this->pattern_state.get_next_task()) {
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
            return_task.buffer_length = s_CompletionMessageSize;
            return_task.buffer_offset = s_SharedBufferSize - s_CompletionMessageSize;
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
            return_task.buffer_length = s_CompletionMessageSize;
            return_task.buffer_offset = s_SharedBufferSize - s_CompletionMessageSize;
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

        this->pattern_state.notify_next_task(return_task);
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
    ctsIOStatus ctsIOPattern::complete_io(const ctsIOTask& _original_task, unsigned long _current_transfer, unsigned long _status_code) NOEXCEPT
    {
        //
        // Take the object lock before touching internal values
        //
        ctAutoReleaseCriticalSection local_cs(&this->cs);

        // Only add the recv buffer back if it was one of our listed recv buffers
        if (ctsIOTask::BufferType::Tracked == _original_task.buffer_type) {
            this->recv_buffer_free_list.push_back(_original_task.buffer);
        }

        // preserve the previous task
        bool task_was_more_io = this->pattern_state.is_current_task_more_io();

        switch (_original_task.ioAction) {
        case IOTaskAction::None:
            // ignore completions for tasks on None
            break;

        case IOTaskAction::FatalAbort:
            ctsConfig::PrintDebug(L"\t\tctsIOPattern : completing a FatalAbort\n");
            this->update_last_error(ctsStatusErrorNotAllDataTransferred);
            break;

        case IOTaskAction::Abort:
            ctsConfig::PrintDebug(L"\t\tctsIOPattern : completing an Abort\n");
            break;

        case IOTaskAction::GracefulShutdown:
            // Fall-through to be processed like send or recv IO
            ctsConfig::PrintDebug(L"\t\tctsIOPattern : completing a GracefulShutdown\n");
        case IOTaskAction::HardShutdown:
            // Fall-through to be processed like send or recv IO
            ctsConfig::PrintDebug(L"\t\tctsIOPattern : completing a HardShutdown\n");
        case IOTaskAction::Recv:
            //
            // Fall-through to Send - where the IO will be processed
            //
        case IOTaskAction::Send:
            bool verify_io = true;
            if (ctsIOTask::BufferType::TcpConnectionId == _original_task.buffer_type) {
                //
                // not verifying the IO buffer if this is the connection id request
                // - but must complete the task to update the protocol
                //
                verify_io = false;

                if (_status_code != NO_ERROR) {
                    this->update_last_error(_status_code);
                } else {
                    if (IOTaskAction::Recv == _original_task.ioAction) {
                        // save off the connection ID when we receive it
                        if (!ctsIOBuffers::SetConnectionId(this->connection_id(), _original_task, _current_transfer)) {
                            this->update_last_error(ctsStatusErrorDataDidNotMatchBitPattern);
                        }
                    }

                    // process the TCP protocol state machine in pattern_state after receiving the connection id
                    this->update_last_protocol_error(this->pattern_state.completed_task(_original_task, _current_transfer));

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
                if (IOTaskAction::Recv == _original_task.ioAction && this->pattern_state.is_completed()) {
                    ctsConfig::PrintDebug(L"\t\tctsIOPattern : Recv failed after the pattern completed (error %u)\n", _status_code);
                } else {
                    auto current_status = this->update_last_error(_status_code);
                    if (current_status != ctsStatusIORunning) {
                        ctsConfig::PrintDebug(L"\t\tctsIOPattern : Recv failed before the pattern completed (error %u, current status %u)\n", _status_code, current_status);
                        verify_io = false;
                    }
                }
            }

            if (verify_io) {
                //
                // IO succeeded - update state machine with the completed task if this task had IO
                //
                auto pattern_status = this->pattern_state.completed_task(_original_task, _current_transfer);
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
                    _original_task.ioAction == IOTaskAction::Recv &&
                    _original_task.track_io &&
                    (ctsIOPatternProtocolError::SuccessfullyCompleted == pattern_status || ctsIOPatternProtocolError::NoError == pattern_status)) {

                    ctFatalCondition(
                        _original_task.expected_pattern_offset != this->recv_pattern_offset,
                        L"ctsIOPattern::complete_io() : ctsIOTask (%p) expected_pattern_offset (%lu) does not match the current pattern_offset (%Iu)",
                        &_original_task, _original_task.expected_pattern_offset, static_cast<size_t>(this->recv_pattern_offset));

                    if (!this->verify_buffer(_original_task, _current_transfer)) {
                        this->update_last_error(ctsStatusErrorDataDidNotMatchBitPattern);
                    }

                    this->recv_pattern_offset += _current_transfer;
                    this->recv_pattern_offset %= BufferPatternSize;
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
        if ((_original_task.ioAction != IOTaskAction::None) &&
            (NO_ERROR == _status_code)) {

            if (IOTaskAction::Send == _original_task.ioAction) {
                ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_current_transfer);
            } else {
                ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_current_transfer);
            }
            // only complete tasks that were requested
            if (task_was_more_io) {
                this->update_last_protocol_error(
                    this->completed_task(_original_task, _current_transfer));
            }
        }
        //
        // If the state machine has verified the connection has completed, 
        // - set the last error to zero in case it was not already set to an error
        //   but do this *after* the other possible failure points were checked
        //
        if (this->pattern_state.is_completed()) {
            this->update_last_error(NO_ERROR);
            this->end_stats();
        }

        return this->current_status();
    }

    ctsIOTask ctsIOPattern::tracked_task(IOTaskAction _action, unsigned long _max_transfer) NOEXCEPT
    {
        ctAutoReleaseCriticalSection local_cs(&this->cs);
        ctsIOTask return_task(this->new_task(_action, _max_transfer));
        return_task.track_io = true;
        return return_task;
    }

    ctsIOTask ctsIOPattern::untracked_task(IOTaskAction _action, unsigned long _max_transfer) NOEXCEPT
    {
        ctAutoReleaseCriticalSection local_cs(&this->cs);
        ctsIOTask return_task(this->new_task(_action, _max_transfer));
        return_task.track_io = false;
        return return_task;
    }

    ctsIOTask ctsIOPattern::new_task(IOTaskAction _action, unsigned long _max_transfer) NOEXCEPT
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
            this->pattern_state.get_remaining_transfer());
        //
        // second: if the protocol specified a ceiling, recalculate given their ceiling
        //
        if ((_max_transfer > 0) && (_max_transfer < new_buffer_size)) {
            new_buffer_size = _max_transfer;
        }
        //
        // guard against hitting a 32-bit overflow
        //
        ctFatalCondition(
            (new_buffer_size > MAXDWORD),
            L"ctsIOPattern internal error: next buffer size (%llu) is greater than MAXDWORD (%u)",
            static_cast<ULONGLONG>(new_buffer_size), MAXDWORD);
        //
        // build the next IO request with a properly calculated buffer size
        // Send must specify the offset because we must align the patterns that we send
        // Recv must not specify an offset because will always use the entire buffer for the recv
        //
        ctsIOTask return_task;
        if (IOTaskAction::Send == _action) {
            //
            // check to see if the send needs to be deferred into the future
            //
            if (this->bytes_sending_per_quantum > 0) {
                auto current_time_ms(ctTimer::snap_qpc_as_msec());
                if (this->bytes_sending_this_quantum < this->bytes_sending_per_quantum) {
                    // adjust bytes_sending_this_quantum
                    this->bytes_sending_this_quantum += new_buffer_size;

                    // no need to adjust quantum_start_time_ms unless we skipped into a new quantum
                    // (meaning the previous quantum had not filled the max bytes for that quantum)
                    if (current_time_ms >(this->quantum_start_time_ms + ctsConfig::Settings->TcpBytesPerSecondPeriod)) {
                        // current time shows it's now beyond this quantum timeframe
                        // - once we see how many quantums we have skipped forward, move our quantum start time to the quantum we are actually in
                        // - then adjust the number of bytes we are to send this quantum by how many quantum we just skipped
                        auto quantums_skipped_since_last_send = (current_time_ms - this->quantum_start_time_ms) / ctsConfig::Settings->TcpBytesPerSecondPeriod;
                        this->quantum_start_time_ms += quantums_skipped_since_last_send * ctsConfig::Settings->TcpBytesPerSecondPeriod;

                        // we have to be careful making this adjustment since the remainingbytes this quantum could be very small
                        // - we only subtract out if the number of bytes skipped is >= bytes actually skipped
                        auto bytes_to_adjust = this->bytes_sending_per_quantum * quantums_skipped_since_last_send;
                        if (bytes_to_adjust > this->bytes_sending_this_quantum) {
                            this->bytes_sending_this_quantum = 0;
                        } else {
                            this->bytes_sending_this_quantum -= bytes_to_adjust;
                        }
                    }
                    // update the return task for when to schedule the send
                    return_task.time_offset_milliseconds = 0LL;
                } else {
                    // we have sent more than required for this quantum
                    // - check if this fullfilled future quantums as well
                    auto quantum_ahead_to_schedule = static_cast<unsigned long>(this->bytes_sending_this_quantum / this->bytes_sending_per_quantum);

                    // ms_for_quantums_to_skip = the # of quantum beyond the current quantum that will be skipped
                    // - when we have already sent at least 1 additional quantum of bytes
                    ctsSignedLongLong ms_for_quantums_to_skip = (quantum_ahead_to_schedule - 1) * ctsConfig::Settings->TcpBytesPerSecondPeriod;

                    // carry forward extra bytes from quantums that will be filled by the bytes we have already sent
                    // (including the current quantum)
                    // then adding the bytes we're about to send
                    this->bytes_sending_this_quantum -= this->bytes_sending_per_quantum * quantum_ahead_to_schedule;
                    this->bytes_sending_this_quantum += new_buffer_size;

                    // update the return task for when to schedule the send
                    // first, calculate the time to get to the end of this time quantum
                    // - only adjust if the current time isn't already outside this quantum
                    if (current_time_ms < this->quantum_start_time_ms + ctsConfig::Settings->TcpBytesPerSecondPeriod) {
                        return_task.time_offset_milliseconds = (this->quantum_start_time_ms + ctsConfig::Settings->TcpBytesPerSecondPeriod) - current_time_ms;
                    }
                    // then add in any quantum we need to skip
                    return_task.time_offset_milliseconds += ms_for_quantums_to_skip;

                    // finally, adjust quantum_start_time_ms to the next quantum which IO will complete
                    this->quantum_start_time_ms += ms_for_quantums_to_skip + ctsConfig::Settings->TcpBytesPerSecondPeriod;
                }
            } else {
                return_task.time_offset_milliseconds = 0LL;
            }

            return_task.ioAction = IOTaskAction::Send;
            return_task.buffer = s_ProtectedSharedBuffer;
            return_task.rio_bufferid = s_SharedBufferId;
            return_task.buffer_length = static_cast<unsigned long>(new_buffer_size);
            return_task.buffer_offset = static_cast<unsigned long>(this->send_pattern_offset);
            return_task.expected_pattern_offset = 0; // The sender shouldn't be validating this
            return_task.buffer_type = ctsIOTask::BufferType::Static;

            // now that we are indicating this buffer to send, increment the offset for the next send request
            this->send_pattern_offset += new_buffer_size;
            this->send_pattern_offset %= BufferPatternSize;

            ctFatalCondition(
                this->send_pattern_offset >= BufferPatternSize,
                L"this->pattern_offset being too large (larger than BufferPatternSize %lu) means we might walk off the end of our shared buffer (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                BufferPatternSize, this);
            ctFatalCondition(
                return_task.buffer_length + return_task.buffer_offset > s_SharedBufferSize,
                L"return_task (%p) for a Send request is specifying a buffer that is larger than the static SharedBufferSize (%lu) (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                &return_task, s_SharedBufferSize, this);

        } else {
            ctFatalCondition(
                this->recv_buffer_free_list.empty(),
                L"recv_buffer_free_list is empty for a new Recv task  (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)", this);

            return_task.ioAction = IOTaskAction::Recv;
            return_task.buffer = *this->recv_buffer_free_list.rbegin();
            this->recv_buffer_free_list.pop_back();
            return_task.buffer_type = ctsIOTask::BufferType::Tracked;

            return_task.rio_bufferid = this->recv_rio_bufferid;
            return_task.buffer_length = static_cast<unsigned long>(new_buffer_size);
            return_task.buffer_offset = 0; // always recv to the beginning of the buffer
            return_task.expected_pattern_offset = static_cast<unsigned long>(this->recv_pattern_offset);

            ctFatalCondition(
                this->recv_pattern_offset >= BufferPatternSize,
                L"pattern_offset being too large means we might walk off the end of our shared buffer (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)", this);
            ctFatalCondition(
                return_task.buffer_length + return_task.buffer_offset > new_buffer_size,
                L"return_task (%p) for a Recv request is specifying a buffer that is larger than buffer_size (%lu) (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                &return_task, static_cast<unsigned long>(new_buffer_size), this);
        }

        return return_task;
    }
    bool ctsIOPattern::verify_buffer(const ctsIOTask& _original_task, unsigned long _transferred_bytes) NOEXCEPT
    {
        // only doing deep verification if the user asked us to
        if (!ctsConfig::Settings->ShouldVerifyBuffers) {
            return true;
        }
        //
        // We're using RtlCompareMemory instead of memcmp because it returns the first offset at which the buffers differ,
        // which is more useful than memcmp's "sign of the difference between the first two differing elements"
        //
        auto pattern_buffer = s_ProtectedSharedBuffer + _original_task.expected_pattern_offset;
        size_t length_matched = ::RtlCompareMemory(
            pattern_buffer,
            _original_task.buffer + _original_task.buffer_offset,
            _transferred_bytes);
        if (length_matched != _transferred_bytes) {
            ctsConfig::PrintErrorInfo(
                L"[%.3f] ctsIOPattern found data corruption: detected an invalid byte pattern in the returned buffer (length %u): "
                L"buffer received (%p), expected buffer pattern (%p) - mismatch from expected pattern at offset (%Iu) [expected 32-bit value '0x%x' didn't match '0x%x']\n",
                ctsConfig::GetStatusTimeStamp(),
                _transferred_bytes,
                _original_task.buffer + _original_task.buffer_offset,
                pattern_buffer,
                length_matched,
                pattern_buffer[length_matched],
                *(_original_task.buffer + _original_task.buffer_offset + length_matched));
        }

        return (length_matched == _transferred_bytes);
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
        io_action(ctsConfig::IsListening() ? IOTaskAction::Send : IOTaskAction::Recv),
        recv_needed(ctsConfig::IsListening() ? 0 : ctsConfig::Settings->PrePostRecvs),
        send_bytes_inflight(0)
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
    ctsIOTask ctsIOPatternPull::next_task() NOEXCEPT
    {
        if (this->io_action == IOTaskAction::Recv && this->recv_needed > 0) {
            --this->recv_needed;
            return this->tracked_task(this->io_action);
        } else if (this->io_action == IOTaskAction::Send && this->get_ideal_send_backlog() > this->send_bytes_inflight) {
            ctsUnsignedLong max_bytes_to_send = this->get_ideal_send_backlog() - this->send_bytes_inflight;
            auto return_task(this->tracked_task(this->io_action, max_bytes_to_send));
            this->send_bytes_inflight += return_task.buffer_length;
            return return_task;
        } else {
            return ctsIOTask();
        }
    }
    ctsIOPatternProtocolError ctsIOPatternPull::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) NOEXCEPT
    {
        if (IOTaskAction::Send == _task.ioAction) {
            this->stats.bytes_sent.add(_completed_bytes);
            this->send_bytes_inflight -= _completed_bytes;
        } else {
            this->stats.bytes_recv.add(_completed_bytes);
            ++this->recv_needed;
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
        io_action(ctsConfig::IsListening() ? IOTaskAction::Recv : IOTaskAction::Send),
        recv_needed(ctsConfig::IsListening() ? ctsConfig::Settings->PrePostRecvs : 0),
        send_bytes_inflight(0)
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
    ctsIOTask ctsIOPatternPush::next_task() NOEXCEPT
    {
        if (this->io_action == IOTaskAction::Recv && this->recv_needed > 0) {
            --this->recv_needed;
            return this->tracked_task(this->io_action);
        } else if (this->io_action == IOTaskAction::Send && this->get_ideal_send_backlog() > send_bytes_inflight) {
            ctsUnsignedLong max_bytes_to_send = this->get_ideal_send_backlog() - send_bytes_inflight;
            auto return_task(this->tracked_task(this->io_action, max_bytes_to_send));
            send_bytes_inflight += return_task.buffer_length;
            return return_task;
        } else {
            return ctsIOTask();
        }
    }
    ctsIOPatternProtocolError ctsIOPatternPush::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) NOEXCEPT
    {
        if (IOTaskAction::Send == _task.ioAction) {
            this->stats.bytes_sent.add(_completed_bytes);
            send_bytes_inflight -= _completed_bytes;
        } else {
            this->stats.bytes_recv.add(_completed_bytes);
            ++this->recv_needed;
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
        push_segment_size(ctsConfig::Settings->PushBytes),
        pull_segment_size(ctsConfig::Settings->PullBytes),
        intra_segment_transfer(0ULL),
        listening(ctsConfig::IsListening()),
        io_needed(true),
        sending(!ctsConfig::IsListening()) // start with clients sending, servers receiving
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
    ctsIOTask ctsIOPatternPushPull::next_task() NOEXCEPT
    {
        ctsUnsignedLong segment_size;
        if (this->listening) {
            // server role is opposite client
            segment_size = (this->sending) ? this->pull_segment_size : this->push_segment_size;
        } else {
            segment_size = (this->sending) ? this->push_segment_size : this->pull_segment_size;
        }

        ctFatalCondition(
            (this->intra_segment_transfer >= segment_size),
            L"Invalid ctsIOPatternPushPull state: intra_segment_transfer (%lu), segment_size (%lu)",
            static_cast<unsigned long>(this->intra_segment_transfer),
            static_cast<unsigned long>(segment_size));

        if (this->io_needed) {
            this->io_needed = false;

            if (this->sending) {
                return this->tracked_task(
                    IOTaskAction::Send,
                    segment_size - this->intra_segment_transfer);
            } else {
                return this->tracked_task(
                    IOTaskAction::Recv,
                    segment_size - this->intra_segment_transfer);
            }
        } else {
            return ctsIOTask();
        }
    }
    ctsIOPatternProtocolError ctsIOPatternPushPull::completed_task(const ctsIOTask& _task, unsigned long _current_transfer) NOEXCEPT
    {
        if (IOTaskAction::Send == _task.ioAction) {
            this->stats.bytes_sent.add(_current_transfer);
        } else {
            this->stats.bytes_recv.add(_current_transfer);
        }

        this->io_needed = true;
        this->intra_segment_transfer += _current_transfer;

        ctsUnsignedLong segment_size;
        if (this->listening) {
            // server role is opposite client
            segment_size = (this->sending) ? this->pull_segment_size : this->push_segment_size;
        } else {
            segment_size = (this->sending) ? this->push_segment_size : this->pull_segment_size;
        }

        ctFatalCondition(
            (this->intra_segment_transfer > segment_size),
            L"Invalid ctsIOPatternPushPull state: intra_segment_transfer (%lu), segment_size (%lu)",
            static_cast<unsigned long>(this->intra_segment_transfer),
            static_cast<unsigned long>(segment_size));

        if (segment_size == this->intra_segment_transfer) {
            this->sending = !this->sending;
            this->intra_segment_transfer = 0;
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
        remaining_send_bytes(0),
        remaining_recv_bytes(0),
        recv_needed(ctsConfig::Settings->PrePostRecvs),
        send_bytes_inflight(0)
    {
        // max transfer bytes must be an even # so send bytes and recv bytes are balanced
        auto current_max_transfer = this->get_total_transfer();
        if (current_max_transfer % 2 != 0) {
            this->set_total_transfer(++current_max_transfer);
        }

        remaining_send_bytes = current_max_transfer / 2;
        remaining_recv_bytes = remaining_send_bytes;

        ctFatalCondition(
            (remaining_send_bytes + remaining_recv_bytes) != this->get_total_transfer(),
            L"ctsIOPatternDuplex: internal failure - send_bytes (%llu) + recv_bytes (%llu) must equal total bytes (%llu)",
            static_cast<ULONGLONG>(remaining_send_bytes),
            static_cast<ULONGLONG>(remaining_recv_bytes),
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
    ctsIOTask ctsIOPatternDuplex::next_task() NOEXCEPT
    {
        ctsIOTask return_task;

        // since we can have multiple receives in flight, must also check that we have remaining_recv_bytes
        if (this->remaining_recv_bytes > 0 && this->recv_needed > 0) {
            // for very large transfers, we need to ensure our SafeInt<long long> doesn't overflow when it's cast 
            // to unsigned long when passed to tracked_task()
            ctsUnsignedLong max_remaining_bytes = this->remaining_recv_bytes > MAXLONG ?
                MAXLONG :
                static_cast<unsigned long>(this->remaining_recv_bytes);
            return_task = this->tracked_task(IOTaskAction::Recv, max_remaining_bytes);
            // for tracking purposes, assume that this recv *might* end up receiving the entire buffer size
            // - only on completion will we adjust to the actual # of bytes received
            this->remaining_recv_bytes -= return_task.buffer_length;
            --this->recv_needed;

        } else if (this->remaining_send_bytes > 0 && this->get_ideal_send_backlog() > this->send_bytes_inflight) {
            // for very large transfers, we need to ensure our SafeInt<long long> doesn't overflow when it's cast 
            // to unsigned long when passed to tracked_task()
            ctsUnsignedLong max_remaining_bytes = this->remaining_send_bytes > MAXLONG ? 
                MAXLONG : 
                static_cast<unsigned long>(this->remaining_send_bytes);
            ctsUnsignedLong max_send = this->get_ideal_send_backlog() - this->send_bytes_inflight;
            if (max_send > max_remaining_bytes) {
                max_send = max_remaining_bytes;
            }
            return_task = this->tracked_task(IOTaskAction::Send, max_remaining_bytes);
            this->remaining_send_bytes -= return_task.buffer_length;
            this->send_bytes_inflight += return_task.buffer_length;
        } else {
            // no IO needed now: return the default task
        }

        return return_task;
    }
    ctsIOPatternProtocolError ctsIOPatternDuplex::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) NOEXCEPT
    {
        switch (_task.ioAction) {
        case IOTaskAction::Send:
            this->stats.bytes_sent.add(_completed_bytes);
            this->send_bytes_inflight -= _completed_bytes;

            // first, we need to adjust the total back from our over-subscription guard when this task was created
            this->remaining_send_bytes += _task.buffer_length;
            // then we need to subtract back out the actual number of bytes sent
            this->remaining_send_bytes -= _completed_bytes;
            break;

        case IOTaskAction::Recv:
            this->stats.bytes_recv.add(_completed_bytes);
            ++this->recv_needed;

            // first, we need to adjust the total back from our over-subscription guard when this task was created
            this->remaining_recv_bytes += _task.buffer_length;
            // then we need to subtract back out the actual number of bytes received
            this->remaining_recv_bytes -= _completed_bytes;
            break;
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
        frame_size_bytes(ctsConfig::GetMediaStream().FrameSizeBytes),
        current_frame_requested(0UL),
        current_frame_completed(0UL),
        frame_rate_fps(ctsConfig::GetMediaStream().FramesPerSecond),
        current_frame(1UL),
        base_time_milliseconds(0LL),
        state(ServerState::NotStarted)
    {
        ctsConfig::PrintDebug(L"\t\tctsIOPatternMediaStreamServer - frame rate in milliseconds per frame : %lld\n", static_cast<long long>(1000UL / this->frame_rate_fps));
    }
    ctsIOPatternMediaStreamServer::~ctsIOPatternMediaStreamServer()
    {
    }
    // required virtual functions
    ctsIOTask ctsIOPatternMediaStreamServer::next_task() NOEXCEPT
    {
        ctsIOTask return_task;
        switch (this->state) {
        case ServerState::NotStarted:
            // get a writable buffer (ie. Recv), then update the fields in the task for the connection_id
            return_task = ctsMediaStreamMessage::MakeConnectionIdTask(
                this->untracked_task(IOTaskAction::Recv, UdpDatagramConnectionIdHeaderLength),
                this->connection_id());
            this->state = ServerState::IdSent;
            break;

        case ServerState::IdSent:
            this->base_time_milliseconds = ctTimer::snap_qpc_as_msec();
            this->state = ServerState::IoStarted;
            // fall-through
        case ServerState::IoStarted:
            if (current_frame_requested < frame_size_bytes) {
                return_task = this->tracked_task(IOTaskAction::Send, frame_size_bytes);
                // calculate the future time to initiate the IO
                // - then subtract the start time to give the difference
                return_task.time_offset_milliseconds =
                    this->base_time_milliseconds
                    + static_cast<long long>(this->current_frame) * 1000LL / static_cast<long long>(this->frame_rate_fps)
                    - ctTimer::snap_qpc_as_msec();

                current_frame_requested += return_task.buffer_length;
            }
            break;
        }
        return return_task;
    }
    ctsIOPatternProtocolError ctsIOPatternMediaStreamServer::completed_task(const ctsIOTask& _task, unsigned long _current_transfer) NOEXCEPT
    {
        if (_task.buffer_type != ctsIOTask::BufferType::UdpConnectionId) {
            ctsUnsignedLong current_transfer_bits = _current_transfer * 8UL;

            ctsConfig::Settings->UdpStatusDetails.bits_received.add(current_transfer_bits);
            this->stats.bits_received.add(current_transfer_bits);

            this->current_frame_completed += _current_transfer;
            if (this->current_frame_completed == frame_size_bytes) {
                ++this->current_frame;
                this->current_frame_requested = 0UL;
                this->current_frame_completed = 0UL;
            }
        }
        return ctsIOPatternProtocolError::NoError;
    }
} //namespace
