/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// parent header
#include "ctsIOPattern.h"
// additional cpp headers
#include <vector>
#include <iterator>
// additional ctl headers
#include <ctSocketExtensions.hpp>
#include <ctScopeGuard.hpp>
#include <ctLocks.hpp>
#include <ctTimer.hpp>
// additional local headers
#include "ctsMediaStreamProtocol.hpp"
#include "ctsIOBuffers.hpp"

namespace ctsTraffic {

    using namespace ctl;
    using namespace std;

    // buffer pattern to transfer
    static const unsigned long BufferPatternSize = 128;
    static const unsigned char BufferPattern[] =
    {
        0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00,
        0x05, 0x00, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00,
        0x09, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x0c, 0x00,
        0x0d, 0x00, 0x0e, 0x00, 0x0f, 0x00, 0x0f, 0x0f,
        0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00,
        0x05, 0x00, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00,
        0x09, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x0c, 0x00,
        0x0d, 0x00, 0x0e, 0x00, 0x0f, 0x00, 0x0f, 0x0f,

        0xf1, 0xff, 0xf2, 0xff, 0xf3, 0xff, 0xf4, 0xff,
        0xf5, 0xff, 0xf6, 0xff, 0xf7, 0xff, 0xf8, 0xff,
        0xf9, 0xff, 0xfa, 0xff, 0xfb, 0xff, 0xfc, 0xff,
        0xfd, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xf1, 0xff, 0xf2, 0xff, 0xf3, 0xff, 0xf4, 0xff,
        0xf5, 0xff, 0xf6, 0xff, 0xf7, 0xff, 0xf8, 0xff,
        0xf9, 0xff, 0xfa, 0xff, 0xfb, 0xff, 0xfc, 0xff,
        0xfd, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff
    };

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
                break;

            case ctsConfig::IoPatternType::Push:
                return make_shared<ctsIOPatternPush>();
                break;

            case ctsConfig::IoPatternType::PushPull:
                return make_shared<ctsIOPatternPushPull>();
                break;

            case ctsConfig::IoPatternType::Duplex:
                return make_shared<ctsIOPatternDuplex>();
                break;

            case ctsConfig::IoPatternType::MediaStream:
                if (ctsConfig::IsListening()) {
                    return make_shared<ctsIOPatternMediaStreamServer>();
                } else {
                    return make_shared<ctsIOPatternMediaStreamClient>();
                }
                break;

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
        buffer_size(ctsConfig::GetBufferSize()),
        send_pattern_offset(0),
        recv_pattern_offset(0),
        recv_rio_bufferid(RIO_INVALID_BUFFERID),
        // (bytes/sec) * (1 sec/1000 ms) * (x ms/Quantum) == (bytes/quantum)
        bytes_sending_per_quantum(ctsConfig::GetTcpBytesPerSecond() * static_cast<unsigned long long>(ctsConfig::Settings->TcpBytesPerSecondPeriod) / 1000LL),
        bytes_sending_this_quantum(0LL),
        quantum_start_time_ms(ctTimer::snap_qpc_msec()),
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
                    recv_buffer_container.resize(buffer_size * _recv_count);
                    char* raw_recv_buffer = &recv_buffer_container[0];
                    for (unsigned long free_list = 0; free_list < _recv_count; ++free_list) {
                        recv_buffer_free_list.push_back(raw_recv_buffer + static_cast<size_t>(free_list * buffer_size));
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
                recv_rio_bufferid = ctRIORegisterBuffer(recv_buffer_free_list[0], static_cast<DWORD>(buffer_size));
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
        if (recv_rio_bufferid != RIO_INVALID_BUFFERID &&
            recv_rio_bufferid != s_SharedBufferId) {
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
            case IOTaskAction::HardShutdown:
                // Fall-through to be processed like send or recv IO
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

                        this->update_last_protocol_error(this->pattern_state.completed_task(_original_task, _current_transfer));
                    }
                    ctsIOBuffers::ReleaseConnectionIdBuffer(_original_task);

                } else if (_status_code != NO_ERROR) {
                    //
                    // if the IO task failed, the entire IO pattern is now failed
                    // - unless this is an extra recv that was canceled once we completed the transfer
                    //
                    if (!(IOTaskAction::Recv == _original_task.ioAction && this->pattern_state.is_completed())) {
                        if (this->update_last_error(_status_code) != ctsStatusIORunning) {
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

                    if (ctsConfig::Settings->Protocol == ctsConfig::ProtocolType::TCP &&
                        (ctsIOPatternProtocolError::SuccessfullyCompleted == pattern_status || ctsIOPatternProtocolError::NoError == pattern_status) &&
                        _original_task.track_io &&
                        ctsConfig::Settings->ShouldVerifyBuffers) {
                        //
                        // no protocol error, so process the IO
                        //
                        switch (_original_task.ioAction) {
                            case IOTaskAction::Recv:
                                ctFatalCondition(
                                    _original_task.expected_pattern_offset != this->recv_pattern_offset,
                                    L"ctsIOPattern::complete_io() : ctsIOTask (%p) expected_pattern_offset (%lu) does not match the current pattern_offset (%Iu)",
                                    &_original_task, _original_task.expected_pattern_offset, static_cast<size_t>(this->recv_pattern_offset));

                                if (!this->verify_buffer(_original_task, _current_transfer)) {
                                    this->update_last_error(ctsStatusErrorDataDidNotMatchBitPattern);
                                }

                                this->recv_pattern_offset += _current_transfer;
                                this->recv_pattern_offset %= BufferPatternSize;
                                break;

                            case IOTaskAction::Send:
                                this->send_pattern_offset += _current_transfer;
                                this->send_pattern_offset %= BufferPatternSize;
                                break;
                        }
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
        if ((_original_task.buffer_type != ctsIOTask::BufferType::TcpConnectionId) &&
            (_original_task.ioAction != IOTaskAction::None) &&
            (NO_ERROR == _status_code)) {
            this->update_last_protocol_error(
                this->completed_task(_original_task, _current_transfer));
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
        ctsSignedLongLong new_buffer_size = 0LL;
        //
        // first: calculate the next buffer size assuming no max ceiling specified by the protocol
        //
        new_buffer_size = min<ctsUnsignedLongLong>(
            this->buffer_size,
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
                auto current_time_ms(ctTimer::snap_qpc_msec());
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
                return_task.buffer_length + return_task.buffer_offset > this->buffer_size,
                L"return_task (%p) for a Recv request is specifying a buffer that is larger than this->buffer_size (%lu) (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                &return_task, static_cast<unsigned long>(this->buffer_size), this);
        }

        return return_task;
    }
    bool ctsIOPattern::verify_buffer(const ctsIOTask& _original_task, unsigned long _transferred_bytes)
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
        io_needed(ctsConfig::IsListening() ? 1 : ctsConfig::Settings->PrePostRecvs),
        sending(ctsConfig::IsListening())
    {
    }
    ctsIOPatternPull::~ctsIOPatternPull() NOEXCEPT
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
        if (this->io_needed > 0) {
            --this->io_needed;
            IOTaskAction next_ioaction;
            if (this->sending) {
                next_ioaction = IOTaskAction::Send;
            } else {
                next_ioaction = IOTaskAction::Recv;
            }
            return this->tracked_task(next_ioaction);
        } else {
            return ctsIOTask();
        }
    }
    ctsIOPatternProtocolError ctsIOPatternPull::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) NOEXCEPT
    {
        if (IOTaskAction::Send == _task.ioAction) {
            ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_completed_bytes);
            this->stats.bytes_sent.add(_completed_bytes);
        } else {
            ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_completed_bytes);
            this->stats.bytes_recv.add(_completed_bytes);
        }

        ++this->io_needed;
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
        io_needed(ctsConfig::IsListening() ? ctsConfig::Settings->PrePostRecvs : 1),
        sending(!ctsConfig::IsListening())
    {
    }
    ctsIOPatternPush::~ctsIOPatternPush() NOEXCEPT
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
        if (this->io_needed > 0) {
            --this->io_needed;
            IOTaskAction next_ioaction;
            if (this->sending) {
                next_ioaction = IOTaskAction::Send;
            } else {
                next_ioaction = IOTaskAction::Recv;
            }
            return this->tracked_task(next_ioaction);
        } else {
            return ctsIOTask();
        }
    }
    ctsIOPatternProtocolError ctsIOPatternPush::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) NOEXCEPT
    {
        if (IOTaskAction::Send == _task.ioAction) {
            ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_completed_bytes);
            this->stats.bytes_sent.add(_completed_bytes);
        } else {
            ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_completed_bytes);
            this->stats.bytes_recv.add(_completed_bytes);
        }

        ++this->io_needed;
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
    ctsIOPatternPushPull::~ctsIOPatternPushPull() NOEXCEPT
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
            ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_current_transfer);
            this->stats.bytes_sent.add(_current_transfer);
        } else {
            ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_current_transfer);
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
        send_needed(1),
        recv_needed(ctsConfig::Settings->PrePostRecvs)
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
    ctsIOPatternDuplex::~ctsIOPatternDuplex() NOEXCEPT
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
    ctsIOTask ctsIOPatternDuplex::next_task() NOEXCEPT
    {
        ctsIOTask return_task;

        // since we can have multiple receives in flight, must also check that we have remaining_recv_bytes
        if (this->recv_needed > 0 && this->remaining_recv_bytes > 0) {
            /// for very large transfers, we need to ensure our SafeInt<long long> doesn't overflow
            /// - when we cast it to unsigned long
            return_task = this->tracked_task(
                IOTaskAction::Recv,
                remaining_recv_bytes > MAXLONG ? MAXLONG : static_cast<unsigned long>(this->remaining_recv_bytes));
            // for tracking purposes, assume that this recv *might* end up receiving the entire buffer size
            // - only on completion will we adjust to the actual # of bytes received
            // this logic was added to avoid over-subscription for remaining recv bytes when recv_needed > 1
            this->remaining_recv_bytes -= return_task.buffer_length;
            --this->recv_needed;

        } else if (this->send_needed > 0 && this->remaining_send_bytes > 0) {
            /// for very large transfers, we need to ensure our SafeInt<long long> doesn't overflow
            /// - when we cast it to unsigned long
            return_task = this->tracked_task(
                IOTaskAction::Send,
                remaining_send_bytes > MAXLONG ? MAXLONG : static_cast<unsigned long>(this->remaining_send_bytes));
            // as above, this logic was added to avoid over-subscription for remaining send bytes when send_needed > 1
            this->remaining_send_bytes -= return_task.buffer_length;
            --this->send_needed;
        } else {
            // no IO needed now: return the default task
        }

        return return_task;
    }
    ctsIOPatternProtocolError ctsIOPatternDuplex::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) NOEXCEPT
    {
        switch (_task.ioAction) {
            case IOTaskAction::Send:
                ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_completed_bytes);
                this->stats.bytes_sent.add(_completed_bytes);

                // first, we need to adjust the total back from our over-subscription guard when this task was created
                this->remaining_send_bytes += _task.buffer_length;
                // then we need to subtract back out the actual number of bytes sent
                this->remaining_send_bytes -= _completed_bytes;
                ++this->send_needed;
                break;

            case IOTaskAction::Recv:
                ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_completed_bytes);
                this->stats.bytes_recv.add(_completed_bytes);

                // first, we need to adjust the total back from our over-subscription guard when this task was created
                this->remaining_recv_bytes += _task.buffer_length;
                // then we need to subtract back out the actual number of bytes received
                this->remaining_recv_bytes -= _completed_bytes;
                ++this->recv_needed;
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
    ctsIOTask ctsIOPatternMediaStreamServer::next_task()
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
                this->base_time_milliseconds = ctTimer::snap_qpc_msec();
                this->state = ServerState::IoStarted;
                // fall-through
            case ServerState::IoStarted:
                if (current_frame_requested < frame_size_bytes) {
                    return_task = this->tracked_task(IOTaskAction::Send, frame_size_bytes);
                    // calculate the future time to initiate the IO
                    // - then subtract the start time to give the difference
                    return_task.time_offset_milliseconds =
                        this->base_time_milliseconds
                        + static_cast<long long>(this->current_frame * 1000UL / this->frame_rate_fps)
                        - ctTimer::snap_qpc_msec();

                    current_frame_requested += return_task.buffer_length;
                }
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
        renderer_timer(nullptr),
        start_timer(nullptr),
        frame_size_bytes(ctsConfig::GetMediaStream().FrameSizeBytes < UdpDatagramDataHeaderLength ? UdpDatagramDataHeaderLength : ctsConfig::GetMediaStream().FrameSizeBytes),
        final_frame(ctsConfig::GetMediaStream().StreamLengthFrames),
        initial_buffer_frames(0UL),
        timer_wheel_offset_frames(0UL),
        recv_needed(ctsConfig::Settings->PrePostRecvs),
        base_time_milliseconds(0LL),
        tracking_resend_sequence_number(1LL),
        frame_rate_ms_per_frame(1000.0 / static_cast<unsigned long>(ctsConfig::GetMediaStream().FramesPerSecond)),
        frame_entries(),
        head_entry(),
        send_buffers(),
        started_timers(false),
        finished_stream(false)
    {
        initial_buffer_frames = ctsConfig::GetMediaStream().BufferedFrames;
        // if the entire session fits in the inital buffer, update accordingly
        if (final_frame < initial_buffer_frames) {
            initial_buffer_frames = final_frame;
        }
        // start the timer at 1/2 the total frame-queue length to start checking for resend's
        timer_wheel_offset_frames = static_cast<unsigned long>(initial_buffer_frames / 2);

        const static long ExtraBufferDepthFactor = 2;
        // queue_size is intentionally a signed long: will catch overflows
        ctsSignedLong queue_size = ExtraBufferDepthFactor * initial_buffer_frames;
        if (queue_size < ExtraBufferDepthFactor) {
            throw ctException(
                ERROR_INVALID_DATA,
                L"BufferDepth & FrameSize don't allow for enough buffered stream",
                L"ctsIOPatternMediaStreamClient",
                false);
        }

        ctsConfig::PrintDebug(L"\t\tctsIOPatternMediaStreamClient - queue size for this new connection is %d\n", static_cast<long>(queue_size));
        ctsConfig::PrintDebug(L"\t\tctsIOPatternMediaStreamClient - frame rate in milliseconds per frame : %lld\n", static_cast<long long>(frame_rate_ms_per_frame));

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
        if (NULL == renderer_timer) {
            throw ctException(::GetLastError(), L"CreateThreadpoolTimer", L"ctsIOPatternMediaStreamClient", false);
        }
        ctlScopeGuard(deleteTimerCallbackOnError, {
            ::SetThreadpoolTimer(renderer_timer, NULL, 0, 0);
            ::WaitForThreadpoolTimerCallbacks(renderer_timer, FALSE);
            ::CloseThreadpoolTimer(renderer_timer);
        });

        start_timer = ::CreateThreadpoolTimer(StartCallback, this, nullptr);
        if (NULL == start_timer) {
            throw ctException(::GetLastError(), L"CreateThreadpoolTimer", L"ctsIOPatternMediaStreamClient", false);
        }
        // no errors, dismiss the scope guard
        deleteTimerCallbackOnError.dismiss();
    }
    ctsIOPatternMediaStreamClient::~ctsIOPatternMediaStreamClient()
    {
        // cleanly shutdown the TP timer
        // - indicate this by setting the TP_TIMER to null under the cs
        // - so the callback will no longer schedule more TP timer instances
        // - then wait for everything to clean up
        PTP_TIMER original_timer = nullptr;

        this->base_lock();
        original_timer = this->renderer_timer;
        this->renderer_timer = nullptr;
        this->base_unlock();

        // stop both timers
        ::SetThreadpoolTimer(this->start_timer, NULL, 0, 0);
        ::WaitForThreadpoolTimerCallbacks(this->start_timer, FALSE);
        ::CloseThreadpoolTimer(this->start_timer);

        ::SetThreadpoolTimer(original_timer, NULL, 0, 0);
        ::WaitForThreadpoolTimerCallbacks(original_timer, FALSE);
        ::CloseThreadpoolTimer(original_timer);
    }

    ctsIOTask ctsIOPatternMediaStreamClient::next_task()
    {
        if (!this->started_timers) {
            // initiate the timers the first time the object is used
            this->started_timers = true;
            this->base_time_milliseconds = ctTimer::snap_qpc_msec();
            this->set_next_start_timer();
            this->set_next_timer();
        }

        // defaulting to an empty task (do nothing)
        ctsIOTask return_task;
        if (this->recv_needed > 0) {
            // don't try posting more than UdpDatagramMaximumSizeBytes at a time
            unsigned long max_size_buffer = 0;
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

    ctsIOPatternProtocolError ctsIOPatternMediaStreamClient::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) NOEXCEPT
    {
        if (_task.ioAction == IOTaskAction::Abort) {
            // the stream should now be done
            ctFatalCondition(
                !this->finished_stream,
                L"ctsIOPatternMediaStreamClient (dt %p ctsTraffic!ctsTraffic::ctsIOPatternMediaStreamClient) processed an Abort before the stream was finished", this);
            return ctsIOPatternProtocolError::SuccessfullyCompleted;
        }

        if (_task.ioAction == IOTaskAction::Recv) {
            if (0 == _completed_bytes && this->finished_stream) {
                // the final WSARecvFrom can complete with a zero-byte recv on loopback after the sender closes
                // TODO: verify on non-loopback
                return ctsIOPatternProtocolError::NoError;
            }

            if (!ctsMediaStreamMessage::ValidateBufferLengthFromTask(_task, _completed_bytes)) {
                ctsConfig::PrintDebug(
                    L"[%.3f] MediaStreamClient recevieved an invalid datagram trying to parse the protocol header\n",
                    ctsConfig::GetStatusTimeStamp());
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

            long long received_seq_number = ctsMediaStreamMessage::GetSequenceNumberFromTask(_task);
            if (received_seq_number > this->final_frame) {
                ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                this->stats.error_frames.increment();

                ctsConfig::PrintDebug(
                    L"[%.3f] MediaStreamClient recevieved **an unknown** seq number (%lld) (outside the final frame %lu)\n",
                    ctsConfig::GetStatusTimeStamp(),
                    received_seq_number,
                    this->final_frame);
            } else {
                //
                // search our circular queue (starting at the head_entry)
                // for the seq number we just received, and if found, tag as received
                // tracking_resend_sequence_number will be zero when it's time to exit
                //
                if (this->tracking_resend_sequence_number > 0) {
                    // take the base class lock before accessing our internal queue
                    this->base_lock();
#pragma warning(suppress: 26110)   //  PREFast is getting confused with the scope guard
                    ctlScopeGuard(unlockBaseLockOnExit, { this->base_unlock(); });

                    auto found_slot = this->find_sequence_number(received_seq_number);
                    if (found_slot != this->frame_entries.end()) {
                        if (found_slot->received != this->frame_size_bytes) {
                            long long buffered_qpc = *reinterpret_cast<long long*>(_task.buffer + 8);
                            long long buffered_qpf = *reinterpret_cast<long long*>(_task.buffer + 16);

                            LARGE_INTEGER qpc;
                            QueryPerformanceCounter(&qpc);
                            // always overwrite qpc & qpf values with the latest datagram details
                            found_slot->sender_qpc = buffered_qpc;
                            found_slot->sender_qpf = buffered_qpf;
                            found_slot->receiver_qpc = qpc.QuadPart;
                            found_slot->receiver_qpf = ctTimer::snap_qpf();
                            found_slot->received += _completed_bytes;

                            ctsConfig::PrintDebug(
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

                            ctsConfig::PrintDebug(
                                L"[%.3f] MediaStreamClient received **a duplicate frame** for seq number (%lld)\n",
                                ctsConfig::GetStatusTimeStamp(),
                                received_seq_number);
                        }
                    } else {
                        ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                        this->stats.error_frames.increment();

                        if (received_seq_number < this->head_entry->sequence_number) {
                            ctsConfig::PrintDebug(
                                L"[%.3f] MediaStreamClient received **a stale** seq number (%lld) - current seq number (%lld)\n",
                                ctsConfig::GetStatusTimeStamp(),
                                received_seq_number,
                                static_cast<long long>(this->head_entry->sequence_number));
                        } else {
                            ctsConfig::PrintDebug(
                                L"[%.3f] MediaStreamClient recevieved **a future** seq number (%lld) - head of queue (%lld) tail of queue (%lld)\n",
                                ctsConfig::GetStatusTimeStamp(),
                                received_seq_number,
                                static_cast<long long>(this->head_entry->sequence_number),
                                static_cast<long long>(this->head_entry->sequence_number + this->frame_entries.size() - 1));
                        }
                    }
                }
            }

            // since a recv completed successfully, will need to request another
            ++this->recv_needed;

        } else {  // else process SEND requests
            // process the DONE request 
            if (0 == ::memcmp("DONE", _task.buffer, min<unsigned long>(4, _task.buffer_length))) {
                // indicate to the caller to abort any pended recv requests: aborting
                this->finished_stream = true;
                ctsIOTask abort_task;
                abort_task.ioAction = IOTaskAction::Abort;
                this->send_callback(abort_task);
                ctsConfig::PrintDebug(L"\t\tctsIOPatternMediaStreamClient - issuing an ABORT to cleanly close the connection\n");

            } else if (0 == ::memcmp("START", _task.buffer, min<unsigned long>(5, _task.buffer_length))) {
                // nothing to do : it's a static buffer

            } else {
                // find the sent buffer and remove it
                auto found_buffer = find_if(
                    begin(this->send_buffers),
                    end(this->send_buffers),
                    [_task] (unique_ptr<string>& _string_ptr) {
                    const string& sent_string(*_string_ptr);
                    return &sent_string[0] == _task.buffer;
                });
                ctFatalCondition(
                    found_buffer == end(this->send_buffers),
                    L"ctsIOPatternMediaStreamClient (%p) failed to find its send_buffer",
                    this);
                this->send_buffers.erase(found_buffer);
            }
        }

        return ctsIOPatternProtocolError::NoError;
    }

    ///
    /// Returns an iterator within frame_entries pointing to the FrameEntry
    ///   matching the specified sequence number.
    /// If the sequence number was not found, will return end(frame_entries)
    ///
    _Requires_lock_held_(cs)
    vector<ctsIOPatternMediaStreamClient::FrameEntry>::iterator ctsIOPatternMediaStreamClient::find_sequence_number(long long _seq_number) NOEXCEPT
    {
        ctsSignedLongLong head_sequence_number = this->head_entry->sequence_number;
        ctsSignedLongLong tail_sequence_number = head_sequence_number + this->frame_entries.size() - 1;
        ctsSignedLongLong vector_end_sequence_number = this->frame_entries.rbegin()->sequence_number;

        if (_seq_number > tail_sequence_number || _seq_number < head_sequence_number) {
            // sequence number was out of range of our circular queue
            // - return end(frame_entries) to indicate it could not be found
            return end(this->frame_entries);
        }

        if (_seq_number <= vector_end_sequence_number) {
            // offset just from the head since it hasn't wrapped around the end
            size_t offset = static_cast<size_t>(_seq_number - head_sequence_number);
            return this->head_entry + offset;
        } else {
            // offset from the beginning since it wrapped around from the end
            size_t offset = static_cast<size_t>(_seq_number - vector_end_sequence_number - 1LL);
            return this->frame_entries.begin() + offset;
        }
    }

    _Requires_lock_held_(cs)
    bool ctsIOPatternMediaStreamClient::received_buffered_frames() NOEXCEPT
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
    void ctsIOPatternMediaStreamClient::set_next_timer() NOEXCEPT
    {
        // only schedule the next timer instance if the d'tor hasn't indicated it's wanting to exit
        if (this->renderer_timer != nullptr) {
            // calculate when that time should be relative to base_time_milliseconds 
            // (base_time_milliseconds is the start milliseconds from ctTimer::snap_qpc_msec())
            long long timer_offset = this->base_time_milliseconds;
            // offset to the time when we need to check the next frame
            // - we'll also render a frame at the same time if the initial buffer is full
            timer_offset += static_cast<long long>(static_cast<unsigned long>(this->timer_wheel_offset_frames) * this->frame_rate_ms_per_frame);
            // subtract out the current time to get the delta # of milliseconds
            timer_offset -= ctTimer::snap_qpc_msec();
            // can't let it go negative
            if (timer_offset < 1) {
                timer_offset = 0;
            }
            // convert to filetime from milliseconds
            // - make a 'relative' for SetThreadpoolTimer
            FILETIME file_time(ctTimer::convert_msec_relative_filetime(timer_offset));
            // TP Timer APIs work off of the UTC time
            ::SetThreadpoolTimer(this->renderer_timer, &file_time, 0, 0);
        }
    }
    _Requires_lock_held_(cs)
    void ctsIOPatternMediaStreamClient::set_next_start_timer() NOEXCEPT
    {
        if (this->start_timer != nullptr) {
            // convert to filetime from milliseconds
            // - make a 'relative' for SetThreadpoolTimer
            FILETIME file_time(ctTimer::convert_msec_relative_filetime(500));
            // TP Timer APIs work off of the UTC time
            ::SetThreadpoolTimer(this->start_timer, &file_time, 0, 0);
        }
    }

    // "render the current frame"
    // - update the current frame as "read" and move the head to the next frame
    _Requires_lock_held_(cs)
    void ctsIOPatternMediaStreamClient::render_frame() NOEXCEPT
    {
        if (this->head_entry->retried) {
            ctsConfig::Settings->UdpStatusDetails.retry_attempts.increment();
            this->stats.retry_attempts.increment();
        }

        if (this->head_entry->received == this->frame_size_bytes) {
            ctsConfig::Settings->UdpStatusDetails.successful_frames.increment();
            this->stats.successful_frames.increment();

            ctsConfig::PrintDebug(
                L"\t\tctsIOPatternMediaStreamClient rendered frame %lld\n",
                static_cast<long long>(this->head_entry->sequence_number));

            // Directly write this status update if jitter is enabled
            ctsConfig::PrintJitterUpdate(
                this->head_entry->sequence_number,
                this->head_entry->sender_qpc,
                this->head_entry->sender_qpf,
                this->head_entry->receiver_qpc,
                this->head_entry->receiver_qpf);

        } else {
            ctsConfig::Settings->UdpStatusDetails.dropped_frames.increment();
            this->stats.dropped_frames.increment();

            ctsConfig::PrintDebug(
                L"[%.3f] MediaStreamClient **dropped** frame %lld\n",
                ctsConfig::GetStatusTimeStamp(),
                static_cast<long long>(this->head_entry->sequence_number));
        }

        // update the current sequence number so it's now the "end" sequence number of the queue (the new max value)
        this->head_entry->sequence_number = this->head_entry->sequence_number + this->frame_entries.size();
        this->head_entry->received = 0;
        this->head_entry->retried = false;

        // move the head entry to the next sequence number
        ++this->head_entry;
        if (this->head_entry == this->frame_entries.end()) {
            this->head_entry = this->frame_entries.begin();
        }
    }

    VOID CALLBACK ctsIOPatternMediaStreamClient::StartCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER)
    {
        static const char StartBuffer[] = "START";

        ctsIOPatternMediaStreamClient* this_ptr = reinterpret_cast<ctsIOPatternMediaStreamClient*>(_context);
        // take the base lock before touching any internal members
        this_ptr->base_lock();
        // guarantee the lock is released on exit
#pragma warning(suppress: 26110)   //  PREFast is getting confused with the scope guard
        ctlScopeGuard(unlockBaseLockOnExit, { this_ptr->base_unlock(); });

        if (0 == this_ptr->tracking_resend_sequence_number) {
            // this_ptr->tracking_resend_sequence_number will be zero when the object indicates to itself that it's time to exit
            return;
        }

        if (!this_ptr->received_buffered_frames()) {
            // send another start message
            ctsConfig::PrintDebug(L"\t\tctsIOPatternMediaStreamClient re-requesting START\n");

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

    VOID CALLBACK ctsIOPatternMediaStreamClient::TimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID _context, PTP_TIMER)
    {
        ctsIOPatternMediaStreamClient* this_ptr = reinterpret_cast<ctsIOPatternMediaStreamClient*>(_context);
        // take the base lock before touching any internal members
        this_ptr->base_lock();
        // guarantee the lock is released on exit
#pragma warning(suppress: 26110)   //  PREFast is getting confused with the scope guard
        ctlScopeGuard(unlockBaseLockOnExit, { this_ptr->base_unlock(); });

        if (0 == this_ptr->tracking_resend_sequence_number) {
            // this_ptr->tracking_resend_sequence_number will be zero when the object indicates to itself that it's time to exit
            return;
        }

        if (ctsConfig::StreamCodecValue::ResendOnce == ctsConfig::GetMediaStream().StreamCodec) {
            // check for resends when the codec has specified and we have not yet checked the final frame
            // - only request a RESEND if we have ever heard from the server
            // if we never hear from the server we'll eventually terminate the connection
            if (this_ptr->received_buffered_frames()) {
                if (this_ptr->tracking_resend_sequence_number <= this_ptr->final_frame) {
                    vector<FrameEntry>::iterator resend_iterator = this_ptr->find_sequence_number(this_ptr->tracking_resend_sequence_number);
                    if (this_ptr->frame_entries.end() != resend_iterator) {
                        if (resend_iterator->received != this_ptr->frame_size_bytes) {
                            // reset received to zero since we want the whole thing resent
                            resend_iterator->received = 0;

                            ctsConfig::PrintDebug(
                                L"\t\tctsIOPatternMediaStreamClient requesting RESEND frame # %lld\n",
                                static_cast<long long>(resend_iterator->sequence_number));

                            try {
                                this_ptr->send_buffers.emplace_back(
                                    ctsMediaStreamMessage::Construct(
                                    MediaStreamAction::RESEND,
                                    resend_iterator->sequence_number));
                                string* resend_string = this_ptr->send_buffers.rbegin()->get();

                                ctsIOTask resend_task;
                                resend_task.ioAction = IOTaskAction::Send;
                                resend_task.track_io = false;
                                resend_task.buffer = &(*resend_string)[0];
                                resend_task.buffer_offset = 0;
                                resend_task.buffer_length = static_cast<unsigned long>(resend_string->length());
                                resend_task.buffer_type = ctsIOTask::BufferType::Static; // this buffer is only maintained in the derived object, not the base class
                                this_ptr->send_callback(resend_task);

                                resend_iterator->retried = true;
                            }
                            catch (const exception& e) {
                                ctsConfig::PrintException(e);
                            }
                        }
                    }
                    // increment the resend tracking value after checking this referenced frame
                    ++this_ptr->tracking_resend_sequence_number;
                }
            } else {
                // move the tracking resend sequence number to the next frame
                // - the server is very late to send data but we need to continue forward progress
                ++this_ptr->tracking_resend_sequence_number;
            }
        } else {
            // if not checking for rends, just increment the tracking sequence number
            ++this_ptr->tracking_resend_sequence_number;
        }

        bool aborted = false;
        // provide a guard if the client *never* receives any datagrams from the server
        // - only issue this fatalabort if enough time has passed to have fulled the buffered set of frames
        //   but none have been received yet
        if (this_ptr->tracking_resend_sequence_number >= (this_ptr->initial_buffer_frames / 2) &&
            this_ptr->head_entry->sequence_number <= this_ptr->final_frame) {
            // if we haven't yet received *anything* from the server, abort this conneciton
            if (!this_ptr->received_buffered_frames()) {
                ctsConfig::PrintDebug(L"\t\tctsIOPatternMediaStreamClient - issuing a FATALABORT to close the connection\n");
                ctsIOTask abort_task;
                abort_task.ioAction = IOTaskAction::FatalAbort;
                this_ptr->send_callback(abort_task);
                aborted = true;

            } else {
                // if the initial buffer has already been filled, "render" the frame
                this_ptr->render_frame();
            }
        }

        if (!aborted) {
            // wait for the precise number of milliseconds for the next frame
            ++this_ptr->timer_wheel_offset_frames;
            if (this_ptr->head_entry->sequence_number <= this_ptr->final_frame) {
                this_ptr->set_next_timer();

            } else {
                // when all frames are rendered, will track this state internally by setting tracking_resend_sequence_number to zero
                this_ptr->tracking_resend_sequence_number = 0;
                ctsConfig::PrintDebug(L"\t\tctsIOPatternMediaStreamClient - indicating DONE: have rendered all possible frames\n");
                this_ptr->send_callback(ctsMediaStreamMessage::Construct(MediaStreamAction::DONE));
            }
        }
    }

} //namespace
