/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include <Windows.h>
// project headers
#include "ctsSafeInt.hpp"
#include "ctsIOTask.hpp"
#include "ctsConfig.h"

namespace ctsTraffic
{
    constexpr auto* const c_completionMessage = "DONE";
    constexpr uint32_t c_completionMessageSize = 4;

    enum class ctsIoPatternType
    {
        NoIo,
        SendConnectionId,
        RecvConnectionId,
        MoreIo,
        SendCompletion,
        RecvCompletion,
        GracefulShutdown,
        HardShutdown,
        RequestFin
    };
    enum class ctsIoPatternError
    {
        NoError,
        TooManyBytes,
        TooFewBytes,
        CorruptedBytes,
        ErrorIoFailed,
        SuccessfullyCompleted
    };

    class ctsIoPatternState
    {
        enum class InternalPatternState
        {
            Initialized,
            MoreIo,
            ServerSendConnectionId,
            ClientRecvConnectionId,
            ServerSendCompletion,
            ClientRecvCompletion,
            GracefulShutdown,  // TCP: instruct the function to call shutdown(SD_SEND) on the socket
            HardShutdown,      // TCP: force a RST instead of a 4-way-FIN
            RequestFin,        // TCP: next ask for IO will be a recv for the zero-byte FIN
            CompletedTransfer,
            ErrorIoFailed
        };

        // tracking current bytes 
        uint64_t m_confirmedBytes = 0ULL;
        // need to know when to stop
        uint64_t m_maxTransfer = ctsConfig::GetTransferSize();
        // need to know in-flight bytes
        uint64_t m_inflightBytes = 0UL;
        // ideal send backlog value
        uint32_t m_idealSendbacklog = ctsConfig::g_configSettings->PrePostSends == 0 ?
            ctsConfig::GetMaxBufferSize() :
            ctsConfig::GetMaxBufferSize() * ctsConfig::g_configSettings->PrePostSends;

        InternalPatternState m_internalState = InternalPatternState::Initialized;
        // track if waiting for the prior state to complete
        bool m_pendedState = false;

    public:
        ctsIoPatternState() noexcept;

        [[nodiscard]] uint64_t GetRemainingTransfer() const noexcept;

        [[nodiscard]] uint64_t GetMaxTransfer() const noexcept;
        void SetMaxTransfer(uint64_t maxTransfer) noexcept;

        [[nodiscard]] uint32_t GetIdealSendBacklog() const noexcept;
        void SetIdealSendBacklog(uint32_t newIsb) noexcept;

        [[nodiscard]] bool IsCompleted() const noexcept;

        [[nodiscard]] bool IsCurrentStateMoreIo() const noexcept;
        ctsIoPatternType GetNextPatternType() noexcept;
        void NotifyNextTask(const ctsTask& nextTask) noexcept;
        ctsIoPatternError CompletedTask(const ctsTask& completedTask, uint32_t completedTransferBytes) noexcept;

        ctsIoPatternError UpdateError(DWORD error) noexcept;
    };


    inline ctsIoPatternState::ctsIoPatternState() noexcept
    {
        if (ctsConfig::ProtocolType::UDP == ctsConfig::g_configSettings->Protocol)
        {
            m_internalState = InternalPatternState::MoreIo;
        }
    }

    inline uint64_t ctsIoPatternState::GetRemainingTransfer() const noexcept
    {
        //
        // Guard our internal tracking - all protocol logic assumes these rules
        //
        const auto alreadyTransferred = m_confirmedBytes + m_inflightBytes;
        FAIL_FAST_IF_MSG(
            alreadyTransferred < m_confirmedBytes || alreadyTransferred < m_inflightBytes,
            "ctsIOPatternState internal overflow (already_transferred = m_confirmedBytes + m_inflightBytes)\n"
            "already_transferred: %llu\n"
            "m_confirmedBytes: %llu\n"
            "m_inflightBytes: %llu\n",
            static_cast<uint64_t>(alreadyTransferred),
            static_cast<uint64_t>(m_confirmedBytes),
            static_cast<uint64_t>(m_inflightBytes));

        FAIL_FAST_IF_MSG(
            alreadyTransferred > m_maxTransfer,
            "ctsIOPatternState internal error: bytes already transferred (%llu) is >= the total we're expected to transfer (%llu)\n",
            static_cast<uint64_t>(alreadyTransferred), static_cast<uint64_t>(m_maxTransfer));

        return m_maxTransfer - alreadyTransferred;
    }
    inline uint64_t ctsIoPatternState::GetMaxTransfer() const noexcept
    {
        return m_maxTransfer;
    }
    inline void ctsIoPatternState::SetMaxTransfer(uint64_t maxTransfer) noexcept
    {
        m_maxTransfer = maxTransfer;
    }
    inline uint32_t ctsIoPatternState::GetIdealSendBacklog() const noexcept
    {
        return m_idealSendbacklog;
    }
    inline void ctsIoPatternState::SetIdealSendBacklog(uint32_t newIsb) noexcept
    {
        m_idealSendbacklog = newIsb;
    }

    inline bool ctsIoPatternState::IsCompleted() const noexcept
    {
        return InternalPatternState::CompletedTransfer == m_internalState || InternalPatternState::ErrorIoFailed == m_internalState;
    }

    inline bool ctsIoPatternState::IsCurrentStateMoreIo() const noexcept
    {
        return m_internalState == InternalPatternState::MoreIo;
    }

    inline ctsIoPatternType ctsIoPatternState::GetNextPatternType() noexcept
    {
        if (m_pendedState)
        {
            // already indicated the next state: waiting for it to complete
            return ctsIoPatternType::NoIo;
        }

        switch (m_internalState)
        {
            case InternalPatternState::Initialized:
                if (ctsConfig::IsListening())
                {
                    PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : SendConnectionId\n");
                    m_pendedState = true;
                    m_internalState = InternalPatternState::ServerSendConnectionId;
                    return ctsIoPatternType::SendConnectionId;
                }
                else
                {
                    PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : RecvConnectionId\n");
                    m_pendedState = true;
                    m_internalState = InternalPatternState::ClientRecvConnectionId;
                    return ctsIoPatternType::RecvConnectionId;
                }

            // both client and server start IO after the connection ID is shared
            case InternalPatternState::ServerSendConnectionId:
            case InternalPatternState::ClientRecvConnectionId:
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : MoreIo\n");
                m_internalState = InternalPatternState::MoreIo;
                return ctsIoPatternType::MoreIo;

            case InternalPatternState::MoreIo:
                // ReSharper disable once CppRedundantParentheses
                return (m_confirmedBytes + m_inflightBytes) < m_maxTransfer ?
                           ctsIoPatternType::MoreIo:
                           ctsIoPatternType::NoIo;

            case InternalPatternState::ServerSendCompletion:
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : SendCompletion\n");
                m_pendedState = true;
                return ctsIoPatternType::SendCompletion;

            case InternalPatternState::ClientRecvCompletion:
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : RecvCompletion\n");
                m_pendedState = true;
                return ctsIoPatternType::RecvCompletion;

            case InternalPatternState::GracefulShutdown:
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : GracefulShutdown\n");
                m_pendedState = true;
                return ctsIoPatternType::GracefulShutdown;

            case InternalPatternState::HardShutdown:
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : HardShutdown\n");
                m_pendedState = true;
                return ctsIoPatternType::HardShutdown;

            case InternalPatternState::RequestFin:
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::GetNextPatternType : RequestFin\n");
                m_pendedState = true;
                return ctsIoPatternType::RequestFin;

            case InternalPatternState::CompletedTransfer: // fall-through
            case InternalPatternState::ErrorIoFailed:
                return ctsIoPatternType::NoIo;

            default:
                FAIL_FAST_MSG(
                    "ctsIOPatternState::GetNextPatternType was called in an invalid state (%u): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState",
                    static_cast<uint32_t>(m_internalState), this);
        }
    }


    inline void ctsIoPatternState::NotifyNextTask(const ctsTask& nextTask) noexcept
    {
        if (nextTask.m_trackIo)
        {
            m_inflightBytes += nextTask.m_bufferLength;
        }
    }

    inline ctsIoPatternError ctsIoPatternState::UpdateError(DWORD error) noexcept
    {
        // if we have already failed, return early
        if (InternalPatternState::ErrorIoFailed == m_internalState)
        {
            return ctsIoPatternError::ErrorIoFailed;
        }

        if (ctsConfig::ProtocolType::UDP == ctsConfig::g_configSettings->Protocol)
        {
            if (error != 0)
            {
                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::UpdateError : ErrorIOFailed (%u)\n", error);
                m_internalState = InternalPatternState::ErrorIoFailed;
                return ctsIoPatternError::ErrorIoFailed;
            }
        }
        else
        {
            // ctsConfig::ProtocolType::TCP
            if (error != 0 && !IsCompleted())
            {
                if (ctsConfig::IsListening() &&
                    InternalPatternState::RequestFin == m_internalState &&
                    (WSAETIMEDOUT == error || WSAECONNRESET == error || WSAECONNABORTED == error))
                {
                    // these errors on the server are OK when we are waiting for a FIN from the client
                    // the client may have just RST instead of a graceful FIN after receiving our status
                    return ctsIoPatternError::NoError;
                }

                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::UpdateError : ErrorIOFailed (%u)\n", error);
                m_internalState = InternalPatternState::ErrorIoFailed;
                return ctsIoPatternError::ErrorIoFailed;
            }
        }

        return ctsIoPatternError::NoError;
    }

    inline ctsIoPatternError ctsIoPatternState::CompletedTask(const ctsTask& completedTask, uint32_t completedTransferBytes) noexcept
    {
        // If already failed, don't continue processing
        if (InternalPatternState::ErrorIoFailed == m_internalState)
        {
            return ctsIoPatternError::ErrorIoFailed;
        }

        // if completed our connection id request, immediately return
        // (not validating IO below)
        if (InternalPatternState::ServerSendConnectionId == m_internalState || InternalPatternState::ClientRecvConnectionId == m_internalState)
        {
            // must have received the full id
            if (completedTransferBytes != ctsStatistics::c_connectionIdLength)
            {
                PRINT_DEBUG_INFO(
                    L"\t\tctsIOPatternState::CompletedTask : ErrorIOFailed (TooFewBytes) [transfered %llu, Expected ConnectionID (%u)]\n",
                    static_cast<uint64_t>(completedTransferBytes),
                    ctsStatistics::c_connectionIdLength);

                m_internalState = InternalPatternState::ErrorIoFailed;
                return ctsIoPatternError::TooFewBytes;
            }

            m_pendedState = false;
        }

        if (completedTask.m_trackIo)
        {
            // Checking for an inconsistent internal state 
            FAIL_FAST_IF_MSG(
                completedTransferBytes > m_inflightBytes,
                "ctsIOPatternState::CompletedTask : ctsIOTask (%p) returned more bytes (%u) than were in flight (%llu)",
                &completedTask, completedTransferBytes, m_inflightBytes);
            FAIL_FAST_IF_MSG(
                completedTask.m_bufferLength > m_inflightBytes,
                "ctsIOPatternState::CompletedTask : the ctsIOTask (%p) had requested more bytes (%u) than were in-flight (%llu)\n",
                &completedTask, completedTask.m_bufferLength, m_inflightBytes);
            FAIL_FAST_IF_MSG(
                completedTransferBytes > completedTask.m_bufferLength,
                "ctsIOPatternState::CompletedTask : ctsIOTask (%p) returned more bytes (%u) than were posted (%u)\n",
                &completedTask, completedTransferBytes, completedTask.m_bufferLength);

            // now update our internal tracking of bytes in-flight / completed
            m_inflightBytes -= completedTask.m_bufferLength;
            m_confirmedBytes += completedTransferBytes;
        }

        // Verify IO Post-condition protocol contracts haven't been violated
        const auto alreadyTransferred = m_confirmedBytes + m_inflightBytes;

        // Udp just tracks bytes
        if (ctsConfig::ProtocolType::UDP == ctsConfig::g_configSettings->Protocol)
        {
            if (alreadyTransferred == m_maxTransfer)
            {
                return ctsIoPatternError::SuccessfullyCompleted;
            }
            return ctsIoPatternError::NoError;
        }

        // Tcp has a full state machine
        if (alreadyTransferred < m_maxTransfer)
        {
            // guard against the client gracefully exiting before the completion of the transfer
            if (0 == completedTransferBytes)
            {
                PRINT_DEBUG_INFO(
                    L"\t\tctsIOPatternState::CompletedTask : ErrorIOFailed (TooFewBytes) [transferred %llu, expected transfer %llu]\n",
                    static_cast<uint64_t>(alreadyTransferred),
                    m_maxTransfer);
                m_internalState = InternalPatternState::ErrorIoFailed;
                return ctsIoPatternError::TooFewBytes;
            }
        }
        else if (alreadyTransferred == m_maxTransfer)
        {
            // With TCP, if inflight_bytes > 0, we are not yet done
            // - we need to wait for that pended IO to complete
            if (0 == m_inflightBytes)
            {
                //
                // All TCP data has been sent/received
                //
                if (ctsConfig::IsListening())
                {
                    // servers will first send their final status before starting their shutdown sequence
                    switch (m_internalState)
                    {
                        case InternalPatternState::MoreIo:
                            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (MoreIo) : ServerSendCompletion\n");
                            m_internalState = InternalPatternState::ServerSendCompletion;
                            m_pendedState = false;
                            break;

                        case InternalPatternState::ServerSendCompletion:
                            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (ServerSendCompletion) : RequestFIN\n");
                            m_internalState = InternalPatternState::RequestFin;
                            m_pendedState = false;
                            break;

                        case InternalPatternState::RequestFin:
                            if (completedTransferBytes != 0)
                            {
                                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (RequestFIN) : ErrorIOFailed (TooManyBytes)\n");
                                m_internalState = InternalPatternState::ErrorIoFailed;
                                return ctsIoPatternError::TooManyBytes;
                            }
                            else
                            {
                                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (RequestFIN) : CompletedTransfer\n");
                                m_internalState = InternalPatternState::CompletedTransfer;
                                return ctsIoPatternError::SuccessfullyCompleted;
                            }

                        default:
                            FAIL_FAST_MSG(
                                "ctsIOPatternState::CompletedTask - invalid internal_status (%u): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState",
                                static_cast<uint32_t>(m_internalState), this);
                    }
                }
                else
                {
                    // clients will recv the server status, then process their shutdown sequence
                    switch (m_internalState)
                    {
                        case InternalPatternState::MoreIo:
                            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (MoreIo) : ClientRecvCompletion\n");
                            m_internalState = InternalPatternState::ClientRecvCompletion;
                            m_pendedState = false;
                            break;

                        case InternalPatternState::ClientRecvCompletion:
                            // process the server's returned status
                            if (completedTransferBytes != c_completionMessageSize)
                            {
                                PRINT_DEBUG_INFO(
                                    L"\t\tctsIOPatternState::CompletedTask (ClientRecvCompletion) : ErrorIOFailed (Server didn't return a completion - returned %u bytes)\n",
                                    completedTransferBytes);
                                m_internalState = InternalPatternState::ErrorIoFailed;
                                return ctsIoPatternError::TooFewBytes;
                            }
                            if (memcmp(completedTask.m_buffer, c_completionMessage, c_completionMessageSize) != 0)
                            {
                                PRINT_DEBUG_INFO(
                                    L"\t\tctsIOPatternState::CompletedTask (ClientRecvCompletion) : ErrorIOFailed (Server didn't return a correct completion message - expected to return DONE but it returned the 4 chars at %p)\n",
                                    completedTask.m_buffer);
                                m_internalState = InternalPatternState::ErrorIoFailed;
                                return ctsIoPatternError::TooFewBytes;
                            }

                            if (ctsConfig::TcpShutdownType::GracefulShutdown == ctsConfig::g_configSettings->TcpShutdown)
                            {
                                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (ClientRecvCompletion) : GracefulShutdown\n");
                                m_internalState = InternalPatternState::GracefulShutdown;
                                m_pendedState = false;
                            }
                            else
                            {
                                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (ClientRecvCompletion) : HardShutdown\n");
                                m_internalState = InternalPatternState::HardShutdown;
                                m_pendedState = false;
                            }
                            break;

                        case InternalPatternState::GracefulShutdown:
                            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (GracefulShutdown) : RequestFIN\n");
                            m_internalState = InternalPatternState::RequestFin;
                            m_pendedState = false;
                            break;

                        case InternalPatternState::RequestFin:
                            if (completedTransferBytes != 0)
                            {
                                PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (RequestFIN) : ErrorIOFailed (TooManyBytes)\n");
                                m_internalState = InternalPatternState::ErrorIoFailed;
                                return ctsIoPatternError::TooManyBytes;
                            }

                            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (RequestFIN) : CompletedTransfer\n");
                            m_internalState = InternalPatternState::CompletedTransfer;
                            return ctsIoPatternError::SuccessfullyCompleted;

                        case InternalPatternState::HardShutdown:
                            PRINT_DEBUG_INFO(L"\t\tctsIOPatternState::CompletedTask (HardShutdown) : CompletedTransfer\n");
                            m_internalState = InternalPatternState::CompletedTransfer;
                            return ctsIoPatternError::SuccessfullyCompleted;

                        default:
                            FAIL_FAST_MSG(
                                "ctsIOPatternState::CompletedTask - invalid internal_status (%u): dt %p ctsTraffic!ctsTraffic::ctsIOPatternState, dt %p ctsTraffic!ctstraffic::ctsIOTask",
                                static_cast<uint32_t>(m_internalState), this, &completedTask);
                    }
                }
            }
        }
        else if (alreadyTransferred > m_maxTransfer)
        {
            PRINT_DEBUG_INFO(
                L"\t\tctsIOPatternState::CompletedTask : ErrorIOFailed (TooManyBytes) [transferred %llu, expected transfer %llu]\n",
                static_cast<uint64_t>(alreadyTransferred),
                m_maxTransfer);
            m_internalState = InternalPatternState::ErrorIoFailed;
            return ctsIoPatternError::TooManyBytes;
        }

        return ctsIoPatternError::NoError;
    }
}
