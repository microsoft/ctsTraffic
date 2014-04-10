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
#include "ctsPrintStatus.hpp"


namespace ctsTraffic {

    using namespace ctl;
    using namespace std;

    // buffer pattern to transfer
    static const size_t BufferPatternSize = 128;
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

    static INIT_ONCE s_IOPatternInitializer = INIT_ONCE_STATIC_INIT;
    static char* s_WriteableSharedBuffer = nullptr;
    static char* s_ProtectedSharedBuffer = nullptr;
    static size_t s_SharedBufferSize = 0;
    static RIO_BUFFERID s_SharedBufferId = RIO_INVALID_BUFFERID;

    static const unsigned long s_FinBufferSize = 16; // just 16 bytes for the FIN
    static char s_FinBuffer[s_FinBufferSize];

    static
    BOOL CALLBACK InitOnceIOPatternCallback(PINIT_ONCE, PVOID, PVOID *) throw()
    {
        s_SharedBufferSize = BufferPatternSize + ctsConfig::GetMaxBufferSize();

        s_ProtectedSharedBuffer = reinterpret_cast<char*>(::VirtualAlloc(nullptr, s_SharedBufferSize, MEM_COMMIT, PAGE_READWRITE));
        if (!s_ProtectedSharedBuffer) {
            ctl::ctAlwaysFatalCondition(L"VirtualAlloc alloc failed: %u", ::GetLastError());
        }

        s_WriteableSharedBuffer = reinterpret_cast<char*>(::VirtualAlloc(nullptr, s_SharedBufferSize, MEM_COMMIT, PAGE_READWRITE));
        if (!s_WriteableSharedBuffer) {
            ctl::ctAlwaysFatalCondition(L"VirtualAlloc alloc failed: %u", ::GetLastError());
        }

        // fill in this allocated buffer while we can write to it
        char* protected_destination = s_ProtectedSharedBuffer;
        char* writeable_destination = s_WriteableSharedBuffer;
        size_t write_size_remaining = s_SharedBufferSize;
        while (write_size_remaining > 0) {
            size_t bytes_to_write = (write_size_remaining > BufferPatternSize) ? BufferPatternSize : write_size_remaining;

            auto memerror = ::memcpy_s(protected_destination, write_size_remaining, BufferPattern, bytes_to_write);
            ctl::ctFatalCondition(
                memerror != 0,
                L"memcpy_s(%p, %Iu, %p, %Iu) failed : %d",
                protected_destination, write_size_remaining, BufferPattern, bytes_to_write, memerror);

            memerror = ::memcpy_s(writeable_destination, write_size_remaining, BufferPattern, bytes_to_write);
            ctl::ctFatalCondition(
                memerror != 0,
                L"memcpy_s(%p, %Iu, %p, %Iu) failed : %d",
                writeable_destination, write_size_remaining, BufferPattern, bytes_to_write, memerror);

            protected_destination += bytes_to_write;
            writeable_destination += bytes_to_write;
            write_size_remaining -= bytes_to_write;
        }

        // now prevent anyone from writing to our s_ProtectedSharedBuffer
        DWORD old_setting;
        if (!::VirtualProtect(s_ProtectedSharedBuffer, s_SharedBufferSize, PAGE_READONLY, &old_setting)) {
            ctl::ctAlwaysFatalCondition(L"VirtualProtect failed: %u", ::GetLastError());
        }

        if (ctsConfig::Settings->SocketFlags & WSA_FLAG_REGISTERED_IO) {
            s_SharedBufferId = ctRIORegisterBuffer(s_WriteableSharedBuffer, static_cast<DWORD>(s_SharedBufferSize));
            if (RIO_INVALID_BUFFERID == s_SharedBufferId) {
                ctl::ctAlwaysFatalCondition(L"RIORegisterBuffer failed: %d", ::WSAGetLastError());
            }
        }

        return TRUE;
    }

    ///
    /// Helper factory to build known patterns
    /// - can throw ctl::ctException on a Win32 error
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
                ctl::ctAlwaysFatalCondition(L"ctsIOPattern::MakeIOPattern - Unknown IoPattern specified (%d)", ctsConfig::Settings->IoPattern);
                return nullptr;
        }
    }
    char* ctsIOPattern::AccessSharedBuffer() throw()
    {
        // this init-once call is no-fail
        (void) ::InitOnceExecuteOnce(&s_IOPatternInitializer, InitOnceIOPatternCallback, NULL, NULL);
        return s_ProtectedSharedBuffer;
    }

    ctsIOPattern::ctsIOPattern(unsigned long _recv_count) :
        cs(),
        recv_buffer_free_list(),
        recv_buffer_container(),
        callback(nullptr),
        current_transfer(0),
        max_transfer(ctsConfig::GetTransferSize()),
        buffer_size(ctsConfig::GetBufferSize()),
        inflight_bytes(0),
        send_pattern_offset(0),
        recv_pattern_offset(0),
        recv_rio_bufferid(RIO_INVALID_BUFFERID),
        protocol_status(MoreData),
        bytes_sending_per_quantum(0LL),
        bytes_sending_this_quantum(0LL),
        quantum_start_time_ms(ctl::ctTimer::snap_qpc_msec())
    {
        // this init-once call is no-fail
        (void) ::InitOnceExecuteOnce(&s_IOPatternInitializer, InitOnceIOPatternCallback, NULL, NULL);

        if (!::InitializeCriticalSectionEx(&cs, 4000, 0)) {
            throw ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsIOPattern", false);
        }
        ctlScopeGuard(deleteCSonError, { ::DeleteCriticalSection(&cs); });

        // (bytes/sec) * (1 sec/1000 ms) * (x ms/Quantum) == (bytes/quantum)
        bytes_sending_per_quantum = ctsConfig::GetTcpBytesPerSecond() * static_cast<unsigned long long>(ctsConfig::Settings->TcpBytesPerSecondPeriod) / 1000LL;

        // if TCP, will always need a recv buffer for the final ACK 
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
                    // just use the shared buffer to capture the ACK's since recv_count == 0
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


    ctsIOPattern::~ctsIOPattern() throw()
    {
        if (recv_rio_bufferid != RIO_INVALID_BUFFERID &&
            recv_rio_bufferid != s_SharedBufferId) {
            ctl::ctRIODeregisterBuffer(recv_rio_bufferid);
        }

        ::DeleteCriticalSection(&cs);
    }

    void ctsIOPattern::register_callback(function<void(const ctsIOTask&)> _callback)
    {
        ctAutoReleaseCriticalSection local_cs(&cs);
        this->callback = _callback;
    }

    ctsIOTask ctsIOPattern::initiate_io() throw()
    {
        ctAutoReleaseCriticalSection local_cs(&this->cs);
        ctsIOTask return_task;
        if (this->protocol_status == MoreData) {
            // only ask the concrete class for the next task if we don't have IO outstanding
            // that *might* satisfy all the bytes we need to transfer
            if ((this->current_transfer + static_cast<ULONGLONG>(this->inflight_bytes)) < this->max_transfer) {
                return_task = this->next_task();
            } else {
                // else, return the default task telling the caller that no more IO needs to be started yet
            }

        } else if (this->protocol_status == RequestFIN) {
            // post one final recv for the zero byte FIN
            ctFatalCondition(
                this->recv_buffer_free_list.empty(),
                L"ctsIOPattern (%p) recv_buffer_free_list is empty", this);

            return_task.ioAction = ctsIOTask::IOAction::Recv;
            if (recv_rio_bufferid != RIO_INVALID_BUFFERID) {
                // RIO must always use the allocated buffers which were registered
                return_task.buffer = *this->recv_buffer_free_list.rbegin();
                this->recv_buffer_free_list.pop_back();
                return_task.rio_bufferid = this->recv_rio_bufferid;
            } else {
                return_task.buffer = s_FinBuffer;
            }
            return_task.buffer_length = s_FinBufferSize;

            // *not* updating inflight_bytes for the FIN request
            this->protocol_status = VerifyFIN;
        }

        return return_task;
    }


    unsigned long ctsIOPattern::verify_io() throw()
    {
        ctAutoReleaseCriticalSection local_cs(&this->cs);

        if ((this->current_transfer > 0) && (this->current_transfer != this->max_transfer)) {
            if (this->current_transfer < this->max_transfer) {
                return ErrorNotAllDataTransferred;
            } else {
                ctAlwaysFatalCondition(
                    L"Fatal Internal error (transferring more than max_transfer): dt %p ctsTraffic!ctsTraffic::ctsIOPattern", this);
            }
        } else if (this->inflight_bytes > 0) {
            return ErrorNotAllDataTransferred;
        }

        return NO_ERROR;
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// complete_io
    ///
    /// updates its internal counters to prepare for the next IO request
    /// - the fact that complete_io was called assumes that the IO was successful
    /// 
    /// _bytes_posted : the number of bytes that were in the io task
    /// _current_transfer : the number of bytes successfully transferred from the task
    /// _status_code: the return code from the prior IO operation [assumes a Win32 error code]
    ///
    /// Returns the current status of the IO operation on this socket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ctsIOPatternStatus ctsIOPattern::complete_io(const ctsIOTask& _original_task, unsigned long _current_transfer, unsigned long _status_code) throw()
    {
        ctAutoReleaseCriticalSection local_cs(&this->cs);
        if (ctsIOTask::IOAction::Recv == _original_task.ioAction &&
            !_original_task.unlisted_buffer) {
            // only add it back if was one of our listed recv buffers that the base class contains
            this->recv_buffer_free_list.push_back(_original_task.buffer);
        }

        //
        // if we have completed the transfer, than any pended IO that was unblocked is not an IO Error
        //
        if ((_status_code != NO_ERROR) && (this->protocol_status != ctsIOPatternStatus::CompletedTransfer)) {
            this->protocol_status = ctsIOPatternStatus::ErrorIOFailed;
            return this->protocol_status;
        }
        // 
        // Checking for an inconsistent internal state 
        // - or the IO returning #'s that are inconsistent with our internal tracking
        // - only compare when not verifying the FIN
        //
        if (this->protocol_status != ctsIOPatternStatus::VerifyFIN) {
            if (_original_task.tracked_io) {
                ctl::ctFatalCondition(
                    (_current_transfer > this->inflight_bytes),
                    L"ctsIOPattern::complete_io() : ctsIOTask (%p) returned more bytes (%u) than were in flight (%llu)",
                    &_original_task, _current_transfer, static_cast<ULONGLONG>(this->inflight_bytes));

                ctl::ctFatalCondition(
                    (_original_task.buffer_length > this->inflight_bytes),
                    L"ctsIOPattern::complete_io() : the ctsIOTask (%p) had requested more bytes (%u) than were in-flight (%llu)\n",
                    &_original_task, _original_task.buffer_length, static_cast<ULONGLONG>(this->inflight_bytes));

                ctl::ctFatalCondition(
                    (_current_transfer > _original_task.buffer_length),
                    L"ctsIOPattern::complete_io() : ctsIOTask (%p) returned more bytes (%u) than were posted (%u)\n",
                    &_original_task, _current_transfer, _original_task.buffer_length);

                if (ctsConfig::Settings->ShouldVerifyBuffers) {
                    switch (_original_task.ioAction) {
                        case ctsIOTask::IOAction::Recv:
                            ctl::ctFatalCondition(
                                _original_task.expected_pattern_offset != this->recv_pattern_offset,
                                L"ctsIOPattern::complete_io() : ctsIOTask (%p) expected_pattern_offset (%u) does not match the current pattern_offset (%llu)",
                                &_original_task, _original_task.expected_pattern_offset, static_cast<ULONGLONG>(this->recv_pattern_offset));

                            if (!this->verify_buffer(_original_task, _current_transfer)) {
                                // immediately exit with failure if the buffer is corrupt
                                return ctsIOPatternStatus::ErrorDataDidNotMatchBitPattern;
                            }
                            this->recv_pattern_offset += _current_transfer;
                            this->recv_pattern_offset %= BufferPatternSize;
                            break;

                        case ctsIOTask::IOAction::Send:
                            this->send_pattern_offset += _current_transfer;
                            this->send_pattern_offset %= BufferPatternSize;
                            break;
                    }
                }
            }
            //
            // notify the derived interface task completed (when not a FIN)
            //
            ctsIOPatternStatus derived_status = this->completed_task(_original_task, _current_transfer);
            if (!ctsIOPatternContinueIO(derived_status)) {
                // exit immediately without further validation if the derived object determines a failure
                this->protocol_status = derived_status;
                return this->protocol_status;
            }
        }
        //
        // update byte counters if we're tracking this IO request
        //
        if (_original_task.tracked_io) {
            this->inflight_bytes -= _original_task.buffer_length;
            this->current_transfer += _current_transfer;
        }
        //
        // Verify Post-condition TCP protocol contracts haven't been violated
        //
        if (ctsConfig::ProtocolType::TCP == ctsConfig::Settings->Protocol) {
            if ((this->current_transfer + static_cast<ULONGLONG>(this->inflight_bytes)) < this->max_transfer) {
                // still more data to transfer unless we have hit an error in an earlier IO
                if (ctsIOPatternStatus::MoreData == this->protocol_status) {
                    // guard against TCP returning 0 bytes before the completion of the transfer
                    // - this situation can occur when the client is gracefully exiting early
                    if (0 == _current_transfer) {
                        this->protocol_status = ctsIOPatternStatus::ErrorNotAllDataTransferred;
                    }
                }

            } else if ((this->current_transfer + static_cast<ULONGLONG>(this->inflight_bytes)) > this->max_transfer) {
                this->protocol_status = ctsIOPatternStatus::ErrorTooMuchDataTransferred;

            } else if (0 == this->inflight_bytes) {
                // all data has now been sent/received: update protocol_status to request & verify a graceful disconnect 
                switch (this->protocol_status) {
                    case ctsIOPatternStatus::MoreData:
                        this->protocol_status = ctsIOPatternStatus::RequestFIN;
                        break;

                    case ctsIOPatternStatus::VerifyFIN:
                        // should have received zero bytes for the final ACK : if we received more, it's a protocol error
                        if (0 == _current_transfer) {
                            this->protocol_status = ctsIOPatternStatus::CompletedTransfer;
                        } else {
                            this->protocol_status = ctsIOPatternStatus::ErrorTooMuchDataTransferred;
                        }
                        break;

                    default:
                        ctl::ctAlwaysFatalCondition(
                            L"ctsIOPattern::complete_io() : invalid state - protocol_status should be MoreData or VerifyFIN [%u]",
                            this->protocol_status);
                }

            } else {
                // there are still inflight bytes which, when completed, will complete the transfer
            }

        } else {
            // Basic UDP post-condition tracking
            if ((this->current_transfer + static_cast<ULONGLONG>(this->inflight_bytes)) == this->max_transfer) {
                if (0 == this->inflight_bytes) {
                    this->protocol_status = ctsIOPatternStatus::CompletedTransfer;
                }
            } else if ((this->current_transfer + static_cast<ULONGLONG>(this->inflight_bytes)) > this->max_transfer) {
                this->protocol_status = ctsIOPatternStatus::ErrorTooMuchDataTransferred;
            } else {
                // current_transfer + inflight_bytes is still less than max_transfer => keep asking for more data
            }
        }

        return this->protocol_status;
    }

    ctsUnsignedLongLong ctsIOPattern::get_total_transfer() const throw()
    {
        return this->max_transfer;
    }

    void ctsIOPattern::set_total_transfer(const ctsUnsignedLongLong& _new_total) throw()
    {
        this->max_transfer = _new_total;
    }

    ctsIOTask ctsIOPattern::tracked_task(ctsIOTask::IOAction _action, unsigned long _max_transfer) throw()
    {
        ctAutoReleaseCriticalSection local_cs(&this->cs);
        ctsIOTask return_task(this->new_task(_action, _max_transfer));
        return_task.tracked_io = true;
        this->inflight_bytes += return_task.buffer_length;
        return return_task;
    }

    ctsIOTask ctsIOPattern::untracked_task(ctsIOTask::IOAction _action, unsigned long _max_transfer) throw()
    {
        ctAutoReleaseCriticalSection local_cs(&this->cs);
        ctsIOTask return_task(this->new_task(_action, _max_transfer));
        return_task.tracked_io = false;
        return return_task;
    }


    ctsIOTask ctsIOPattern::new_task(ctsIOTask::IOAction _action, unsigned long _max_transfer) throw()
    {
        //
        // need to know the # of bytes we have already transfered (or are in-flight)
        // - to properly calculate the # of bytes to transfer in this IO request
        //
        ctsUnsignedLongLong already_transferred = this->current_transfer;
        already_transferred += static_cast<ctsUnsignedLongLong>(this->inflight_bytes);
        //
        // Guard our internal tracking - all protocol logic assumes these rules
        //
        ctl::ctFatalCondition(
            ((already_transferred < this->current_transfer) || (already_transferred < this->inflight_bytes)),
            L"ctsIOPattern internal overflow (already_transferred = this->current_transfer + this->inflight_bytes)\n"
            L"already_transferred: %llu\n"
            L"this->current_transfer: %llu\n"
            L"this->inflight_bytes: %llu\n",
            static_cast<ULONGLONG>(already_transferred),
            static_cast<ULONGLONG>(this->current_transfer),
            static_cast<ULONGLONG>(this->inflight_bytes));

        ctl::ctFatalCondition(
            (already_transferred >= this->max_transfer),
            L"ctsIOPattern internal error: bytes already transferred (%llu) is >= the total we're expected to transfer (%llu)\n",
            static_cast<ULONGLONG>(already_transferred), static_cast<ULONGLONG>(this->max_transfer));

        //
        // with TCP, we need to calculate the buffer size based off bytes remaining
        // with UDP, we're always posting the same size buffer
        //
        ctsUnsignedLongLong new_buffer_size = 0ULL;
        //
        // first: calculate the next buffer size assuming no max ceiling specified by the protocol
        //
        new_buffer_size = min<ctsUnsignedLongLong>(
            this->buffer_size,
            this->max_transfer - already_transferred);
        //
        // second: if the protocol specified a ceiling, recalculate given their ceiling
        //
        if ((_max_transfer > 0) && (_max_transfer < new_buffer_size)) {
            new_buffer_size = _max_transfer;
        }
        //
        // guard against hitting a 32-bit overflow
        //
        ctl::ctFatalCondition(
            (new_buffer_size > MAXDWORD),
            L"ctsIOPattern internal error: next buffer size (%llu) is greater than MAXDWORD (%u)\n",
            static_cast<ULONGLONG>(new_buffer_size), MAXDWORD);
        //
        // build the next IO request with a properly calculated buffer size
        // Send must specify the offset because we must align the patterns that we send
        // Recv must not specify an offset because will always use the entire buffer for the recv
        //
        ctsIOTask return_task;
        if (ctsIOTask::IOAction::Send == _action) {
            //
            // check to see if the send needs to be deferred into the future
            //
            if (ctsConfig::GetTcpBytesPerSecond() > 0) {
                auto current_time_ms(ctl::ctTimer::snap_qpc_msec());
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
                    ctsUnsignedLongLong ms_for_quantums_to_skip = (quantum_ahead_to_schedule - 1) * ctsConfig::Settings->TcpBytesPerSecondPeriod;

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

            return_task.ioAction = ctsIOTask::IOAction::Send;
            return_task.buffer = s_ProtectedSharedBuffer;
            return_task.rio_bufferid = s_SharedBufferId;
            return_task.buffer_length = static_cast<unsigned long>(new_buffer_size);
            return_task.buffer_offset = static_cast<unsigned long>(this->send_pattern_offset);
            return_task.expected_pattern_offset = 0; // The sender shouldn't be validating this

            ctl::ctFatalCondition(
                this->send_pattern_offset >= BufferPatternSize,
                L"pattern_offset being too large means we might walk off the end of our shared buffer");
            ctl::ctFatalCondition(
                return_task.buffer_length + return_task.buffer_offset > s_SharedBufferSize,
                L"Vector overflow");
        } else {
            ctFatalCondition(
                this->recv_buffer_free_list.empty(),
                L"ctsIOPattern (%p) recv_buffer_free_list is empty", this);

            return_task.ioAction = ctsIOTask::IOAction::Recv;
            return_task.buffer = *this->recv_buffer_free_list.rbegin();
            this->recv_buffer_free_list.pop_back();

            return_task.rio_bufferid = this->recv_rio_bufferid;
            return_task.buffer_length = static_cast<unsigned long>(new_buffer_size);
            return_task.buffer_offset = 0; // always recv to the beginning of the buffer
            return_task.expected_pattern_offset = static_cast<unsigned long>(this->recv_pattern_offset);

            ctl::ctFatalCondition(
                this->recv_pattern_offset >= BufferPatternSize,
                L"pattern_offset being too large means we might walk off the end of our shared buffer");
            ctl::ctFatalCondition(
                return_task.buffer_length + return_task.buffer_offset > this->buffer_size,
                L"Vector overflow");
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
        ctsIOPatternImpl(ctsConfig::IsListening() ? 0 : ctsConfig::Settings->PrePostRecvs),
        io_needed(ctsConfig::IsListening() ? 1 : ctsConfig::Settings->PrePostRecvs),
        sending(ctsConfig::IsListening())
    {
    }
    ctsIOPatternPull::~ctsIOPatternPull() throw()
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
    ctsIOTask ctsIOPatternPull::next_task() throw()
    {
        if (this->io_needed > 0) {
            --this->io_needed;
            ctsIOTask::IOAction next_ioaction;
            if (this->sending) {
                next_ioaction = ctsIOTask::IOAction::Send;
            } else {
                next_ioaction = ctsIOTask::IOAction::Recv;
            }
            return this->tracked_task(next_ioaction);
        } else {
            return ctsIOTask();
        }
    }
    ctsIOPatternStatus ctsIOPatternPull::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) throw()
    {
        if (ctsIOTask::IOAction::Send == _task.ioAction) {
            ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_completed_bytes);
            this->stats.bytes_sent.add(_completed_bytes);
        } else {
            ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_completed_bytes);
            this->stats.bytes_recv.add(_completed_bytes);
        }

        ++this->io_needed;
        return MoreData;
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
        ctsIOPatternImpl(ctsConfig::IsListening() ? ctsConfig::Settings->PrePostRecvs : 0),
        io_needed(ctsConfig::IsListening() ? ctsConfig::Settings->PrePostRecvs : 1),
        sending(!ctsConfig::IsListening())
    {
    }
    ctsIOPatternPush::~ctsIOPatternPush() throw()
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
    ctsIOTask ctsIOPatternPush::next_task() throw()
    {
        if (this->io_needed > 0) {
            --this->io_needed;
            ctsIOTask::IOAction next_ioaction;
            if (this->sending) {
                next_ioaction = ctsIOTask::IOAction::Send;
            } else {
                next_ioaction = ctsIOTask::IOAction::Recv;
            }
            return this->tracked_task(next_ioaction);
        } else {
            return ctsIOTask();
        }
    }
    ctsIOPatternStatus ctsIOPatternPush::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) throw()
    {
        if (ctsIOTask::IOAction::Send == _task.ioAction) {
            ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_completed_bytes);
            this->stats.bytes_sent.add(_completed_bytes);
        } else {
            ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_completed_bytes);
            this->stats.bytes_recv.add(_completed_bytes);
        }

        ++this->io_needed;
        return MoreData;
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
        ctsIOPatternImpl(1), // currently not supporting >1 concurrent IO requests
        push_segment_size(ctsConfig::Settings->PushBytes),
        pull_segment_size(ctsConfig::Settings->PullBytes),
        intra_segment_transfer(0ULL),
        listening(ctsConfig::IsListening()),
        io_needed(true),
        sending(!ctsConfig::IsListening()) // start with clients sending, servers receiving
    {
    }
    ctsIOPatternPushPull::~ctsIOPatternPushPull() throw()
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
    ctsIOTask ctsIOPatternPushPull::next_task() throw()
    {
        ctsUnsignedLong segment_size;
        if (this->listening) {
            // server role is opposite client
            segment_size = (this->sending) ? this->pull_segment_size : this->push_segment_size;
        } else {
            segment_size = (this->sending) ? this->push_segment_size : this->pull_segment_size;
        }

        ctl::ctFatalCondition(
            (this->intra_segment_transfer >= segment_size),
            L"Invalid ctsIOPatternPushPull state: intra_segment_transfer (%lu), segment_size (%lu)\n",
            static_cast<unsigned long>(this->intra_segment_transfer),
            static_cast<unsigned long>(segment_size));

        if (this->io_needed) {
            this->io_needed = false;

            if (this->sending) {
                return this->tracked_task(
                    ctsIOTask::IOAction::Send,
                    segment_size - this->intra_segment_transfer);
            } else {
                return this->tracked_task(
                    ctsIOTask::IOAction::Recv,
                    segment_size - this->intra_segment_transfer);
            }
        } else {
            return ctsIOTask();
        }
    }
    ctsIOPatternStatus ctsIOPatternPushPull::completed_task(const ctsIOTask& _task, unsigned long _current_transfer) throw()
    {
        if (ctsIOTask::IOAction::Send == _task.ioAction) {
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

        ctl::ctFatalCondition(
            (this->intra_segment_transfer > segment_size),
            L"Invalid ctsIOPatternPushPull state: intra_segment_transfer (%lu), segment_size (%lu)\n",
            static_cast<unsigned long>(this->intra_segment_transfer),
            static_cast<unsigned long>(segment_size));

        if (segment_size == this->intra_segment_transfer) {
            this->sending = !this->sending;
            this->intra_segment_transfer = 0;
        }

        return MoreData;
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
        ctsIOPatternImpl(ctsConfig::Settings->PrePostRecvs),
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
    ctsIOPatternDuplex::~ctsIOPatternDuplex() throw()
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
    ctsIOTask ctsIOPatternDuplex::next_task() throw()
    {
        ctsIOTask return_task;

        // since we can have multiple receives in flight, must also check that we have remaining_recv_bytes
        if (this->recv_needed > 0 && this->remaining_recv_bytes > 0) {
            /// for very large transfers, we need to ensure our SafeInt<long long> doesn't overflow
            /// - when we cast it to unsigned long
            return_task = this->tracked_task(
                ctsIOTask::IOAction::Recv,
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
                ctsIOTask::IOAction::Send,
                remaining_send_bytes > MAXLONG ? MAXLONG : static_cast<unsigned long>(this->remaining_send_bytes));
            // as above, this logic was added to avoid over-subscription for remaining send bytes when send_needed > 1
            this->remaining_send_bytes -= return_task.buffer_length;
            --this->send_needed;
        } else {
            // no IO needed now: return the default task
        }

        return return_task;
    }
    ctsIOPatternStatus ctsIOPatternDuplex::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) throw()
    {
        switch (_task.ioAction) {
            case ctsIOTask::Send:
                ctsConfig::Settings->TcpStatusDetails.bytes_sent.add(_completed_bytes);
                this->stats.bytes_sent.add(_completed_bytes);

                // first, we need to adjust the total back from our over-subscription guard when this task was created
                this->remaining_send_bytes += _task.buffer_length;
                // then we need to subtract back out the actual number of bytes sent
                this->remaining_send_bytes -= _completed_bytes;
                ++this->send_needed;
                break;

            case ctsIOTask::Recv:
                ctsConfig::Settings->TcpStatusDetails.bytes_recv.add(_completed_bytes);
                this->stats.bytes_recv.add(_completed_bytes);

                // first, we need to adjust the total back from our over-subscription guard when this task was created
                this->remaining_recv_bytes += _task.buffer_length;
                // then we need to subtract back out the actual number of bytes received
                this->remaining_recv_bytes -= _completed_bytes;
                ++this->recv_needed;
                break;
        }
        return MoreData;
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
        ctsIOPatternImpl(0), // the pattern will recv data OOB from within the protocol implementation
        frame_size_bytes(ctsConfig::GetMediaStream().FrameSizeBytes),
        current_frame_requested(0),
        current_frame_completed(0),
        frame_rate_fps(ctsConfig::GetMediaStream().FramesPerSecond),
        current_frame(1),
        base_time_milliseconds(ctTimer::snap_qpc_msec())
    {
    }
    ctsIOPatternMediaStreamServer::~ctsIOPatternMediaStreamServer()
    {
    }
    // required virtual functions
    ctsIOTask ctsIOPatternMediaStreamServer::next_task()
    {
        ctsIOTask return_task;
        if (current_frame_requested < frame_size_bytes) {
            return_task = this->tracked_task(ctsIOTask::IOAction::Send, frame_size_bytes);
            // calculate the future time to initiate the IO
            // - then subtract the start time to give the difference
            return_task.time_offset_milliseconds =
                this->base_time_milliseconds
                + static_cast<long long>(this->current_frame * 1000 / this->frame_rate_fps)
                - ctTimer::snap_qpc_msec();

            current_frame_requested += return_task.buffer_length;
        }
        return return_task;
    }
    ctsIOPatternStatus ctsIOPatternMediaStreamServer::completed_task(const ctsIOTask&, unsigned long _current_transfer) throw()
    {
        ctsConfig::Settings->UdpStatusDetails.bits_received.add(_current_transfer * 8);
        this->stats.bits_received.add(_current_transfer * 8);

        this->current_frame_completed += _current_transfer;
        if (this->current_frame_completed == frame_size_bytes) {
            ++this->current_frame;
            this->current_frame_requested = 0;
            this->current_frame_completed = 0;
        }
        return MoreData;
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

    ///
    /// ctsIOPatternMediaStreamClient will always issue a # of recv's based on the number of TP threads
    /// - this is to ensure that UDP will scale up appropriately even with small numbers of 'connections'
    ///
    ctsIOPatternMediaStreamClient::ctsIOPatternMediaStreamClient() :
        ctsIOPatternImpl(ctsConfig::Settings->PrePostRecvs),
        renderer_timer(nullptr),
        start_timer(nullptr),
        frame_size_bytes(ctsConfig::GetMediaStream().FrameSizeBytes),
        final_frame(ctsConfig::GetMediaStream().StreamLengthFrames),
        initial_buffer_frames(0),
        timer_wheel_offset_frames(0),
        recv_needed(ctsConfig::Settings->PrePostRecvs),
        base_time_milliseconds(ctTimer::snap_qpc_msec()),
        tracking_resend_sequence_number(1LL),
        frame_rate_ms_per_frame(1000.0 / static_cast<unsigned long>(ctsConfig::GetMediaStream().FramesPerSecond)),
        frame_entries(),
        head_entry(),
        send_buffers()
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
            throw ctl::ctException(
                ERROR_INVALID_DATA,
                L"BufferDepth & FrameSize don't allow for enough buffered stream",
                L"ctsIOPatternMediaStreamClient",
                false);
        }
        ctsConfig::PrintDebug(L"\t\tctsIOPatternMediaStreamClient - queue size for this new connection is %d\n", static_cast<long>(queue_size));

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

        // once the TP timer is created, start it for the next timer based off this->timer_wheel_offset_frames
        this->base_lock();
        this->set_next_start_timer();
        this->set_next_timer();
        this->base_unlock();
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
        // defaulting to an empty task (do nothing)
        ctsIOTask return_task;
        if (this->recv_needed > 0) {
            // don't try posting more than 64000 at a time
            unsigned long max_size_buffer = 0;
            if (this->frame_size_bytes > UdpDatagramMaximumSizeBytes) {
                max_size_buffer = UdpDatagramMaximumSizeBytes;
            } else {
                max_size_buffer = this->frame_size_bytes;
            }

            return_task = this->untracked_task(ctsIOTask::IOAction::Recv, max_size_buffer);
            // always write in a zero for the seq number to initialize the buffer
            *(reinterpret_cast<long long*>(return_task.buffer)) = 0LL;
            --this->recv_needed;
        }
        return return_task;
    }

    ctsIOPatternStatus ctsIOPatternMediaStreamClient::completed_task(const ctsIOTask& _task, unsigned long _completed_bytes) throw()
    {
        if (_task.ioAction == ctsIOTask::IOAction::Recv) {
            // since a recv completed, will need to request another
            ++this->recv_needed;

            // first validate the buffer contents
            ctsIOTask validation_task(_task);
            validation_task.buffer_offset = 24; // skip the first 24 bytes since we use them for our own stuff
            validation_task.buffer_length -= 24; // 24 bytes of the buffer were used with our own data
            if (!this->verify_buffer(validation_task, _completed_bytes - 24)) {
                // exit early if the buffers don't match
                return ctsIOPatternStatus::ErrorDataDidNotMatchBitPattern;
            }

            long long buffered_seq_number = *reinterpret_cast<long long*>(_task.buffer);
            if (buffered_seq_number > this->final_frame) {
                ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                this->stats.error_frames.increment();

                ctsConfig::PrintDebug(
                    L"[%.3f] MediaStreamClient recevieved **an unknown** seq number (%lld) (outside the final frame %lu)\n",
                    ctsConfig::GetStatusTimeStamp(),
                    buffered_seq_number,
                    this->final_frame);
            } else {
                // track the # of *bits* received
                ctsConfig::Settings->UdpStatusDetails.bits_received.add(_completed_bytes * 8);
                this->stats.bits_received.add(_completed_bytes * 8);

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

                    auto found_slot = this->find_sequence_number(buffered_seq_number);
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
                            found_slot->receiver_qpf = ctl::ctTimer::snap_qpf();
                            found_slot->received += _completed_bytes;

                            ctsConfig::PrintDebug(
                                L"\t\tctsIOPatternMediaStreamClient received seq number %lld (%lu bytes)\n",
                                static_cast<long long>(found_slot->sequence_number),
                                static_cast<unsigned long>(found_slot->received));

                            // stop the timer once we receive the last frame
                            // - it's not perfect (e.g. might have received them out of order)
                            // - but it will be very close for tracking the total bits/sec
                            if (static_cast<unsigned long>(buffered_seq_number) == this->final_frame) {
                                this->end_pattern();
                            }

                        } else {
                            ctsConfig::Settings->UdpStatusDetails.duplicate_frames.increment();
                            this->stats.duplicate_frames.increment();

                            ctsConfig::PrintDebug(
                                L"[%.3f] MediaStreamClient received **a duplicate frame** for seq number (%lld)\n",
                                ctsConfig::GetStatusTimeStamp(),
                                buffered_seq_number);
                        }
                    } else {
                        ctsConfig::Settings->UdpStatusDetails.error_frames.increment();
                        this->stats.error_frames.increment();

                        if (buffered_seq_number < this->head_entry->sequence_number) {
                            ctsConfig::PrintDebug(
                                L"\t\tctsIOPatternMediaStreamClient::completed_task returned ctsIOTask (%p)\n",
                                &_task);
                            ctsConfig::PrintDebug(
                                L"[%.3f] MediaStreamClient received **a stale** seq number (%lld) - current seq number (%lld)\n",
                                ctsConfig::GetStatusTimeStamp(),
                                buffered_seq_number,
                                static_cast<long long>(this->head_entry->sequence_number));
                        } else {
                            ctsConfig::PrintDebug(
                                L"[%.3f] MediaStreamClient recevieved **a future** seq number (%lld)\n",
                                ctsConfig::GetStatusTimeStamp(),
                                buffered_seq_number);
                        }
                    }
                }
            }

        } else {  // else process SEND requests
            // process the DONE request 
            if (0 == ::memcmp("DONE", _task.buffer, min<unsigned long>(4, _task.buffer_length))) {
                // indicate to the caller to abort any pended recv requests: aborting
                ctsIOTask abort_task;
                abort_task.ioAction = ctsIOTask::IOAction::Abort;
                this->send_callback(abort_task);

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
                ctl::ctFatalCondition(
                    found_buffer == end(this->send_buffers),
                    L"ctsIOPatternMediaStreamClient (%p) failed to find its send_buffer",
                    this);
                this->send_buffers.erase(found_buffer);
            }
        }

        return MoreData;
    }

    ///
    /// Returns an iterator within frame_entries pointing to the FrameEntry
    ///   matching the specified sequence number.
    /// If the sequence number was not found, will return end(frame_entries)
    ///
    _Requires_lock_held_(cs)
    vector<ctsIOPatternMediaStreamClient::FrameEntry>::iterator ctsIOPatternMediaStreamClient::find_sequence_number(long long _seq_number) throw()
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
    bool ctsIOPatternMediaStreamClient::received_buffered_frames() throw()
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
    void ctsIOPatternMediaStreamClient::set_next_timer() throw()
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
    void ctsIOPatternMediaStreamClient::set_next_start_timer() throw()
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
    void ctsIOPatternMediaStreamClient::render_frame() throw()
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

        ctsConfig::PrintDebug(L"\t\tctsIOPatternMediaStreamClient processing StartCallback\n");

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
            resend_task.ioAction = ctsIOTask::IOAction::Send;
            resend_task.tracked_io = false;
            resend_task.buffer = const_cast<char*>(StartBuffer);
            resend_task.buffer_offset = 0;
            resend_task.buffer_length = static_cast<unsigned long>(::strlen(StartBuffer));
            resend_task.unlisted_buffer = true; // this is our own buffer: the base class should not mess with it

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

        if (ctsConfig::MediaStreamSettings::StreamCodecValues::ResendOnce == ctsConfig::GetMediaStream().StreamCodec) {
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
                                        ctsMediaStreamMessage::Action::RESEND,
                                        resend_iterator->sequence_number));
                                string* resend_string = this_ptr->send_buffers.rbegin()->get();

                                ctsIOTask resend_task;
                                resend_task.ioAction = ctsIOTask::IOAction::Send;
                                resend_task.tracked_io = false;
                                resend_task.buffer = &(*resend_string)[0];
                                resend_task.buffer_offset = 0;
                                resend_task.buffer_length = static_cast<unsigned long>(resend_string->length());
                                resend_task.unlisted_buffer = true; // this buffer is only maintained in the derived object, not the base class
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
                abort_task.ioAction = ctsIOTask::IOAction::FatalAbort;
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
                this_ptr->send_callback(ctsMediaStreamMessage::Construct(ctsMediaStreamMessage::Action::DONE));
            }
        }
    }

} //namespace
