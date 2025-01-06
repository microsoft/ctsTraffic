/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// project headers
#include "ctsIOTask.hpp"
#include "ctsConfig.h"

namespace ctsTraffic
{
    enum class ctsIoPatternType : std::uint8_t
    {
        NoIo,
        SendConnectionGuid,
        RecvConnectionGuid,
        MoreIo,
        SendCompletion,
        RecvCompletion,
        GracefulShutdown,
        HardShutdown,
        RequestFin
    };

    enum class ctsIoPatternError : std::uint8_t
    {
        NotProtocolError,
        NoConnectionGuid,
        ZeroByteXfer,
        TooManyBytes,
        TooFewBytes,
        CorruptedXfer
    };

    constexpr uint32_t c_statusUnsetErrorCode = MAXUINT; // 4294967296
    constexpr uint32_t c_statusErrorNoConnectionGuid = MAXUINT - 1;
    constexpr uint32_t c_statusErrorNoDataTransferred = MAXUINT - 2;
    constexpr uint32_t c_statusErrorNotAllDataTransferred = MAXUINT - 3;
    constexpr uint32_t c_statusErrorTooMuchDataTransferred = MAXUINT - 4;
    constexpr uint32_t c_statusErrorDataDidNotMatchBitPattern = MAXUINT - 5;
    constexpr uint32_t c_statusMinimumValue = MAXUINT - 5;

    inline ctsIoPatternError ctsIoPatternStateCheckProtocolError(uint32_t status) noexcept
    {
        switch (status)
        {
        case c_statusErrorNoConnectionGuid:
            return ctsIoPatternError::NoConnectionGuid;

        case c_statusErrorNoDataTransferred:
            return ctsIoPatternError::ZeroByteXfer;

        case c_statusErrorNotAllDataTransferred:
            return ctsIoPatternError::TooFewBytes;

        case c_statusErrorTooMuchDataTransferred:
            return ctsIoPatternError::TooManyBytes;

        case c_statusErrorDataDidNotMatchBitPattern:
            return ctsIoPatternError::CorruptedXfer;

        default:
            return ctsIoPatternError::NotProtocolError;
        }
    }

    inline const wchar_t* ctsIoPatternBuildProtocolErrorString(uint32_t status) noexcept
    {
        switch (status)
        {
        case c_statusErrorNoConnectionGuid:
            return L"Protocol Error: No Connection GUID Transferred";

        case c_statusErrorNoDataTransferred:
            return L"Protocol Error: No Data Transferred";

        case c_statusErrorNotAllDataTransferred:
            return L"Protocol Error: Not All Data Transferred";

        case c_statusErrorTooMuchDataTransferred:
            return L"Protocol Error: Too Much Data Transferred";

        case c_statusErrorDataDidNotMatchBitPattern:
            return L"Protocol Error: Data Did Not Match Bit Pattern";

        default:
            FAIL_FAST_MSG(
                "ctsIOPattern: internal inconsistency - expecting a protocol error ctsIOProtocolState (%u)", status);
        }
    }


    using ctsIoPatternProtocolTcpClient = struct ctsIOPatternProtocolTcpClient_t;
    using ctsIoPatternProtocolTcpServer = struct ctsIOPatternProtocolTcpServer_t;
    using ctsIoPatternProtocolUdp = struct ctsIOPatternProtocolUdp_t;

    template <typename Protocol>
    class ctsIoPatternProtocolPolicy
    {
    private:
        enum class InternalPatternState : std::uint8_t
        {
            Initialized,
            MoreIo,
            ServerSendConnectionGuid,
            ClientRecvConnectionGuid,
            ServerSendFinalStatus,
            ClientRecvServerStatus,
            GracefulShutdown,
            HardShutdown,
            RequestFin,
            CompletedTransfer,
            ErrorIoFailed
        };

        // tracking current bytes 
        uint64_t m_confirmedBytes = 0;
        // need to know when to stop
        uint64_t m_maxTransfer = 0;
        // need to know in-flight bytes
        uint64_t m_inFlightBytes = 0;
        // tracking the pattern state
        mutable InternalPatternState m_internalState = InternalPatternState::Initialized;
        // tracking the specific error for this connection
        uint32_t m_lastError = c_statusUnsetErrorCode;
        // track if waiting for the prior state to complete
        mutable bool m_pendedState = false;

    public:
        ctsIoPatternProtocolPolicy() noexcept :
            m_maxTransfer(ctsConfig::GetTransferSize())
        {
        }

        uint64_t GetRemainingTransfer() const noexcept
        {
            //
            // Guard our internal tracking - all protocol logic assumes these rules
            //
            const auto alreadyTransferred = m_confirmedBytes + m_inFlightBytes;

            FAIL_FAST_IF_MSG(
                alreadyTransferred < m_confirmedBytes || alreadyTransferred < m_inFlightBytes,
                "ctsIOPatternState internal overflow (already_transferred = m_confirmedBytes + m_inFlightBytes)\n"
                "already_transferred: %llu\n"
                "m_confirmedBytes: %llu\n"
                "m_inFlightBytes: %llu\n",
                alreadyTransferred,
                m_confirmedBytes,
                m_inFlightBytes);
            FAIL_FAST_IF_MSG(
                alreadyTransferred > m_maxTransfer,
                "ctsIOPatternState internal error: bytes already transferred (%llu) is >= the total we're expected to transfer (%llu)\n",
                alreadyTransferred, m_maxTransfer);

            return m_maxTransfer - alreadyTransferred;
        }

        uint64_t GetMaxTransfer() const noexcept
        {
            return m_maxTransfer;
        }

        void SetMaxTransfer(uint64_t newMaxTransfer) noexcept
        {
            m_maxTransfer = newMaxTransfer;
        }

        bool IsCompleted() const noexcept
        {
            return m_internalState == InternalPatternState::CompletedTransfer ||
                m_internalState == InternalPatternState::ErrorIoFailed;
        }

        uint32_t UpdateProtocolError(ctsIoPatternError protocolError) noexcept
        {
            switch (protocolError)
            {
            case ctsIoPatternError::NoConnectionGuid:
                return UpdateLastError(c_statusErrorNoConnectionGuid);

            case ctsIoPatternError::CorruptedXfer:
                return UpdateLastError(c_statusErrorDataDidNotMatchBitPattern);

            case ctsIoPatternError::TooFewBytes:
                return UpdateLastError(c_statusErrorNotAllDataTransferred);

            case ctsIoPatternError::TooManyBytes:
                return UpdateLastError(c_statusErrorTooMuchDataTransferred);

            case ctsIoPatternError::ZeroByteXfer:
                return UpdateLastError(c_statusErrorNoDataTransferred);

            default:
                FAIL_FAST_MSG(
                    "Unknown ctsIoPatternError : %d", protocolError);
            }
        }

        uint32_t UpdateLastError(uint32_t errorCode) noexcept
        {
            if (m_lastError != c_statusUnsetErrorCode)
            {
                // do nothing: already have the initial error for the connection
                // - this error just came after-the-fact
            }
            else if (NO_ERROR == errorCode)
            {
                // a success error code doesn't directly change the m_internalState
            }
            else if (InternalPatternState::CompletedTransfer == m_internalState)
            {
                // do nothing: connection is closed: internal state is known-succeeded
                // - ignore the error code
            }
            else
            {
                // otherwise, we still have an ongoing connection
                // - let the protocol determine how to handle this error given its state
                UpdateErrorPerProtocol(errorCode);
                if (InternalPatternState::ErrorIoFailed == m_internalState)
                {
                    // the protocol determined this IO is now failed - grab the error code
                    m_lastError = errorCode;
                    m_pendedState = false;
                }
            }

            return GetLastError();
        }

        uint32_t GetLastError() const noexcept
        {
            // if still running, report no error has been seen
            if (c_statusUnsetErrorCode == m_lastError)
            {
                return NO_ERROR;
            }
            return m_lastError;
        }

        // callers are expected to follow this pattern when working with tasks:
        //
        // GetNextPatternType() : returns what is expected next in the protocol
        // NotifyNextTask() : updates the state machine with what task is about to be processed
        // CompletedTask() : updates the state machine with the result of the processed task

        ctsIoPatternType GetNextPatternType() const noexcept;
        void NotifyNextTask(const ctsTask& nextTask) noexcept;
        void CompletedTask(const ctsTask& completedTask, uint32_t completedTransferredBytes) noexcept;

    private:
        //
        // these private methods are specialized by the Protocol specified in the class template
        //
        ctsIoPatternType GetNextPatternTypePerProtocol() const noexcept;
        void CompletedTaskPerProtocol(const ctsTask& completedTask, uint32_t completedTransferredBytes) noexcept;
        void UpdateErrorPerProtocol(DWORD errorCode) const noexcept;
    };

    template <typename Protocol>
    ctsIoPatternType ctsIoPatternProtocolPolicy<Protocol>::GetNextPatternType() const noexcept
    {
        //
        // If already indicated the next state, wait for it to complete before giving another task
        //
        if (m_pendedState)
        {
            return ctsIoPatternType::NoIo;
        }
        //
        // All protocols respect max_transfer
        //
        if (InternalPatternState::MoreIo == m_internalState)
        {
            if (m_confirmedBytes + m_inFlightBytes < m_maxTransfer)
            {
                return ctsIoPatternType::MoreIo;
            }
            return ctsIoPatternType::NoIo;
        }
        //
        // If already failed, don't continue processing
        //
        if (InternalPatternState::ErrorIoFailed == m_internalState)
        {
            return ctsIoPatternType::NoIo;
        }

        ctsIoPatternType nextType;
        switch (m_internalState)
        {
        case InternalPatternState::Initialized:
            if (ctsConfig::IsListening())
            {
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : ServerSendConnectionGuid\n");
                m_pendedState = true;
                m_internalState = InternalPatternState::ServerSendConnectionGuid;
                nextType = ctsIoPatternType::SendConnectionGuid;
            }
            else
            {
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : RecvConnectionGuid\n");
                m_pendedState = true;
                m_internalState = InternalPatternState::ClientRecvConnectionGuid;
                nextType = ctsIoPatternType::RecvConnectionGuid;
            }
            break;

        case InternalPatternState::ServerSendConnectionGuid:
        // both client and server start IO after the connection ID is shared
        case InternalPatternState::ClientRecvConnectionGuid:
            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : MoreIo\n");
            m_internalState = InternalPatternState::MoreIo;
            nextType = ctsIoPatternType::MoreIo;
            break;

        default:
            nextType = GetNextPatternTypePerProtocol();
        }

        return nextType;
    }

    template <typename Protocol>
    void ctsIoPatternProtocolPolicy<Protocol>::NotifyNextTask(const ctsTask& nextTask) noexcept
    {
        if (nextTask.m_trackIo)
        {
            m_inFlightBytes += nextTask.m_bufferLength;
        }
    }

    template <typename Protocol>
    void ctsIoPatternProtocolPolicy<Protocol>::CompletedTask(const ctsTask& completedTask,
                                                             uint32_t completedTransferredBytes) noexcept
    {
        m_pendedState = false;
        //
        // If already failed, don't continue processing
        //
        if (InternalPatternState::ErrorIoFailed == m_internalState)
        {
            return;
        }
        //
        // if failed to receive our connection id, don't continue processing
        //
        if (InternalPatternState::ServerSendConnectionGuid == m_internalState ||
            InternalPatternState::ClientRecvConnectionGuid == m_internalState)
        {
            // must have received the full id otherwise fail early
            if (completedTransferredBytes != ctsStatistics::ConnectionIdLength)
            {
                PRINT_DEBUG_INFO(
                    L"\t\tctsIOPatternState::completedTask : ErrorIoFailed (TooFewBytes) [transferred %u, Expected ConnectionID (%u)]\n",
                    completedTransferredBytes, ctsStatistics::ConnectionIdLength);
                UpdateProtocolError(ctsIoPatternError::NoConnectionGuid);
                return;
            }
        }
        //
        // look to validate the IO
        //
        if (completedTask.m_trackIo)
        {
            // 
            // Checking for an inconsistent internal state 
            //
            FAIL_FAST_IF_MSG(
                completedTransferredBytes > m_inFlightBytes,
                "ctsIOPatternState::completedTask : ctsIOTask (%p) returned more bytes (%u) than were in flight (%llu)",
                &completedTask, completedTransferredBytes, m_inFlightBytes);
            FAIL_FAST_IF_MSG(
                completedTask.m_bufferLength > m_inFlightBytes,
                "ctsIOPatternState::completedTask : the ctsIOTask (%p) had requested more bytes (%u) than were in-flight (%llu)\n",
                &completedTask, completedTask.m_bufferLength, m_inFlightBytes);
            FAIL_FAST_IF_MSG(
                completedTransferredBytes > completedTask.m_bufferLength,
                "ctsIOPatternState::completedTask : ctsIOTask (%p) returned more bytes (%u) than were posted (%u)\n",
                &completedTask, completedTransferredBytes, completedTask.m_bufferLength);
            //
            // now update our internal tracking of bytes in-flight / completed
            //
            m_inFlightBytes -= completedTask.m_bufferLength;
            m_confirmedBytes += completedTransferredBytes;
        }
        //
        // notify the protocol of the completed task
        //
        CompletedTaskPerProtocol(completedTask, completedTransferredBytes);
    }

    template <>
    inline
    void
    ctsIoPatternProtocolPolicy<ctsIoPatternProtocolUdp>::UpdateErrorPerProtocol(DWORD errorCode) const noexcept
    {
        if (errorCode != 0)
        {
            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::update_error : ErrorIoFailed\n");
            m_internalState = InternalPatternState::ErrorIoFailed;
        }
    }

    template <>
    inline
    void
    ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpClient>::UpdateErrorPerProtocol(DWORD errorCode) const noexcept
    {
        if (errorCode != 0 && !IsCompleted())
        {
            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::update_error : ErrorIoFailed\n");
            m_internalState = InternalPatternState::ErrorIoFailed;
        }
    }

    template <>
    inline
    void
    ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpServer>::UpdateErrorPerProtocol(DWORD errorCode) const noexcept
    {
        if (errorCode != 0 && !IsCompleted())
        {
            if (InternalPatternState::RequestFin == m_internalState &&
                (WSAETIMEDOUT == errorCode || WSAECONNRESET == errorCode || WSAECONNABORTED == errorCode))
            {
                // this is actually OK - the client may have just sent an RST instead of a graceful FIN
                m_internalState = InternalPatternState::CompletedTransfer;
                // must update pended since the IO is no longer pended, 
                // - but the class doesn't realize this since we are not moving to a failed internal state
                m_pendedState = false;
            }
            else
            {
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::update_error : ErrorIoFailed\n");
                m_internalState = InternalPatternState::ErrorIoFailed;
            }
        }
    }

    template <>
    inline
    ctsIoPatternType
    ctsIoPatternProtocolPolicy<ctsIoPatternProtocolUdp>::GetNextPatternTypePerProtocol() const noexcept
    {
        // if we get here, the state is either completed or failed
        FAIL_FAST_IF_MSG(
            !IsCompleted(),
            "ctsIOPatternState::get_next_task was called in an invalid state (%d) - should be completed: dt %p ctsTraffic!ctsTraffic::ctsIOPatternProtocolPolicy<ctsIOPatternProtocolUdp>",
            m_internalState, this);

        return ctsIoPatternType::NoIo;
    }

    template <>
    inline
    ctsIoPatternType
    ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpClient>::GetNextPatternTypePerProtocol() const noexcept
    {
        switch (m_internalState)
        {
        case InternalPatternState::ClientRecvServerStatus:
            m_pendedState = true;
            return ctsIoPatternType::RecvCompletion;

        case InternalPatternState::GracefulShutdown:
            m_pendedState = true;
            return ctsIoPatternType::GracefulShutdown;

        case InternalPatternState::HardShutdown:
            m_pendedState = true;
            return ctsIoPatternType::HardShutdown;

        case InternalPatternState::RequestFin:
            m_pendedState = true;
            return ctsIoPatternType::RequestFin;

        case InternalPatternState::CompletedTransfer:
            [[fallthrough]];
        case InternalPatternState::ErrorIoFailed:
            return ctsIoPatternType::NoIo;

        default:
            FAIL_FAST_MSG(
                "ctsIOPatternState::get_next_task was called in an invalid state (%d): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState",
                m_internalState, this);
        }
    }

    template <>
    inline
    ctsIoPatternType
    ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpServer>::GetNextPatternTypePerProtocol() const noexcept
    {
        switch (m_internalState)
        {
        case InternalPatternState::ServerSendFinalStatus:
            m_pendedState = true;
            return ctsIoPatternType::SendCompletion;

        case InternalPatternState::RequestFin:
            m_pendedState = true;
            return ctsIoPatternType::RequestFin;

        case InternalPatternState::CompletedTransfer:
            [[fallthrough]];
        case InternalPatternState::ErrorIoFailed:
            return ctsIoPatternType::NoIo;

        default:
            FAIL_FAST_MSG(
                "ctsIOPatternState::get_next_task was called in an invalid state (%d): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState",
                m_internalState, this);
        }
    }

    template <>
    inline
    void
    ctsIoPatternProtocolPolicy<ctsIoPatternProtocolUdp>::CompletedTaskPerProtocol(const ctsTask&, uint32_t) noexcept
    {
        // UDP pattern is not concerned about in-flight bytes
        // - this function is only concerned about bytes that have been completed to determine completion

        // the common case - check this first
        if (m_confirmedBytes < m_maxTransfer)
        {
            return;
        }

        if (m_confirmedBytes == m_maxTransfer)
        {
            m_internalState = InternalPatternState::CompletedTransfer;
            return;
        }

        if (m_confirmedBytes > m_maxTransfer)
        {
            PRINT_DEBUG_INFO(
                L"\t\tctsIOPatternState::completedTask : ErrorIoFailed (TooManyBytes) [transferred %llu, expected transfer %llu]\n",
                m_confirmedBytes, m_maxTransfer);
            UpdateProtocolError(ctsIoPatternError::TooManyBytes);
        }
    }

    template <>
    inline
    void
    ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpClient>::CompletedTaskPerProtocol(
        const ctsTask& completedTask, uint32_t completedTransferredBytes) noexcept
    {
        const auto alreadyTransferred = m_confirmedBytes + m_inFlightBytes;
        //
        // TCP has a full state machine
        //
        if (alreadyTransferred < m_maxTransfer)
        {
            // guard against the client gracefully exiting before the completion of the transfer
            if (0 == completedTransferredBytes)
            {
                PRINT_DEBUG_INFO(
                    L"\t\tctsIOPatternState::completedTask : ErrorIoFailed (TooFewBytes) [transferred %llu, expected transfer %llu]\n",
                    alreadyTransferred, m_maxTransfer);
                if (0 == alreadyTransferred)
                {
                    UpdateProtocolError(ctsIoPatternError::ZeroByteXfer);
                }
                else
                {
                    UpdateProtocolError(ctsIoPatternError::TooFewBytes);
                }
            }
        }
        else if (alreadyTransferred == m_maxTransfer)
        {
            // With TCP, if inFlightBytes > 0, we are not yet done
            // - we need to wait for that pended IO to complete
            if (0 == m_inFlightBytes)
            {
                // clients will recv the server status, then process their shutdown sequence
                switch (m_internalState)
                {
                case InternalPatternState::MoreIo:
                    PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : ClientRecvServerStatus\n");
                    m_internalState = InternalPatternState::ClientRecvServerStatus;
                    break;

                case InternalPatternState::ClientRecvServerStatus:
                    // process the server's returned status
                    if (completedTransferredBytes != 4)
                    {
                        PRINT_DEBUG_INFO(
                            L"\t\tctsIOPatternState::completedTask : ErrorIoFailed (Server didn't return a completion - returned %u bytes)\n",
                            completedTransferredBytes);
                        UpdateProtocolError(ctsIoPatternError::TooFewBytes);
                    }
                    else
                    {
                        if (ctsConfig::TcpShutdownType::GracefulShutdown == ctsConfig::GetShutdownType())
                        {
                            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : GracefulShutdown\n");
                            m_internalState = InternalPatternState::GracefulShutdown;
                        }
                        else
                        {
                            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : HardShutdown\n");
                            m_internalState = InternalPatternState::HardShutdown;
                        }
                    }
                    break;

                case InternalPatternState::GracefulShutdown:
                    PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : RequestFIN\n");
                    m_internalState = InternalPatternState::RequestFin;
                    break;

                case InternalPatternState::HardShutdown:
                    PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : CompletedTransfer\n");
                    m_internalState = InternalPatternState::CompletedTransfer;
                    break;

                case InternalPatternState::RequestFin:
                    if (completedTransferredBytes != 0)
                    {
                        PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : ErrorIoFailed (TooManyBytes)\n");
                        UpdateProtocolError(ctsIoPatternError::TooManyBytes);
                    }
                    else
                    {
                        PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : CompletedTransfer\n");
                        m_internalState = InternalPatternState::CompletedTransfer;
                    }
                    break;

                default:
                    FAIL_FAST_MSG(
                        "ctsIOPatternState::completedTask - invalid internal state (%d): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState, dt %p ctsTraffic!ctsTraffic::ctsIOTask",
                        m_internalState, this, &completedTask);
                }
            }
        }
        else
        {
            // if (already_transferred > max_transfer)
            PRINT_DEBUG_INFO(
                L"\t\tctsIOPatternState::completedTask : ErrorIoFailed (TooManyBytes) [transferred %llu, expected transfer %llu]\n",
                alreadyTransferred, m_maxTransfer);
            UpdateProtocolError(ctsIoPatternError::TooManyBytes);
        }
    }

    template <>
    inline
    void
    ctsIoPatternProtocolPolicy<ctsIoPatternProtocolTcpServer>::CompletedTaskPerProtocol(
        const ctsTask&, uint32_t completedTransferredBytes) noexcept
    {
        const auto alreadyTransferred = m_confirmedBytes + m_inFlightBytes;
        //
        // TCP has a full state machine
        //
        if (alreadyTransferred < m_maxTransfer)
        {
            // guard against the client gracefully exiting before the completion of the transfer
            if (0 == completedTransferredBytes)
            {
                PRINT_DEBUG_INFO(
                    L"\t\tctsIOPatternState::completedTask : ErrorIoFailed (TooFewBytes) [transferred %llu, expected transfer %llu]\n",
                    alreadyTransferred, m_maxTransfer);
                if (0 == alreadyTransferred)
                {
                    UpdateProtocolError(ctsIoPatternError::ZeroByteXfer);
                }
                else
                {
                    UpdateProtocolError(ctsIoPatternError::TooFewBytes);
                }
            }
        }
        else if (alreadyTransferred == m_maxTransfer)
        {
            // With TCP, if inFlightBytes > 0, we are not yet done
            // - we need to wait for that pended IO to complete
            if (0 == m_inFlightBytes)
            {
                // servers will first send their final status before starting their shutdown sequence
                switch (m_internalState)
                {
                case InternalPatternState::MoreIo:
                    PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : ServerSendFinalStatus\n");
                    m_internalState = InternalPatternState::ServerSendFinalStatus;
                    break;

                case InternalPatternState::ServerSendFinalStatus:
                    PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : RequestFIN\n");
                    m_internalState = InternalPatternState::RequestFin;
                    break;

                case InternalPatternState::RequestFin:
                    if (completedTransferredBytes != 0)
                    {
                        PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : ErrorIoFailed (TooManyBytes)\n");
                        UpdateProtocolError(ctsIoPatternError::TooManyBytes);
                    }
                    else
                    {
                        PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::completedTask : CompletedTransfer\n");
                        m_internalState = InternalPatternState::CompletedTransfer;
                    }
                    break;

                default:
                    FAIL_FAST_MSG(
                        "ctsIOPatternState::completedTask - invalid internal state (%d): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState",
                        m_internalState, this);
                }
            }
        }
        else
        {
            // if (already_transferred > max_transfer)
            PRINT_DEBUG_INFO(
                L"\t\tctsIOPatternState::completedTask : ErrorIoFailed (TooManyBytes) [transferred %llu, expected transfer %llu]\n",
                alreadyTransferred, m_maxTransfer);
            UpdateProtocolError(ctsIoPatternError::TooManyBytes);
        }
    }
}
