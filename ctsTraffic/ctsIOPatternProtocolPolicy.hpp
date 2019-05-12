/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// project headers
#include "ctsSafeInt.hpp"
#include "ctsIOTask.hpp"
#include "ctsConfig.h"


namespace ctsTraffic {

    enum class ctsIOPatternProtocolTask
    {
        NoIo,
        SendConnectionGuid,
        RecvConnectionGuid,
        MoreIo,
        SendCompletion,
        RecvCompletion,
        GracefulShutdown,
        HardShutdown,
        RequestFIN
    };
    enum class ctsIOPatternProtocolError
    {
        NotProtocolError,
        NoConnectionGuid,  // ctsStatusErrorNoConnectionGuid
        ZeroByteXfer,      // ctsStatusErrorNoDataTransferred
        TooManyBytes,      // ctsStatusErrorTooMuchDataTransferred
        TooFewBytes,       // ctsStatusErrorNotAllDataTransferred
        CorruptedXfer      // ctsStatusErrorDataDidNotMatchBitPattern
    };

    static const unsigned long ctsStatusUnsetErrorCode = MAXUINT; // 4294967296
    static const unsigned long ctsStatusErrorNoConnectionGuid = MAXUINT - 1;
    static const unsigned long ctsStatusErrorNoDataTransferred = MAXUINT - 2;
    static const unsigned long ctsStatusErrorNotAllDataTransferred = MAXUINT - 3;
    static const unsigned long ctsStatusErrorTooMuchDataTransferred = MAXUINT - 4;
    static const unsigned long ctsStatusErrorDataDidNotMatchBitPattern = MAXUINT - 5;
    static const unsigned long ctsStatusMinimumValue = MAXUINT - 5;

    inline ctsIOPatternProtocolError ctsIOPatternStateCheckProtocolError(unsigned long _status) noexcept
    {
        switch (_status) {
            case ctsStatusErrorNoConnectionGuid:
                return ctsIOPatternProtocolError::NoConnectionGuid;

            case ctsStatusErrorNoDataTransferred:
                return ctsIOPatternProtocolError::ZeroByteXfer;

            case ctsStatusErrorNotAllDataTransferred:
                return ctsIOPatternProtocolError::TooFewBytes;

            case ctsStatusErrorTooMuchDataTransferred:
                return ctsIOPatternProtocolError::TooManyBytes;

            case ctsStatusErrorDataDidNotMatchBitPattern:
                return ctsIOPatternProtocolError::CorruptedXfer;

            default:
                return ctsIOPatternProtocolError::NotProtocolError;
        }
    }

    inline const wchar_t* ctsIOPatternBuildProtocolErrorString(unsigned long _status) noexcept
    {
        switch (_status) {
            case ctsStatusErrorNoConnectionGuid:
                return L"Protocol Error: No Connection GUID Transferred";

            case ctsStatusErrorNoDataTransferred:
                return L"Protocol Error: No Data Transferred";

            case ctsStatusErrorNotAllDataTransferred:
                return L"Protocol Error: Not All Data Transferred";

            case ctsStatusErrorTooMuchDataTransferred:
                return L"Protocol Error: Too Much Data Transferred";

            case ctsStatusErrorDataDidNotMatchBitPattern:
                return L"Protocol Error: Data Did Not Match Bit Pattern";

            default:
                ctl::ctAlwaysFatalCondition(
                    L"ctsIOPattern: internal inconsistency - expecting a protocol error ctsIOProtocolState (%u)", _status);
                return nullptr;
        }
    }


    typedef struct ctsIOPatternProtocolTcpClient_t ctsIOPatternProtocolTcpClient;
    typedef struct ctsIOPatternProtocolTcpServer_t ctsIOPatternProtocolTcpServer;
    typedef struct ctsIOPatternProtocolUdp_t       ctsIOPatternProtocolUdp;

    template <typename Protocol>
    class ctsIOPatternProtocolPolicy
    {
    private:
        enum class InternalPatternState
        {
            Initialized,
            MoreIo,
            ServerSendConnectionGuid,
            ClientRecvConnectionGuid,
            ServerSendFinalStatus,
            ClientRecvServerStatus,
            GracefulShutdown,
            HardShutdown,
            RequestFIN,
            CompletedTransfer,
            ErrorIOFailed
        };

        // tracking current bytes 
        ctsUnsignedLongLong confirmed_bytes = 0;
        // need to know when to stop
        ctsUnsignedLongLong max_transfer = 0;
        // need to know in-flight bytes
        ctsUnsignedLongLong inflight_bytes = 0;
        // tracking the pattern state
        mutable InternalPatternState internal_state = InternalPatternState::Initialized;
        // tracking the specific error for this connection
        unsigned long last_error = ctsStatusUnsetErrorCode;
        // track if waiting for the prior state to complete
        mutable bool pended_state = false;

    public:
        ctsIOPatternProtocolPolicy() noexcept
        {
            max_transfer = ctsConfig::GetTransferSize();
        }

        ctsUnsignedLongLong get_remaining_transfer() const noexcept
        {
            //
            // Guard our internal tracking - all protocol logic assumes these rules
            //
            auto already_transferred = this->confirmed_bytes + this->inflight_bytes;

            ctl::ctFatalCondition(
                ((already_transferred < this->confirmed_bytes) || (already_transferred < this->inflight_bytes)),
                L"ctsIOPatternState internal overflow (already_transferred = this->current_transfer + this->inflight_bytes)\n"
                L"already_transferred: %llu\n"
                L"this->current_transfer: %llu\n"
                L"this->inflight_bytes: %llu\n",
                static_cast<ULONGLONG>(already_transferred),
                static_cast<ULONGLONG>(this->confirmed_bytes),
                static_cast<ULONGLONG>(this->inflight_bytes));
            ctl::ctFatalCondition(
                (already_transferred > this->max_transfer),
                L"ctsIOPatternState internal error: bytes already transferred (%llu) is >= the total we're expected to transfer (%llu)\n",
                static_cast<ULONGLONG>(already_transferred), static_cast<ULONGLONG>(this->max_transfer));

            return this->max_transfer - already_transferred;
        }

        ctsUnsignedLongLong get_max_transfer() const noexcept
        {
            return this->max_transfer;
        }

        void set_max_transfer(const ctsUnsignedLongLong& _new_max_transfer) noexcept
        {
            this->max_transfer = _new_max_transfer;
        }

        bool is_completed() const noexcept
        {
            return (InternalPatternState::CompletedTransfer == this->internal_state ||
                    InternalPatternState::ErrorIOFailed == this->internal_state);
        }

        unsigned long update_protocol_error(ctsIOPatternProtocolError _protocol_error) noexcept
        {
            switch (_protocol_error) {
                case ctsIOPatternProtocolError::NoConnectionGuid:
                    return this->update_last_error(ctsStatusErrorNoConnectionGuid);

                case ctsIOPatternProtocolError::CorruptedXfer:
                    return this->update_last_error(ctsStatusErrorDataDidNotMatchBitPattern);

                case ctsIOPatternProtocolError::TooFewBytes:
                    return this->update_last_error(ctsStatusErrorNotAllDataTransferred);

                case ctsIOPatternProtocolError::TooManyBytes:
                    return this->update_last_error(ctsStatusErrorTooMuchDataTransferred);

                case ctsIOPatternProtocolError::ZeroByteXfer:
                    return this->update_last_error(ctsStatusErrorNoDataTransferred);

                default:
                    ctl::ctAlwaysFatalCondition(
                        L"Unknown ctsIOPatternProtocolError : %u", _protocol_error);
                    // will never hit
                    return 0;
            }
        }

        unsigned long update_last_error(unsigned long _error_code) noexcept
        {
            if (this->last_error != ctsStatusUnsetErrorCode) {
                // do nothing: already have the initial error for the connection
                // - this error just came after-the-fact

            } else if (NO_ERROR == _error_code) {
                // a success error code doesn't directly change the internal_state

            } else if (InternalPatternState::CompletedTransfer == this->internal_state) {
                // do nothing: connection is closed: internal state is known-succeeded
                // - ignore the error code

            } else {
                // otherwise, we still have an on-going connection
                // - let the protocol determine how to handle this error given its state
                this->update_error_per_protocol(_error_code);
                if (InternalPatternState::ErrorIOFailed == this->internal_state) {
                    // the protocol determined this IO is now failed - grab the error code
                    this->last_error = _error_code;
                    this->pended_state = false;
                }
            }

            return this->get_last_error();
        }

        unsigned long get_last_error() const noexcept
        {
            // if still running, report no error has been seen
            if (ctsStatusUnsetErrorCode == this->last_error) {
                return NO_ERROR;
            }
            return this->last_error;
        }

        /// 
        /// callers are expected to follow this pattern when working with tasks:
        ///
        /// get_next_task() : returns what is expected next in the protocol
        /// notify_next_task() : updates the state machine with what task is about to be processed
        /// completed_task() : updates the state machine with the result of the processed task
        ///
        ctsIOPatternProtocolTask get_next_task() const noexcept;
        void notify_next_task(const ctsIOTask& _next_task) noexcept;
        void completed_task(const ctsIOTask& _completed_task, unsigned long _completed_transfer_bytes) noexcept;

    private:
        //
        // these private methods are specialized by the Potocol specified in the class template
        //
        ctsIOPatternProtocolTask get_next_task_per_protocol() const noexcept;
        void completed_task_per_protocol(const ctsIOTask& _completed_task, unsigned long _completed_transfer_bytes) noexcept;
        void update_error_per_protocol(DWORD _error_code) const noexcept;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Definitions for the class methods
    /// - including the specialized methods per-protocol
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename Protocol>
    ctsIOPatternProtocolTask ctsIOPatternProtocolPolicy<Protocol>::get_next_task() const noexcept
    {
        //
        // If already indicated the next state, wait for it to complete before giving another task
        //
        if (this->pended_state) {
            return ctsIOPatternProtocolTask::NoIo;
        }
        //
        // All protocols respect max_transfer
        //
        if (InternalPatternState::MoreIo == this->internal_state) {
            if ((this->confirmed_bytes + this->inflight_bytes) < this->max_transfer) {
                return ctsIOPatternProtocolTask::MoreIo;
            } else {
                return ctsIOPatternProtocolTask::NoIo;
            }
        }
        //
        // If already failed, don't continue processing
        //
        if (InternalPatternState::ErrorIOFailed == this->internal_state) {
            return ctsIOPatternProtocolTask::NoIo;
        }

        ctsIOPatternProtocolTask next_task;
        switch (this->internal_state) {
            case InternalPatternState::Initialized:
                if (ctsConfig::IsListening()) {
                    PrintDebugInfo(L"\t\tctsIOPatternState::get_next_task : ServerSendConnectionGuid\n");
                    this->pended_state = true;
                    this->internal_state = InternalPatternState::ServerSendConnectionGuid;
                    next_task = ctsIOPatternProtocolTask::SendConnectionGuid;
                } else {
                    PrintDebugInfo(L"\t\tctsIOPatternState::get_next_task : RecvConnectionGuid\n");
                    this->pended_state = true;
                    this->internal_state = InternalPatternState::ClientRecvConnectionGuid;
                    next_task = ctsIOPatternProtocolTask::RecvConnectionGuid;
                }
                break;

            case InternalPatternState::ServerSendConnectionGuid: // both client and server start IO after the connection ID is shared
            case InternalPatternState::ClientRecvConnectionGuid:
                PrintDebugInfo(L"\t\tctsIOPatternState::get_next_task : MoreIo\n");
                this->internal_state = InternalPatternState::MoreIo;
                next_task = ctsIOPatternProtocolTask::MoreIo;
                break;

            default:
                next_task = this->get_next_task_per_protocol();
        }

        return next_task;
    }

    template <typename Protocol>
    void ctsIOPatternProtocolPolicy<Protocol>::notify_next_task(const ctsIOTask& _next_task) noexcept
    {
        if (_next_task.track_io) {
            this->inflight_bytes += _next_task.buffer_length;
        }
    }

    template <typename Protocol>
    void ctsIOPatternProtocolPolicy<Protocol>::completed_task(const ctsIOTask& _completed_task, unsigned long _completed_transfer_bytes) noexcept
    {
        this->pended_state = false;
        //
        // If already failed, don't continue processing
        //
        if (InternalPatternState::ErrorIOFailed == this->internal_state) {
            return;
        }
        //
        // if failed to receive our connection id, don't continue processing
        //
        if (InternalPatternState::ServerSendConnectionGuid == this->internal_state || InternalPatternState::ClientRecvConnectionGuid == this->internal_state) {
            // must have received the full id otherwise fail early
            if (_completed_transfer_bytes != ctsStatistics::ConnectionIdLength) {
                PrintDebugInfo(
                    L"\t\tctsIOPatternState::completed_task : ErrorIOFailed (TooFewBytes) [transfered %llu, Expected ConnectionID (%u)]\n",
                    static_cast<unsigned long long>(_completed_transfer_bytes),
                    ctsStatistics::ConnectionIdLength);
                this->update_protocol_error(ctsIOPatternProtocolError::NoConnectionGuid);
                return;
            }
        }
        //
        // look to validate the IO
        //
        if (_completed_task.track_io) {
            // 
            // Checking for an inconsistent internal state 
            //
            ctl::ctFatalCondition(
                (_completed_transfer_bytes > this->inflight_bytes),
                L"ctsIOPatternState::completed_task : ctsIOTask (%p) returned more bytes (%u) than were in flight (%llu)",
                &_completed_task, _completed_transfer_bytes, static_cast<unsigned long long>(this->inflight_bytes));
            ctl::ctFatalCondition(
                (_completed_task.buffer_length > this->inflight_bytes),
                L"ctsIOPatternState::completed_task : the ctsIOTask (%p) had requested more bytes (%u) than were in-flight (%llu)\n",
                &_completed_task, _completed_task.buffer_length, static_cast<unsigned long long>(this->inflight_bytes));
            ctl::ctFatalCondition(
                (_completed_transfer_bytes > _completed_task.buffer_length),
                L"ctsIOPatternState::completed_task : ctsIOTask (%p) returned more bytes (%u) than were posted (%u)\n",
                &_completed_task, _completed_transfer_bytes, _completed_task.buffer_length);
            //
            // now update our internal tracking of bytes in-flight / completed
            //
            this->inflight_bytes -= _completed_task.buffer_length;
            this->confirmed_bytes += _completed_transfer_bytes;
        }
        //
        // notify the protocol of the completed task
        //
        this->completed_task_per_protocol(_completed_task, _completed_transfer_bytes);
    }

    template <>
    inline void ctsIOPatternProtocolPolicy<ctsIOPatternProtocolUdp>::update_error_per_protocol(DWORD _error_code) const noexcept
    {
        if (_error_code != 0) {
            PrintDebugInfo(L"\t\tctsIOPatternState::update_error : ErrorIOFailed\n");
            this->internal_state = InternalPatternState::ErrorIOFailed;
        }
    }
    template <>
    inline void ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpClient>::update_error_per_protocol(DWORD _error_code) const noexcept
    {
        if (_error_code != 0 && !this->is_completed()) {
            PrintDebugInfo(L"\t\tctsIOPatternState::update_error : ErrorIOFailed\n");
            this->internal_state = InternalPatternState::ErrorIOFailed;
        }
    }
    template <>
    inline void ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpServer>::update_error_per_protocol(DWORD _error_code) const noexcept
    {
        if (_error_code != 0 && !this->is_completed()) {
            if (InternalPatternState::RequestFIN == this->internal_state &&
                (WSAETIMEDOUT == _error_code || WSAECONNRESET == _error_code || WSAECONNABORTED == _error_code)) {
                // this is actually OK - the client may have just sent a RST instead of a graceful FIN
                this->internal_state = InternalPatternState::CompletedTransfer;
                // must update this->pended since the IO is no longer pended, 
                // - but the class doesn't realize this since we are not moving to a failed internal state
                this->pended_state = false;

            } else {
                PrintDebugInfo(L"\t\tctsIOPatternState::update_error : ErrorIOFailed\n");
                this->internal_state = InternalPatternState::ErrorIOFailed;
            }
        }
    }

    template <>
    inline ctsIOPatternProtocolTask ctsIOPatternProtocolPolicy<ctsIOPatternProtocolUdp>::get_next_task_per_protocol() const noexcept
    {
        // if gets here, the state is either completed or failed
        ctl::ctFatalCondition(
            !this->is_completed(),
            L"ctsIOPatternState::get_next_task was called in an invalid state (%u) - should be completed: dt %p ctsTraffic!ctsTraffic::ctsIOPatternProtocolPolicy<ctsIOPatternProtocolUdp>",
            this->internal_state, this);

        return ctsIOPatternProtocolTask::NoIo;
    }

    template <>
    inline ctsIOPatternProtocolTask ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpClient>::get_next_task_per_protocol() const noexcept
    {
        ctsIOPatternProtocolTask next_task = ctsIOPatternProtocolTask::NoIo;
        switch (this->internal_state) {
            case InternalPatternState::ClientRecvServerStatus:
                this->pended_state = true;
                next_task = ctsIOPatternProtocolTask::RecvCompletion;
                break;

            case InternalPatternState::GracefulShutdown:
                this->pended_state = true;
                next_task = ctsIOPatternProtocolTask::GracefulShutdown;
                break;

            case InternalPatternState::HardShutdown:
                this->pended_state = true;
                next_task = ctsIOPatternProtocolTask::HardShutdown;
                break;

            case InternalPatternState::RequestFIN:
                this->pended_state = true;
                next_task = ctsIOPatternProtocolTask::RequestFIN;
                break;

            case InternalPatternState::CompletedTransfer: // fall-through
            case InternalPatternState::ErrorIOFailed:
                next_task = ctsIOPatternProtocolTask::NoIo;
                break;

            default:
                ctl::ctAlwaysFatalCondition(
                    L"ctsIOPatternState::get_next_task was called in an invalid state (%u): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState",
                    this->internal_state, this);
        }

        return next_task;
    }

    template <>
    inline ctsIOPatternProtocolTask ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpServer>::get_next_task_per_protocol() const noexcept
    {
        ctsIOPatternProtocolTask next_task = ctsIOPatternProtocolTask::NoIo;
        switch (this->internal_state) {
            case InternalPatternState::ServerSendFinalStatus:
                this->pended_state = true;
                next_task = ctsIOPatternProtocolTask::SendCompletion;
                break;

            case InternalPatternState::RequestFIN:
                this->pended_state = true;
                next_task = ctsIOPatternProtocolTask::RequestFIN;
                break;

            case InternalPatternState::CompletedTransfer: // fall-through
            case InternalPatternState::ErrorIOFailed:
                next_task = ctsIOPatternProtocolTask::NoIo;
                break;

            default:
                ctl::ctAlwaysFatalCondition(
                    L"ctsIOPatternState::get_next_task was called in an invalid state (%u): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState",
                    this->internal_state, this);
        }

        return next_task;
    }

    template <>
    inline void ctsIOPatternProtocolPolicy<ctsIOPatternProtocolUdp>::completed_task_per_protocol(const ctsIOTask& _completed_task, unsigned long) noexcept
    {
        // UDP pattern is not concerned about in-flight bytes
        // - this function is only concerned about bytes that have been completed to determine completion

        // Udp just tracks bytes
        if (this->confirmed_bytes < this->max_transfer) {
            // the common case - check this first

        } else if (this->confirmed_bytes == this->max_transfer) {
            this->internal_state = InternalPatternState::CompletedTransfer;

        } else if (this->confirmed_bytes > this->max_transfer) {
            PrintDebugInfo(
                L"\t\tctsIOPatternState::completed_task : ErrorIOFailed (TooManyBytes) [transferred %llu, expected transfer %llu]\n",
                static_cast<unsigned long long>(this->confirmed_bytes),
                static_cast<unsigned long long>(this->max_transfer));
            this->update_protocol_error(ctsIOPatternProtocolError::TooManyBytes);
        }
    }

    template <>
    inline void ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpClient>::completed_task_per_protocol(const ctsIOTask& _completed_task, unsigned long _completed_transfer_bytes) noexcept
    {
        const auto already_transferred = this->confirmed_bytes + this->inflight_bytes;
        //
        // Tcp has a full state machine
        //
        if (already_transferred < this->max_transfer) {
            // guard against the client gracefully exiting before the completion of the transfer
            if (0 == _completed_transfer_bytes) {
                PrintDebugInfo(
                    L"\t\tctsIOPatternState::completed_task : ErrorIOFailed (TooFewBytes) [transferred %llu, expected transfer %llu]\n",
                    static_cast<unsigned long long>(already_transferred),
                    static_cast<unsigned long long>(this->max_transfer));
                if (0 == already_transferred) {
                    this->update_protocol_error(ctsIOPatternProtocolError::ZeroByteXfer);
                } else {
                    this->update_protocol_error(ctsIOPatternProtocolError::TooFewBytes);
                }
            }

        } else if (already_transferred == this->max_transfer) {
            // With TCP, if inflight_bytes > 0, we are not yet done
            // - we need to wait for that pended IO to complete
            if (0 == this->inflight_bytes) {
                // clients will recv the server status, then process their shutdown sequence
                switch (this->internal_state) {
                    case InternalPatternState::MoreIo:
                        PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : ClientRecvServerStatus\n");
                        this->internal_state = InternalPatternState::ClientRecvServerStatus;
                        break;

                    case InternalPatternState::ClientRecvServerStatus:
                        // process the server's returned status
                        if (_completed_transfer_bytes != 4) {
                            PrintDebugInfo(
                                L"\t\tctsIOPatternState::completed_task : ErrorIOFailed (Server didn't return a completion - returned %u bytes)\n",
                                _completed_transfer_bytes);
                            this->update_protocol_error(ctsIOPatternProtocolError::TooFewBytes);

                        } else {
                            if (ctsConfig::TcpShutdownType::GracefulShutdown == ctsConfig::Settings->TcpShutdown) {
                                PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : GracefulShutdown\n");
                                this->internal_state = InternalPatternState::GracefulShutdown;
                            } else {
                                PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : HardShutdown\n");
                                this->internal_state = InternalPatternState::HardShutdown;
                            }
                        }
                        break;

                    case InternalPatternState::GracefulShutdown:
                        PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : RequestFIN\n");
                        this->internal_state = InternalPatternState::RequestFIN;
                        break;

                    case InternalPatternState::HardShutdown:
                        PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : CompletedTransfer\n");
                        this->internal_state = InternalPatternState::CompletedTransfer;
                        break;

                    case InternalPatternState::RequestFIN:
                        if (_completed_transfer_bytes != 0) {
                            PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : ErrorIOFailed (TooManyBytes)\n");
                            this->update_protocol_error(ctsIOPatternProtocolError::TooManyBytes);
                        } else {
                            PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : CompletedTransfer\n");
                            this->internal_state = InternalPatternState::CompletedTransfer;
                        }
                        break;

                    default:
                        ctl::ctAlwaysFatalCondition(
                            L"ctsIOPatternState::completed_task - invalid internal_status (%u): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState, dt %p ctsTraffic!ctstraffic::ctsIOTask",
                            this->internal_state, this, &_completed_task);
                }
            }

        } else { // if (already_transferred > this->max_transfer)
            PrintDebugInfo(
                L"\t\tctsIOPatternState::completed_task : ErrorIOFailed (TooManyBytes) [transferred %llu, expected transfer %llu]\n",
                static_cast<unsigned long long>(already_transferred),
                static_cast<unsigned long long>(this->max_transfer));
            this->update_protocol_error(ctsIOPatternProtocolError::TooManyBytes);
        }
    }

    template <>
    inline void ctsIOPatternProtocolPolicy<ctsIOPatternProtocolTcpServer>::completed_task_per_protocol(const ctsIOTask& _completed_task, unsigned long _completed_transfer_bytes) noexcept
    {
        const auto already_transferred = this->confirmed_bytes + this->inflight_bytes;
        //
        // Tcp has a full state machine
        //
        if (already_transferred < this->max_transfer) {
            // guard against the client gracefully exiting before the completion of the transfer
            if (0 == _completed_transfer_bytes) {
                PrintDebugInfo(
                    L"\t\tctsIOPatternState::completed_task : ErrorIOFailed (TooFewBytes) [transferred %llu, expected transfer %llu]\n",
                    static_cast<unsigned long long>(already_transferred),
                    static_cast<unsigned long long>(this->max_transfer));
                if (0 == already_transferred) {
                    this->update_protocol_error(ctsIOPatternProtocolError::ZeroByteXfer);
                } else {
                    this->update_protocol_error(ctsIOPatternProtocolError::TooFewBytes);
                }
            }

        } else if (already_transferred == this->max_transfer) {
            // With TCP, if inflight_bytes > 0, we are not yet done
            // - we need to wait for that pended IO to complete
            if (0 == this->inflight_bytes) {
                // servers will first send their final status before starting their shutdown sequence
                switch (this->internal_state) {
                    case InternalPatternState::MoreIo:
                        PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : ServerSendFinalStatus\n");
                        this->internal_state = InternalPatternState::ServerSendFinalStatus;
                        break;

                    case InternalPatternState::ServerSendFinalStatus:
                        PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : RequestFIN\n");
                        this->internal_state = InternalPatternState::RequestFIN;
                        break;

                    case InternalPatternState::RequestFIN:
                        if (_completed_transfer_bytes != 0) {
                            PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : ErrorIOFailed (TooManyBytes)\n");
                            this->update_protocol_error(ctsIOPatternProtocolError::TooManyBytes);
                        } else {
                            PrintDebugInfo(L"\t\tctsIOPatternState::completed_task : CompletedTransfer\n");
                            this->internal_state = InternalPatternState::CompletedTransfer;
                        }
                        break;

                    default:
                        ctl::ctAlwaysFatalCondition(
                            L"ctsIOPatternState::completed_task - invalid internal_status (%u): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState",
                            this->internal_state, this);
                }
            }

        } else { // if (already_transferred > this->max_transfer)
            PrintDebugInfo(
                L"\t\tctsIOPatternState::completed_task : ErrorIOFailed (TooManyBytes) [transferred %llu, expected transfer %llu]\n",
                static_cast<unsigned long long>(already_transferred),
                static_cast<unsigned long long>(this->max_transfer));
            this->update_protocol_error(ctsIOPatternProtocolError::TooManyBytes);
        }
    }
}
