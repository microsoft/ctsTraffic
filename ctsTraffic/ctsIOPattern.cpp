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
#include <ctTimer.hpp>
// project headers
#include "ctsMediaStreamProtocol.hpp"
#include "ctsTCPFunctions.h"
// wil headers always included last
#include <wil/stl.h>
#include <wil/resource.h>

namespace ctsTraffic
{
    using namespace ctl;
    using namespace std;

    constexpr uint32_t c_bufferPatternSize = 0xffff + 0x1; // fill from 0x0000 to 0xffff
    static unsigned char g_bufferPattern[c_bufferPatternSize * 2]; // * 2 as unsigned short values are twice as large as unsigned char

    // SharedBuffer is a larger buffer with many copies of BufferPattern in it. This is what the various IO patterns
    // will be memcmp'ing against for validity checks.
    //
    // The buffers' sizes will be the constant "BufferPatternSize + ctsConfig::GetMaxBufferSize()", but we
    // need to wait for input parsing before we can set that.

    static INIT_ONCE g_ctsIoPatternInitializer = INIT_ONCE_STATIC_INIT;
    static char* g_receiverSharedBuffer = nullptr;
    static char* g_senderSharedBuffer = nullptr;
    static uint32_t g_maximumBufferSize = 0;

    constexpr auto c_maxSupportedBytesInFlight = 0x1000000ul;
    static uint32_t g_maxNumberOfRioSendBuffers = 0;

    static BOOL CALLBACK InitOnceIoPatternCallback(PINIT_ONCE, PVOID, PVOID*) noexcept // NOLINT(bugprone-exception-escape)
    {
        // first create the buffer pattern
        for (size_t fillSlot = 0; fillSlot < c_bufferPatternSize; ++fillSlot)
        {
            *reinterpret_cast<unsigned short*>(&g_bufferPattern[fillSlot * 2]) = static_cast<unsigned short>(fillSlot);
        }

        g_maximumBufferSize = c_bufferPatternSize + ctsConfig::GetMaxBufferSize();
        g_maxNumberOfRioSendBuffers = c_maxSupportedBytesInFlight / ctsConfig::GetMinBufferSize() + 1;

        g_receiverSharedBuffer = static_cast<char*>(VirtualAlloc(nullptr, g_maximumBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        FAIL_FAST_IF_MSG(!g_receiverSharedBuffer, "VirtualAlloc alloc failed: %lu", GetLastError());

        g_senderSharedBuffer = static_cast<char*>(VirtualAlloc(nullptr, g_maximumBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        FAIL_FAST_IF_MSG(!g_senderSharedBuffer, "VirtualAlloc alloc failed: %lu", GetLastError());

        // fill in this allocated buffer while we can write to it
        auto* protectedDestination = g_senderSharedBuffer;
        auto writeSizeRemaining = g_maximumBufferSize;
        while (writeSizeRemaining > 0)
        {
            const auto bytesToWrite = writeSizeRemaining > c_bufferPatternSize ? c_bufferPatternSize : writeSizeRemaining;
            const auto memError = memcpy_s(protectedDestination, writeSizeRemaining, g_bufferPattern, bytesToWrite);
            FAIL_FAST_IF(memError != 0);

            protectedDestination += bytesToWrite;
            writeSizeRemaining -= bytesToWrite;
        }

        // guarantee no one will write to our g_ProtectedSharedBuffer - but not if using RIO (can't register read-only buffers)
        if (WI_IsFlagClear(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
        {
            DWORD oldSetting;
            FAIL_FAST_IF_MSG(!VirtualProtect(g_senderSharedBuffer, g_maximumBufferSize, PAGE_READONLY, &oldSetting), "VirtualProtect failed: %lu", GetLastError());
        }

        return TRUE;
    }

    //
    // Factory function to build known patterns
    // - can throw wil::ResultException on a Win32 error
    // - can throw exception on allocation failure
    //
    shared_ptr<ctsIoPattern> ctsIoPattern::MakeIoPattern()
    {
        switch (ctsConfig::g_configSettings->IoPattern)
        {
        case ctsConfig::IoPatternType::Pull:
            return make_shared<ctsIoPatternPull>();

        case ctsConfig::IoPatternType::Push:
            return make_shared<ctsIoPatternPush>();

        case ctsConfig::IoPatternType::PushPull:
            return make_shared<ctsIoPatternPushPull>();

        case ctsConfig::IoPatternType::Duplex:
            return make_shared<ctsIoPatternDuplex>();

        case ctsConfig::IoPatternType::MediaStream:
            if (ctsConfig::IsListening())
            {
                return make_shared<ctsIoPatternMediaStreamServer>();
            }
            return make_shared<ctsIoPatternMediaStreamClient>();

        case ctsConfig::IoPatternType::NoIoSet: // fall through
        default: // NOLINT(clang-diagnostic-covered-switch-default)
            FAIL_FAST_MSG("ctsIOPattern::MakeIOPattern - Unknown IoPattern specified (%d)", ctsConfig::g_configSettings->IoPattern);
        }
    }

    char* ctsIoPattern::AccessSharedBuffer() noexcept
    {
        // this init-once call is no-fail
        InitOnceExecuteOnce(&g_ctsIoPatternInitializer, InitOnceIoPatternCallback, nullptr, nullptr);
        return g_senderSharedBuffer;
    }

    void ctsIoPattern::CreateRecvBuffers()
    {
        const auto recvCount = m_recvBufferFreeList.size();
        if (recvCount > 0)
        {
            // recv will only use the same shared buffer when the user specified to do so on the cmdline
            if (ctsConfig::g_configSettings->UseSharedBuffer)
            {
                for (auto bufferCount = 0ul; bufferCount < recvCount; ++bufferCount)
                {
                    m_recvBufferFreeList[bufferCount] = g_receiverSharedBuffer;
                    if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
                    {
                        m_receivingRioBufferIds[bufferCount].m_bufferId = ctRIORegisterBuffer(g_receiverSharedBuffer, g_maximumBufferSize);
                        if (m_receivingRioBufferIds[bufferCount].m_bufferId == RIO_INVALID_BUFFERID)
                        {
                            THROW_WIN32_MSG(WSAGetLastError(), "RIORegisterBuffer");
                        }
                    }
                }
            }
            else
            {
                // every recv will need their own buffer to use
                // we must keep track of the raw buffers even with RIO as we need the backing buffers to compare against
                m_recvBufferContainer.resize(ctsConfig::GetMaxBufferSize() * recvCount);
                auto* const rawRecvBuffer = m_recvBufferContainer.data();

                for (auto bufferCount = 0ul; bufferCount < recvCount; ++bufferCount)
                {
                    auto* const nextBuffer = rawRecvBuffer + static_cast<size_t>(bufferCount * ctsConfig::GetMaxBufferSize());
                    m_recvBufferFreeList[bufferCount] = nextBuffer;

                    if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
                    {
                        m_receivingRioBufferIds[bufferCount].m_bufferId = ctRIORegisterBuffer(nextBuffer, ctsConfig::GetMaxBufferSize());
                        if (m_receivingRioBufferIds[bufferCount].m_bufferId == RIO_INVALID_BUFFERID)
                        {
                            THROW_WIN32_MSG(WSAGetLastError(), "RIORegisterBuffer");
                        }
                    }
                }
            }
        }

        // register buffers for the connection ID and the completion message
        if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
        {
            m_rioConnectionId.m_bufferId = ctRIORegisterBuffer(GetConnectionIdentifier(), ctsStatistics::ConnectionIdLength);
            if (m_rioConnectionId.m_bufferId == RIO_INVALID_BUFFERID)
            {
                THROW_WIN32_MSG(WSAGetLastError(), "RIORegisterBuffer");
            }

            m_rioCompletionMessage.m_bufferId = ctRIORegisterBuffer(m_completionMessageBuffer.data(), static_cast<DWORD>(m_completionMessageBuffer.size()));
            if (m_rioCompletionMessage.m_bufferId == RIO_INVALID_BUFFERID)
            {
                THROW_WIN32_MSG(WSAGetLastError(), "RIORegisterBuffer");
            }
        }
    }

    void ctsIoPattern::CreateSendBuffers()
    {
        memcpy_s(m_completionMessageBuffer.data(), m_completionMessageBuffer.size(), c_completionMessage, c_completionMessageSize);

        // if not using RIO, will just use the same global read-only buffer
        // if using RIO, we must have a unique RIO_BUFFERID for each concurrent RIOSend
        if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
        {
            m_sendingRioBufferIds.resize(g_maxNumberOfRioSendBuffers);
            for (auto& sendingBuffer : m_sendingRioBufferIds)
            {
                sendingBuffer.m_bufferId = ctRIORegisterBuffer(g_senderSharedBuffer, g_maximumBufferSize);
                if (sendingBuffer.m_bufferId == RIO_INVALID_BUFFERID)
                {
                    THROW_WIN32_MSG(WSAGetLastError(), "RIORegisterBuffer");
                }
            }

            // CreateRecvBuffers should have already created these 2 RIO Buffers
            FAIL_FAST_IF(m_rioConnectionId.m_bufferId == RIO_INVALID_BUFFERID); // NOLINT(performance-no-int-to-ptr, cppcoreguidelines-pro-type-cstyle-cast)
            FAIL_FAST_IF(m_rioCompletionMessage.m_bufferId == RIO_INVALID_BUFFERID); // NOLINT(performance-no-int-to-ptr, cppcoreguidelines-pro-type-cstyle-cast)
        }
    }

    ctsIoPattern::ctsIoPattern(uint32_t recvCount) :
        // (bytes/sec) * (1 sec/1000 ms) * (x ms/Quantum) == (bytes/quantum)
        m_burstCount{ ctsConfig::g_configSettings->BurstCount },
        m_burstDelay{ ctsConfig::g_configSettings->BurstDelay },
        m_bytesSendingPerQuantum{ ctsConfig::GetTcpBytesPerSecond() * ctsConfig::g_configSettings->TcpBytesPerSecondPeriod / 1000LL },
        m_quantumStartTimeMs{ ctTimer::snap_qpc_as_msec() }
    {
        FAIL_FAST_IF_MSG(
            ctsConfig::g_configSettings->UseSharedBuffer && ctsConfig::g_configSettings->ShouldVerifyBuffers,
            "Cannot use a shared buffer across connections and still verify buffers");

        // this init-once call is no-fail
        InitOnceExecuteOnce(&g_ctsIoPatternInitializer, InitOnceIoPatternCallback, nullptr, nullptr);

        // we can't fully initialize the send and recv buffers as we need to access ctsIoPatternStatistics
        // but that is instantiated after ctsIoPattern is instantiated
        // so will create the send and recv buffers during InitiateIo
        // just as when we verify we have started statistics
        if (recvCount > 0)
        {
            // we don't store recvCount : but we'll know it based on the size of m_recvBufferFreeList
            m_recvBufferFreeList.resize(recvCount);
            if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
            {
                m_receivingRioBufferIds.resize(recvCount);
            }
        }
    }

    //
    // requires that the caller has locked the socket
    // 
    ctsTask ctsIoPattern::InitiateIo() noexcept
    {
        // make sure stats starts tracking IO at the first IO request
        StartStatistics();

        ctsTask returnTask;
        switch (m_patternState.GetNextPatternType())
        {
        case ctsIoPatternType::MoreIo:
            returnTask = GetNextTaskFromPattern();
            break;

        case ctsIoPatternType::NoIo:
            break;

        case ctsIoPatternType::SendConnectionId:
        {
            returnTask.m_ioAction = ctsTaskAction::Send;
            returnTask.m_buffer = GetConnectionIdentifier();
            returnTask.m_rioBufferid = m_rioConnectionId.m_bufferId;
            returnTask.m_bufferLength = ctsStatistics::ConnectionIdLength;
            returnTask.m_bufferOffset = 0;
            returnTask.m_bufferType = ctsTask::BufferType::TcpConnectionId;
            returnTask.m_trackIo = false;
            break;
        }

        case ctsIoPatternType::RecvConnectionId:
            returnTask.m_ioAction = ctsTaskAction::Recv;
            returnTask.m_buffer = GetConnectionIdentifier();
            returnTask.m_rioBufferid = m_rioConnectionId.m_bufferId;
            returnTask.m_bufferLength = ctsStatistics::ConnectionIdLength;
            returnTask.m_bufferOffset = 0;
            returnTask.m_bufferType = ctsTask::BufferType::TcpConnectionId;
            returnTask.m_trackIo = false;
            break;

        case ctsIoPatternType::SendCompletion:
            // end-stats as early as possible after the actual IO finished
            EndStatistics();

            returnTask.m_ioAction = ctsTaskAction::Send;
            returnTask.m_buffer = m_completionMessageBuffer.data();
            returnTask.m_rioBufferid = m_rioCompletionMessage.m_bufferId;
            returnTask.m_bufferLength = c_completionMessageSize;
            returnTask.m_bufferOffset = 0;
            returnTask.m_bufferType = ctsTask::BufferType::CompletionMessage;
            returnTask.m_trackIo = false;
            break;

        case ctsIoPatternType::RecvCompletion:
            // end-stats as early as possible after the actual IO finished
            EndStatistics();

            returnTask.m_ioAction = ctsTaskAction::Recv;
            returnTask.m_buffer = m_completionMessageBuffer.data();
            returnTask.m_rioBufferid = m_rioCompletionMessage.m_bufferId;
            returnTask.m_bufferLength = c_completionMessageSize;
            returnTask.m_bufferOffset = 0;
            returnTask.m_bufferType = ctsTask::BufferType::CompletionMessage;
            returnTask.m_trackIo = false;
            break;

        case ctsIoPatternType::HardShutdown:
            // end-stats as early as possible after the actual IO finished
            EndStatistics();

            returnTask.m_ioAction = ctsTaskAction::HardShutdown;
            returnTask.m_trackIo = false;
            break;

        case ctsIoPatternType::GracefulShutdown:
            // end-stats as early as possible after the actual IO finished
            EndStatistics();

            returnTask.m_ioAction = ctsTaskAction::GracefulShutdown;
            returnTask.m_trackIo = false;
            break;

        case ctsIoPatternType::RequestFin:
            // post one final recv for the zero byte FIN
            // end-stats as early as possible after the actual IO finished
            EndStatistics();

            returnTask.m_ioAction = ctsTaskAction::Recv;
            returnTask.m_buffer = m_completionMessageBuffer.data();
            returnTask.m_rioBufferid = m_rioCompletionMessage.m_bufferId;
            returnTask.m_bufferLength = c_completionMessageSize;
            returnTask.m_bufferOffset = 0;
            returnTask.m_trackIo = false;
            returnTask.m_bufferType = ctsTask::BufferType::Static;
            break;

        default: // NOLINT(clang-diagnostic-covered-switch-default)
            FAIL_FAST_MSG("ctsIOPattern::initiate_io was called in an invalid state: dt %p ctsTraffic!ctsTraffic::ctsIOPattern", this);
        }

        m_patternState.NotifyNextTask(returnTask);
        return returnTask;
    }

    //
    // updates the internal counters to prepare for the next IO request
    // - the fact that complete_io was called assumes that the IO was successful
    // 
    // - original_task: the task provided to the caller from initiate_io (or a copy of)
    // - current_transfer: the number of bytes successfully transferred from the task
    // - status_code: the return code from the prior IO operation [assumes a Win32 error code]
    //
    // - returns the current status of the IO operation on this socket
    //
    // requires that the caller has locked the socket
    //
    ctsIoStatus ctsIoPattern::CompleteIo(const ctsTask& originalTask, uint32_t currentTransfer, uint32_t statusCode) noexcept // NOLINT(bugprone-exception-escape)
    {
        // preserve the initial state for the prior task
        const bool wasIoRequestedFromPattern = m_patternState.IsCurrentStateMoreIo();

        // add the recv buffer back if it was one of our dynamically allocated recv buffers
        // add back the RIO BufferId if it was a RIO request
        if (ctsTask::BufferType::Dynamic == originalTask.m_bufferType)
        {
            if (originalTask.m_ioAction == ctsTaskAction::Recv)
            {
                m_recvBufferFreeList.push_back(originalTask.m_buffer);
            }

            if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
            {
                if (originalTask.m_ioAction == ctsTaskAction::Send)
                {
                    m_sendingRioBufferIds.emplace_back(originalTask.m_rioBufferid);
                }
                else
                {
                    m_receivingRioBufferIds.emplace_back(originalTask.m_rioBufferid);
                }
            }
        }

        switch (originalTask.m_ioAction)
        {
        case ctsTaskAction::None:
            // ignore completions for tasks on None
            break;

        case ctsTaskAction::FatalAbort:
            PRINT_DEBUG_INFO(L"\t\tctsIOPattern : completing a FatalAbort (statusCode %u)\n", statusCode);
            UpdateLastError(c_statusErrorNotAllDataTransferred);
            break;

        case ctsTaskAction::Abort:
            PRINT_DEBUG_INFO(L"\t\tctsIOPattern : completing an Abort (statusCode %u)\n", statusCode);
            break;

        case ctsTaskAction::GracefulShutdown:
            // Fall-through to be processed like send or recv IO
            PRINT_DEBUG_INFO(L"\t\tctsIOPattern : completing a GracefulShutdown (statusCode %u)\n", statusCode);
            [[fallthrough]];
        case ctsTaskAction::HardShutdown:
            // GracefulShutdown falls through to this case - don't print HardShutdown for that case
            if (originalTask.m_ioAction == ctsTaskAction::HardShutdown)
            {
                // Fall-through to be processed like send or recv IO
                PRINT_DEBUG_INFO(L"\t\tctsIOPattern : completing a HardShutdown (statusCode %u)\n", statusCode);
            }
            [[fallthrough]];
        case ctsTaskAction::Recv:
            // Fall-through to Send - where the IO will be processed
            [[fallthrough]];
        case ctsTaskAction::Send:
        {
            auto verifyIo = true;
            if (ctsTask::BufferType::TcpConnectionId == originalTask.m_bufferType ||
                ctsTask::BufferType::CompletionMessage == originalTask.m_bufferType)
            {
                // not verifying the buffer since it's the connectionId - but must complete the task to update the protocol
                verifyIo = false;

                if (statusCode != NO_ERROR)
                {
                    UpdateLastError(statusCode);
                }
                else
                {
                    // process the TCP protocol state machine in pattern_state after receiving the connection id
                    UpdateLastPatternError(m_patternState.CompletedTask(originalTask, currentTransfer));
                }
            }
            else if (statusCode != NO_ERROR)
            {
                //
                // if the IO task failed, the entire IO pattern is now failed
                // - unless this is an extra recv that was canceled once we completed the transfer
                //
                if (ctsTaskAction::Recv == originalTask.m_ioAction && m_patternState.IsCompleted())
                {
                    PRINT_DEBUG_INFO(L"\t\tctsIOPattern : Recv failed after the pattern completed (error %u)\n", statusCode);
                }
                else
                {
                    const auto currentStatus = UpdateLastError(statusCode);
                    if (currentStatus != c_statusIoRunning)
                    {
                        PRINT_DEBUG_INFO(L"\t\tctsIOPattern : Recv failed before the pattern completed (error %u, current status %u)\n", statusCode, currentStatus);
                        verifyIo = false;
                    }
                }
            }

            if (verifyIo)
            {
                //
                // IO succeeded - update state machine with the completed task if this task had IO
                //
                const auto patternStatus = m_patternState.CompletedTask(originalTask, currentTransfer);
                // update the last_error if the pattern_state detected an error
                UpdateLastPatternError(patternStatus);
                //
                // if this is a TCP receive completion
                // and no IO or protocol errors
                // and the user requested to verify buffers
                // then actually validate the received completion
                //
                if (ctsConfig::g_configSettings->Protocol == ctsConfig::ProtocolType::TCP &&
                    ctsConfig::g_configSettings->ShouldVerifyBuffers &&
                    originalTask.m_ioAction == ctsTaskAction::Recv &&
                    originalTask.m_trackIo &&
                    (ctsIoPatternError::SuccessfullyCompleted == patternStatus || ctsIoPatternError::NoError == patternStatus))
                {
                    FAIL_FAST_IF_MSG(
                        originalTask.m_expectedPatternOffset != m_recvPatternOffset,
                        "ctsIOPattern::complete_io() : ctsIOTask (%p) expected_pattern_offset (%u) does not match the current pattern_offset (%u)",
                        &originalTask, originalTask.m_expectedPatternOffset, m_recvPatternOffset);

                    if (!VerifyBuffer(originalTask, currentTransfer))
                    {
                        UpdateLastError(c_statusErrorDataDidNotMatchBitPattern);
                    }

                    m_recvPatternOffset += currentTransfer;
                    m_recvPatternOffset %= c_bufferPatternSize;
                }
            }
            break;
        }
        }
        //
        // Notify the derived interface that the task completed
        // - if this wasn't our internal connection id request
        // - if there wasn't an error with the IO and an IO operation completed
        // If the derived interface returns an error,
        // - update the last_error status
        //
        if (originalTask.m_ioAction != ctsTaskAction::None &&
            NO_ERROR == statusCode)
        {
            if (ctsTaskAction::Send == originalTask.m_ioAction)
            {
                ctsConfig::g_configSettings->TcpStatusDetails.m_bytesSent.Add(currentTransfer);
            }
            else if (ctsTaskAction::Recv == originalTask.m_ioAction)
            {
                ctsConfig::g_configSettings->TcpStatusDetails.m_bytesRecv.Add(currentTransfer);
            }
            // only complete tasks that were requested
            if (wasIoRequestedFromPattern)
            {
                UpdateLastPatternError(CompleteTaskBackToPattern(originalTask, currentTransfer));
            }
        }
        //
        // If the state machine has verified the connection has completed, 
        // - set the last error to zero in case it was not already set to an error
        //   but do this *after* the other possible failure points were checked
        //
        if (m_patternState.IsCompleted())
        {
            UpdateLastError(NO_ERROR);
            EndStatistics();
        }

        return GetCurrentStatus();
    }

    ctsTask ctsIoPattern::CreateTrackedTask(ctsTaskAction action, uint32_t maxTransfer) noexcept
    {
        ctsTask returnTask(CreateNewTask(action, maxTransfer));
        returnTask.m_trackIo = true;
        return returnTask;
    }

    ctsTask ctsIoPattern::CreateUntrackedTask(ctsTaskAction action, uint32_t maxTransfer) noexcept
    {
        ctsTask returnTask(CreateNewTask(action, maxTransfer));
        returnTask.m_trackIo = false;
        return returnTask;
    }

    ctsTask ctsIoPattern::CreateNewTask(ctsTaskAction action, uint32_t maxTransfer) noexcept
    {
        //
        // with TCP, we need to calculate the buffer size based off bytes remaining
        // with UDP, we're always posting the same size buffer
        //

        // first: calculate the next buffer size assuming no max ceiling specified by the protocol
        const auto remainingTransfer = m_patternState.GetRemainingTransfer();
        const auto nextBufferSize = ctsConfig::GetBufferSize();
        const auto minBufferSize = min<uint64_t>(remainingTransfer, nextBufferSize);
        uint64_t newBufferSize = minBufferSize;

        // second: if the protocol specified a ceiling, recalculate given their ceiling
        if (maxTransfer > 0 && maxTransfer < minBufferSize)
        {
            newBufferSize = maxTransfer;
        }

        // guard against hitting a 32-bit overflow
        FAIL_FAST_IF_MSG(
            newBufferSize > MAXDWORD,
            "ctsIOPattern internal error: next buffer size (%llu) is greater than MAXDWORD (%u)",
            newBufferSize, MAXDWORD);
        const auto verifiedNewBufferSize{ static_cast<uint32_t>(newBufferSize) };

        // build the next IO request with a properly calculated buffer size
        // Send must specify the offset because we must align the patterns that we send
        // Recv must not specify an offset because will always use the entire buffer for the recv
        ctsTask returnTask;
        if (ctsTaskAction::Send == action)
        {
            // with RIO, we have pre-allocated only so many pre-pinned buffers for data to keep in flight
            // if that's exhausted, return no-IO yet
            if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO) && m_sendingRioBufferIds.empty())
            {
                return {};
            }

            //
            // check to see if the send needs to be deferred into the future
            //
            returnTask.m_timeOffsetMilliseconds = 0LL;
            if (m_bytesSendingPerQuantum > 0)
            {
                const auto currentTimeMs(ctTimer::snap_qpc_as_msec());
                if (m_bytesSendingThisQuantum < m_bytesSendingPerQuantum)
                {
                    // adjust bytes_sending_this_quantum
                    m_bytesSendingThisQuantum += verifiedNewBufferSize;

                    // no need to adjust quantum_start_time_ms unless we skipped into a new quantum
                    // (meaning the previous quantum had not filled the max bytes for that quantum)
                    // ReSharper disable once CppRedundantParentheses
                    if (currentTimeMs > (m_quantumStartTimeMs + ctsConfig::g_configSettings->TcpBytesPerSecondPeriod))
                    {
                        // current time shows it's now beyond this quantum timeframe
                        // - once we see how many quantums we have skipped forward, move our quantum start time to the quantum we are actually in
                        // - then adjust the number of bytes we are to send this quantum by how many quantum we just skipped
                        const auto quantumsSkippedSinceLastSend = (currentTimeMs - m_quantumStartTimeMs) / ctsConfig::g_configSettings->TcpBytesPerSecondPeriod;
                        m_quantumStartTimeMs += quantumsSkippedSinceLastSend * ctsConfig::g_configSettings->TcpBytesPerSecondPeriod;

                        // we have to be careful making this adjustment since the remaining bytes this quantum could be very small
                        // - we only subtract out if the number of bytes skipped is >= bytes actually skipped
                        const auto bytesToAdjust = m_bytesSendingPerQuantum * quantumsSkippedSinceLastSend;
                        if (bytesToAdjust > m_bytesSendingThisQuantum)
                        {
                            m_bytesSendingThisQuantum = 0;
                        }
                        else
                        {
                            m_bytesSendingThisQuantum -= bytesToAdjust;
                        }
                    }
                }
                else
                {
                    // we have sent more than required for this quantum
                    // - check if this full-filled future quantums as well
                    const auto quantumAheadToSchedule = m_bytesSendingThisQuantum / m_bytesSendingPerQuantum;

                    // ms_for_quantums_to_skip = the # of quantum beyond the current quantum that will be skipped
                    // - when we have already sent at least 1 additional quantum of bytes
                    const auto msForQuantumsToSkip = (quantumAheadToSchedule - 1) * ctsConfig::g_configSettings->TcpBytesPerSecondPeriod;

                    // carry forward extra bytes from quantums that will be filled by the bytes we have already sent
                    // (including the current quantum)
                    // then adding the bytes we're about to send
                    m_bytesSendingThisQuantum -= m_bytesSendingPerQuantum * quantumAheadToSchedule;
                    m_bytesSendingThisQuantum += verifiedNewBufferSize;

                    // update the return task for when to schedule the send
                    // first, calculate the time to get to the end of this time quantum
                    // - only adjust if the current time isn't already outside this quantum
                    if (currentTimeMs < m_quantumStartTimeMs + ctsConfig::g_configSettings->TcpBytesPerSecondPeriod)
                    {
                        returnTask.m_timeOffsetMilliseconds = m_quantumStartTimeMs + ctsConfig::g_configSettings->TcpBytesPerSecondPeriod - currentTimeMs;
                    }
                    // then add in any quantum we need to skip
                    returnTask.m_timeOffsetMilliseconds += msForQuantumsToSkip;
                    PRINT_DEBUG_INFO(L"\t\tctsIOPattern : delaying the next send due to RateLimit (%llu ms)\n", returnTask.m_timeOffsetMilliseconds);

                    // finally, adjust quantum_start_time_ms to the next quantum which IO will complete
                    m_quantumStartTimeMs += msForQuantumsToSkip + ctsConfig::g_configSettings->TcpBytesPerSecondPeriod;
                }
            }
            else if (m_burstCount.has_value())
            {
                if (m_burstCount.value() == 0)
                {
                    m_burstCount = ctsConfig::g_configSettings->BurstCount;
                }

                m_burstCount = m_burstCount.value() - 1;
                if (m_burstCount.value() == 0)
                {
                    returnTask.m_timeOffsetMilliseconds = m_burstDelay.value();
                    PRINT_DEBUG_INFO(L"\t\tctsIOPattern : delaying the next send due to BurstDelay (%llu ms)\n", returnTask.m_timeOffsetMilliseconds);
                }
                else
                {
                    PRINT_DEBUG_INFO(L"\t\tctsIOPattern : not delaying the next send due to BurstDelay\n");
                }
            }

            returnTask.m_ioAction = ctsTaskAction::Send;
            returnTask.m_bufferType = ctsTask::BufferType::Static;
            returnTask.m_bufferLength = verifiedNewBufferSize;
            returnTask.m_bufferOffset = m_sendPatternOffset;
            returnTask.m_expectedPatternOffset = 0;
            returnTask.m_buffer = g_senderSharedBuffer;

            // every RIOSend must have unique RIO buffer IDs - it can't reuse buffers ID's like WSASend can use the same m_buffer
            if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
            {
                FAIL_FAST_IF_MSG(
                    m_sendingRioBufferIds.empty(),
                    "m_sendingRioBufferIds is empty for a new Send task  (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)", this);
                returnTask.m_bufferType = ctsTask::BufferType::Dynamic;
                // Release successfully hands off the RIO_BUFFERID - safe to pop this object now
                returnTask.m_rioBufferid = m_sendingRioBufferIds.rbegin()->Release();
                m_sendingRioBufferIds.pop_back();
            }

            // now that we are indicating this buffer to send, increment the offset for the next send request
            m_sendPatternOffset += verifiedNewBufferSize;
            m_sendPatternOffset %= c_bufferPatternSize;

            FAIL_FAST_IF_MSG(
                m_sendPatternOffset >= c_bufferPatternSize,
                "pattern_offset being too large (larger than BufferPatternSize %u) means we might walk off the end of our shared buffer (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                c_bufferPatternSize, this);
            FAIL_FAST_IF_MSG(
                returnTask.m_bufferLength + returnTask.m_bufferOffset > g_maximumBufferSize,
                "return_task (%p) for a Send request is specifying a buffer that is larger than the static SharedBufferSize (%u) (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                &returnTask, g_maximumBufferSize, this);
        }
        else
        {
            returnTask.m_ioAction = ctsTaskAction::Recv;
            returnTask.m_bufferType = ctsTask::BufferType::Dynamic;
            returnTask.m_bufferLength = verifiedNewBufferSize;
            returnTask.m_bufferOffset = 0; // always recv to the beginning of the buffer
            returnTask.m_expectedPatternOffset = m_recvPatternOffset;

            FAIL_FAST_IF_MSG(
                m_recvBufferFreeList.empty(),
                "m_recvBufferFreeList is empty for a new Recv task  (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)", this);
            returnTask.m_buffer = *m_recvBufferFreeList.rbegin();
            m_recvBufferFreeList.pop_back();

            if (WI_IsFlagSet(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
            {
                FAIL_FAST_IF_MSG(
                    m_receivingRioBufferIds.empty(),
                    "m_receivingRioBufferIds is empty for a new Recv task  (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)", this);
                returnTask.m_rioBufferid = m_receivingRioBufferIds.rbegin()->m_bufferId;
                // successfully handed off the RIO_BUFFERID - don't deregister it when we pop it
                m_receivingRioBufferIds.rbegin()->m_bufferId = RIO_INVALID_BUFFERID;
                m_receivingRioBufferIds.pop_back();
            }

            FAIL_FAST_IF_MSG(
                m_recvPatternOffset >= c_bufferPatternSize,
                "pattern_offset being too large means we might walk off the end of our shared buffer (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)", this);
            FAIL_FAST_IF_MSG(
                returnTask.m_bufferLength + returnTask.m_bufferOffset > verifiedNewBufferSize,
                "return_task (%p) for a Recv request is specifying a buffer that is larger than buffer_size (%u) (dt ctsTraffic!ctsTraffic::ctsIOPattern %p)",
                &returnTask, verifiedNewBufferSize, this);
        }

        return returnTask;
    }

    bool ctsIoPattern::VerifyBuffer(const ctsTask& originalTask, uint32_t transferredBytes) noexcept
    {
        // only doing deep verification if the user asked us to
        if (!ctsConfig::g_configSettings->ShouldVerifyBuffers)
        {
            return true;
        }
        //
        // We're using RtlCompareMemory instead of memcmp because it returns the first offset at which the buffers differ,
        // which is more useful than memcmp's "sign of the difference between the first two differing elements"
        //
        const auto* const patternBuffer = g_senderSharedBuffer + originalTask.m_expectedPatternOffset;
        const size_t lengthMatched = RtlCompareMemory(
            patternBuffer,
            originalTask.m_buffer + originalTask.m_bufferOffset,
            transferredBytes);
        if (lengthMatched != transferredBytes)
        {
            ctsConfig::PrintErrorInfo(
                L"ctsIOPattern found data corruption: detected an invalid byte pattern in the returned buffer (length %u): "
                L"buffer received (%p), expected buffer pattern (%p) - mismatch from expected pattern at offset (%Iu) [expected 32-bit value '0x%x' didn't match '0x%x']",
                transferredBytes,
                originalTask.m_buffer + originalTask.m_bufferOffset,
                patternBuffer,
                lengthMatched,
                patternBuffer[lengthMatched],
                *(originalTask.m_buffer + originalTask.m_bufferOffset + lengthMatched));
        }

        return lengthMatched == transferredBytes;
    }

    [[nodiscard]] wil::cs_leave_scope_exit ctsIoPattern::AcquireIoPatternLock() const noexcept
    {
        const auto sharedSocket = m_parentSocket.lock();
        if (!sharedSocket)
        {
            // possible if we are in the destructor of the pattern while the socket is being closed
            // and one of the pattern's timer or callback threads is completing
            return {};
        }
        return sharedSocket->AcquireLock();
    }

    //
    // ctsIoPatternPull
    // - Pull Pattern
    //   - TCP-only
    //   - The server pushes data (sends)
    //   - The client pulls data (receives)
    //
    ctsIoPatternPull::ctsIoPatternPull() :
        ctsIoPatternStatistics(ctsConfig::IsListening() ? 0 : ctsConfig::g_configSettings->PrePostRecvs),
        m_ioAction(ctsConfig::IsListening() ? ctsTaskAction::Send : ctsTaskAction::Recv),
        m_recvNeeded(ctsConfig::IsListening() ? 0 : ctsConfig::g_configSettings->PrePostRecvs)
    {
    }

    //
    // virtual methods from the base class:
    // - assumes will be called under a CS from the base class
    // - tracking # of outstanding IO requests (configurable through the constructor)
    // - returns an empty task when no more IO is needed
    //
    ctsTask ctsIoPatternPull::GetNextTaskFromPattern() noexcept
    {
        if (m_ioAction == ctsTaskAction::Recv && m_recvNeeded > 0)
        {
            --m_recvNeeded;
            return CreateTrackedTask(m_ioAction);
        }

        if (m_ioAction == ctsTaskAction::Send && GetIdealSendBacklog() > m_sendBytesInFlight)
        {
            const auto returnTask(CreateTrackedTask(m_ioAction));
            m_sendBytesInFlight += returnTask.m_bufferLength;
            return returnTask;
        }

        return {};
    }

    ctsIoPatternError ctsIoPatternPull::CompleteTaskBackToPattern(const ctsTask& task, uint32_t completedBytes) noexcept
    {
        if (ctsTaskAction::Send == task.m_ioAction)
        {
            m_statistics.m_bytesSent.Add(completedBytes);
            m_sendBytesInFlight -= completedBytes;
        }
        else if (ctsTaskAction::Recv == task.m_ioAction)
        {
            m_statistics.m_bytesRecv.Add(completedBytes);
            ++m_recvNeeded;
        }

        return ctsIoPatternError::NoError;
    }

    //
    // ctsIoPatternPush
    // - Push Pattern
    //   - TCP-only
    //   - The client pushes data (send)
    //   - The server pulls data (recv)
    //
    ctsIoPatternPush::ctsIoPatternPush() :
        ctsIoPatternStatistics(ctsConfig::IsListening() ? ctsConfig::g_configSettings->PrePostRecvs : 0),
        m_ioAction(ctsConfig::IsListening() ? ctsTaskAction::Recv : ctsTaskAction::Send),
        m_recvNeeded(ctsConfig::IsListening() ? ctsConfig::g_configSettings->PrePostRecvs : 0)
    {
    }

    //
    // virtual methods from the base class:
    // - assumes will be called under a CS from the base class
    // - tracking # of outstanding IO requests (configurable through the constructor)
    // - returns an empty task when no more IO is needed
    //
    ctsTask ctsIoPatternPush::GetNextTaskFromPattern() noexcept
    {
        if (m_ioAction == ctsTaskAction::Recv && m_recvNeeded > 0)
        {
            --m_recvNeeded;
            return CreateTrackedTask(m_ioAction);
        }

        if (m_ioAction == ctsTaskAction::Send && GetIdealSendBacklog() > m_sendBytesInFlight)
        {
            const auto returnTask(CreateTrackedTask(m_ioAction));
            m_sendBytesInFlight += returnTask.m_bufferLength;
            return returnTask;
        }

        return {};
    }

    ctsIoPatternError ctsIoPatternPush::CompleteTaskBackToPattern(const ctsTask& task, uint32_t completedBytes) noexcept
    {
        if (ctsTaskAction::Send == task.m_ioAction)
        {
            m_statistics.m_bytesSent.Add(completedBytes);
            m_sendBytesInFlight -= completedBytes;
        }
        else if (ctsTaskAction::Recv == task.m_ioAction)
        {
            m_statistics.m_bytesRecv.Add(completedBytes);
            ++m_recvNeeded;
        }

        return ctsIoPatternError::NoError;
    }

    //
    // ctsIoPatternPushPull
    // - PushPull Pattern
    //   - TCP-only
    //   - The client pushes data in 'segments'
    //   - The server pulls data in 'segments'
    //   - At each segment, roles swap (pusher/puller)
    //
    //   - Currently not supporting concurrent IO via ctsConfig::GetConcurrentIoCount()
    //     as we need precise controls when to flip from send -> recv -> send
    //
    ctsIoPatternPushPull::ctsIoPatternPushPull() :
        ctsIoPatternStatistics(1), // currently not supporting >1 concurrent IO requests
        m_pushSegmentSize(ctsConfig::g_configSettings->PushBytes),
        m_pullSegmentSize(ctsConfig::g_configSettings->PullBytes),
        m_listening(ctsConfig::IsListening()),
        m_sending(!ctsConfig::IsListening()) // start with clients sending, servers receiving
    {
    }

    //
    // virtual methods from the base class:
    // - assumes will be called under a CS from the base class
    // - tracks if sending or receiving in the IO flow
    // - returns an empty task when no more IO is needed
    //
    ctsTask ctsIoPatternPushPull::GetNextTaskFromPattern() noexcept
    {
        uint32_t segmentSize{};
        if (m_listening)
        {
            // server role is opposite client
            segmentSize = m_sending ? m_pullSegmentSize : m_pushSegmentSize;
        }
        else
        {
            segmentSize = m_sending ? m_pushSegmentSize : m_pullSegmentSize;
        }

        FAIL_FAST_IF_MSG(
            m_intraSegmentTransfer >= segmentSize,
            "Invalid ctsIOPatternPushPull state: intra_segment_transfer (%u), segment_size (%u)",
            m_intraSegmentTransfer, segmentSize);

        if (m_ioNeeded)
        {
            m_ioNeeded = false;

            if (m_sending)
            {
                return CreateTrackedTask(
                    ctsTaskAction::Send,
                    segmentSize - m_intraSegmentTransfer);
            }
            return CreateTrackedTask(
                ctsTaskAction::Recv,
                segmentSize - m_intraSegmentTransfer);
        }
        return {};
    }

    ctsIoPatternError ctsIoPatternPushPull::CompleteTaskBackToPattern(const ctsTask& task, uint32_t currentTransfer) noexcept
    {
        if (ctsTaskAction::Send == task.m_ioAction)
        {
            m_statistics.m_bytesSent.Add(currentTransfer);
        }
        else if (ctsTaskAction::Recv == task.m_ioAction)
        {
            m_statistics.m_bytesRecv.Add(currentTransfer);
        }

        m_ioNeeded = true;
        m_intraSegmentTransfer += currentTransfer;

        uint32_t segmentSize{};
        if (m_listening)
        {
            // server role is opposite client
            segmentSize = m_sending ? m_pullSegmentSize : m_pushSegmentSize;
        }
        else
        {
            segmentSize = m_sending ? m_pushSegmentSize : m_pullSegmentSize;
        }

        FAIL_FAST_IF_MSG(
            m_intraSegmentTransfer > segmentSize,
            "Invalid ctsIOPatternPushPull state: intra_segment_transfer (%u), segment_size (%u)",
            m_intraSegmentTransfer, segmentSize);

        if (segmentSize == m_intraSegmentTransfer)
        {
            m_sending = !m_sending;
            m_intraSegmentTransfer = 0;
        }

        return ctsIoPatternError::NoError;
    }

    //
    // ctsIoPatternDuplex
    // - Concurrent Pattern
    //   - TCP-only
    //   - The client and server both send and receive data concurrently
    //
    ctsIoPatternDuplex::ctsIoPatternDuplex() noexcept :
        ctsIoPatternStatistics(ctsConfig::g_configSettings->PrePostRecvs),
        m_recvNeeded(ctsConfig::g_configSettings->PrePostRecvs)
    {
        // max transfer bytes must be an even # so send bytes and recv bytes are balanced
        auto currentMaxTransfer = GetTotalTransfer();
        if (currentMaxTransfer % 2 != 0)
        {
            SetTotalTransfer(++currentMaxTransfer);
        }

        m_remainingSendBytes = currentMaxTransfer / 2;
        m_remainingRecvBytes = m_remainingSendBytes;

        FAIL_FAST_IF_MSG(
            m_remainingSendBytes + m_remainingRecvBytes != GetTotalTransfer(),
            "ctsIOPatternDuplex: internal failure - send_bytes (%llu) + recv_bytes (%llu) must equal total bytes (%llu)",
            m_remainingSendBytes,
            m_remainingRecvBytes,
            GetTotalTransfer());
    }

    //
    // virtual methods from the base class:
    // - assumes will be called under a CS from the base class
    // - tracks if sending or receiving in the IO flow
    // - returns an empty task when no more IO is needed
    //
    ctsTask ctsIoPatternDuplex::GetNextTaskFromPattern() noexcept
    {
        ctsTask returnTask;

        // since we can have multiple receives in flight, must also check that we have remaining_recv_bytes
        if (m_remainingRecvBytes > 0 && m_recvNeeded > 0)
        {
            // for very large transfers, we need to ensure our SafeInt<int64_t> doesn't overflow when it's cast 
            // to uint32_t when passed to tracked_task()
            const uint32_t maxRemainingBytes = m_remainingRecvBytes > MAXLONG ?
                MAXLONG :
                static_cast<uint32_t>(m_remainingRecvBytes);
            returnTask = CreateTrackedTask(ctsTaskAction::Recv, maxRemainingBytes);
            // for tracking purposes, assume that this recv *might* end up receiving the entire buffer size
            // - only on completion will we adjust to the actual # of bytes received
            m_remainingRecvBytes -= returnTask.m_bufferLength;
            --m_recvNeeded;
        }
        else if (m_remainingSendBytes > 0 && GetIdealSendBacklog() > m_sendBytesInFlight)
        {
            // for very large transfers, we need to ensure our SafeInt<int64_t> doesn't overflow when it's cast 
            // to uint32_t when passed to tracked_task()
            const uint32_t maxRemainingBytes = m_remainingSendBytes > MAXLONG ?
                MAXLONG :
                static_cast<uint32_t>(m_remainingSendBytes);
            returnTask = CreateTrackedTask(ctsTaskAction::Send, maxRemainingBytes);
            m_remainingSendBytes -= returnTask.m_bufferLength;
            m_sendBytesInFlight += returnTask.m_bufferLength;
        }
        else
        {
            // no IO needed now: return the default task
        }

        return returnTask;
    }

    ctsIoPatternError ctsIoPatternDuplex::CompleteTaskBackToPattern(const ctsTask& task, uint32_t completedBytes) noexcept
    {
        switch (task.m_ioAction)
        {
        case ctsTaskAction::Send:
        {
            m_statistics.m_bytesSent.Add(completedBytes);
            m_sendBytesInFlight -= completedBytes;

            // first, we need to adjust the total back from our over-subscription guard when this task was created
            m_remainingSendBytes += task.m_bufferLength;
            // then we need to subtract back out the actual number of bytes sent
            m_remainingSendBytes -= completedBytes;
            break;
        }
        case ctsTaskAction::Recv:
        {
            m_statistics.m_bytesRecv.Add(completedBytes);
            ++m_recvNeeded;

            // first, we need to adjust the total back from our over-subscription guard when this task was created
            m_remainingRecvBytes += task.m_bufferLength;
            // then we need to subtract back out the actual number of bytes received
            m_remainingRecvBytes -= completedBytes;
            break;
        }

        default: ;
            // all others fall through to return NoError
        }

        return ctsIoPatternError::NoError;
    }

    //
    // ctsIoPatternMediaStreamServer
    // - ctsIOPatternMediaStream (Server) Pattern
    //   - UDP-only
    //   - The server sends data at a specified rate
    //   - The client receives data continuously
    //     After a 'buffer period' of data has been received,
    //     The client starts as timer to 'process' a time-slice of data
    //
    ctsIoPatternMediaStreamServer::ctsIoPatternMediaStreamServer() noexcept :
        ctsIoPatternStatistics(1), // the pattern will use the recv writeable-buffer for sending a connection ID
        m_frameSizeBytes(ctsConfig::GetMediaStream().FrameSizeBytes),
        m_frameRateFps(ctsConfig::GetMediaStream().FramesPerSecond)
    {
        PRINT_DEBUG_INFO(L"\t\tctsIOPatternMediaStreamServer - frame rate in milliseconds per frame : %lld\n", static_cast<int64_t>(1000UL / m_frameRateFps));
    }

    // required virtual functions
    ctsTask ctsIoPatternMediaStreamServer::GetNextTaskFromPattern() noexcept
    {
        ctsTask returnTask;
        switch (m_state)
        {
        case ServerState::NotStarted:
            // get a writable buffer (i.e. Recv), then update the fields in the task for the connection_id
            returnTask = ctsMediaStreamMessage::MakeConnectionIdTask(
                CreateUntrackedTask(ctsTaskAction::Recv, c_udpDatagramConnectionIdHeaderLength),
                GetConnectionIdentifier());
            m_state = ServerState::IdSent;
            break;

        case ServerState::IdSent:
            m_baseTimeMilliseconds = ctTimer::snap_qpc_as_msec();
            m_state = ServerState::IoStarted;
            [[fallthrough]];
        case ServerState::IoStarted:
            if (m_currentFrameRequested < m_frameSizeBytes)
            {
                returnTask = CreateTrackedTask(ctsTaskAction::Send, m_frameSizeBytes);
                // calculate the future time to initiate the IO
                // - then subtract the start time to give the difference
                // ReSharper disable CppRedundantParentheses
                returnTask.m_timeOffsetMilliseconds =
                    m_baseTimeMilliseconds
                    + (m_currentFrame * 1000LL / m_frameRateFps)
                    - ctTimer::snap_qpc_as_msec();
                // ReSharper restore CppRedundantParentheses

                m_currentFrameRequested += returnTask.m_bufferLength;
            }
            break;
        }
        return returnTask;
    }

    ctsIoPatternError ctsIoPatternMediaStreamServer::CompleteTaskBackToPattern(const ctsTask& task, uint32_t currentTransfer) noexcept
    {
        if (task.m_bufferType != ctsTask::BufferType::UdpConnectionId)
        {
            const int64_t currentTransferBits = static_cast<int64_t>(currentTransfer) * 8LL;

            ctsConfig::g_configSettings->UdpStatusDetails.m_bitsReceived.Add(currentTransferBits);
            m_statistics.m_bitsReceived.Add(currentTransferBits);

            m_currentFrameCompleted += currentTransfer;
            if (m_currentFrameCompleted == m_frameSizeBytes)
            {
                ++m_currentFrame;
                m_currentFrameRequested = 0UL;
                m_currentFrameCompleted = 0UL;
            }
        }
        return ctsIoPatternError::NoError;
    }
} //namespace
