/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <array>
#include <memory>
#include <algorithm>
// os headers
#include <Windows.h>
// project headers
#include "ctsConfig.h"
#include "ctsIOPatternState.hpp"
#include "ctsIOTask.hpp"
#include "ctsStatistics.hpp"
#include "ctSocketExtensions.hpp"

namespace ctsTraffic
{
// forward declaring the parent ctsSocket class
// cannot include its header in this header as there will be a circular reference
class ctsSocket;

///////////////////////////////////////////////////////////////////////////////////////////////////
///
/// Possible status values returned to the caller upon completing IO
/// - on failure, these codes can be reported as errors back to the caller
///
////////////////////////////////////////////////////////////////////////////////////////////////////
enum class ctsIoStatus
{
    ContinueIo,
    CompletedIo,
    FailedIo
};

constexpr int c_statusIoRunning = MAXINT;
constexpr int c_statusErrorNotAllDataTransferred = MAXINT - 1;
constexpr int c_statusErrorTooMuchDataTransferred = MAXINT - 2;
constexpr int c_statusErrorDataDidNotMatchBitPattern = MAXINT - 3;
constexpr int c_statusMinimumValue = MAXINT - 3;

class ctsIoPattern
{
public:
    constexpr static bool IsProtocolError(uint32_t status) noexcept
    {
        return status >= c_statusMinimumValue && status < c_statusIoRunning;
    }

    constexpr static const wchar_t* BuildProtocolErrorString(uint32_t status) noexcept
    {
        switch (status)
        {
            case c_statusErrorNotAllDataTransferred:
                return L"ErrorNotAllDataTransferred";

            case c_statusErrorTooMuchDataTransferred:
                return L"ErrorTooMuchDataTransferred";

            case c_statusErrorDataDidNotMatchBitPattern:
                return L"ErrorDataDidNotMatchBitPattern";

            default:
                FAIL_FAST_MSG(
                    "ctsIOPattern: internal inconsistency - expecting a protocol error ctsIOProtocolState (%u)", status);
        }
    }

    ///
    /// Helper factory to build known patterns
    ///
    static std::shared_ptr<ctsIoPattern> MakeIoPattern();
    ///
    /// Making available the shared buffer used for sends and recvs
    ///
    static char* AccessSharedBuffer() noexcept;
    ///
    /// d'tor must be virtual as this is a base pure virtual class
    ///
    virtual ~ctsIoPattern() noexcept = default;

    ///
    /// Exposing statistics members publicly to ctsSocket
    ///
    virtual void PrintStatistics(const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr) noexcept = 0;
    virtual void PrintTcpInfo(const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr, SOCKET socket) noexcept = 0;

    //
    // These are public functions exposed to ctsSocket and the derived types
    // it's required they have already acquired the socket or pattern lock
    //
    virtual void RegisterCallback(std::function<void(const ctsTask&)> callback) noexcept
    {
        m_callback = std::move(callback);
    }

    [[nodiscard]] virtual uint32_t GetLastPatternError() const noexcept
    {
        return m_lastError;
    }

    void SetParent(const std::shared_ptr<ctsSocket>& parentSocket) noexcept
    {
        m_parentSocket = parentSocket;
    }

    void SetIdealSendBacklog(uint32_t newIsb) noexcept
    {
        m_patternState.SetIdealSendBacklog(newIsb);
    }

    [[nodiscard]] size_t GetRioBufferIdCount() const noexcept
    {
        if (WI_IsFlagClear(ctsConfig::g_configSettings->SocketFlags, WSA_FLAG_REGISTERED_IO))
        {
            return 0;
        }

        // add 2 to count 1 for m_rioConnectionId and one for m_rioCompletionMessages
        return m_receivingRioBufferIds.size() + m_sendingRioBufferIds.size() + 2;
    }

    ///
    /// none of these *_io functions can throw
    /// failures are critical and will RaiseException to be debugged
    /// - the task given by initiate_io should be returned through complete_io
    ///   (or a copy of that task)
    ///
    /// Callers access initiate_io() to retrieve a ctsIOTask object for the next IO operation
    /// - they are expected to retain that ctsIOTask object until the IO operation completes
    /// - at which time they pass it back to complete_io()
    ///
    /// initiate_io() can be called repeatedly by the caller if they want overlapping IO calls
    /// - without forced to wait for complete_io() for the next IO request
    ///
    /// complete_io() should be called for every returned initiate_io with the following:
    ///   _task : the ctsIOTask that was provided to perform
    ///   _current_transfer : the number of bytes successfully transferred from the task
    ///   _status_code: the return code from the prior IO operation [assumes a Win32 error code]
    ///
    [[nodiscard]] ctsTask InitiateIo() noexcept;
    ctsIoStatus CompleteIo(const ctsTask& originalTask, uint32_t currentTransfer, uint32_t statusCode) noexcept; // NOLINT(bugprone-exception-escape)

    /// no default c'tor
    ctsIoPattern() = delete;
    /// no copy c'tor or copy assignment
    ctsIoPattern(const ctsIoPattern&) = delete;
    ctsIoPattern& operator=(const ctsIoPattern&) = delete;
    ctsIoPattern(ctsIoPattern&&) = delete;
    ctsIoPattern& operator=(ctsIoPattern&&) = delete;

private:
    [[nodiscard]] ctsIoStatus GetCurrentStatus() const noexcept
    {
        if (c_statusIoRunning == m_lastError)
        {
            return ctsIoStatus::ContinueIo;
        }

        if (NO_ERROR == m_lastError)
        {
            return ctsIoStatus::CompletedIo;
        }

        // all other values indicate failure
        return ctsIoStatus::FailedIo;
    }

    // Private method to return a pre-populated task
    // - *not* setting the private ctsIOTask::tracked_io property
    ctsTask CreateNewTask(ctsTaskAction action, uint32_t maxTransfer) noexcept;

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Private method which must be implemented by the derived interface (the IO pattern)
    ///
    /// ctsIOTask GetNextTask()
    /// - must return a ctsIOTask returned from CreateTrackedTask or CreateUntrackedTask
    ///
    /// ctsIoPatternError CompleteTask(const ctsIOTask&, uint32_t currentTransfer) noexcept
    /// - a notification to the derived class over what task completed
    ///   - ctsIOTask argument: the ctsIOTask which it previously returned from GetNextTask()
    ///   - uint32_t argument:  the # of bytes actually transferred
    /// - cannot throw [if it fails, it must RaiseException to debug]
    /// - returns a uint32_t back to the base class to indicate errors
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    virtual ctsTask GetNextTaskFromPattern() = 0;
    virtual ctsIoPatternError CompleteTaskBackToPattern(const ctsTask&, uint32_t currentTransfer) noexcept = 0;

    // holding a weak reference to the parent socket object
    // since these will share the same locking requirements
    std::weak_ptr<ctsSocket> m_parentSocket;

    // track the state of the L4 protocol (TCP or UDP)
    ctsIoPatternState m_patternState;

    // optional callback for protocols which need to communicate OOB to the IO function
    std::function<void(const ctsTask&)> m_callback;

    // need to track the current offset into the buffer pattern
    // these are separate as we could have both sends and receive operations on the same connection
    uint32_t m_sendPatternOffset = 0;
    uint32_t m_recvPatternOffset = 0;

    std::optional<uint32_t> m_burstCount;
    std::optional<uint32_t> m_burstDelay;

    // recv buffers to return to the caller
    // - tracking sending buffers separate from receiving buffers
    //   since sending buffers will have a test pattern written to it (thus send buffers can be static)
    // For supporting multiple recv calls, allocating a larger buffer to contain all recv requests
    // - as well as a vector to contain the multiple ptrs to each buffer
    // When needing to dynamically allocate, containing a vector to hold the bytes
    std::vector<char*> m_recvBufferFreeList;
    std::vector<char> m_recvBufferContainer;
    std::array<char, c_completionMessageSize> m_completionMessageBuffer{};

    struct RioBufferId
    {
        RIO_BUFFERID m_bufferId = RIO_INVALID_BUFFERID;

        RioBufferId() = default;

        explicit RioBufferId(RIO_BUFFERID bufferId) noexcept :
            m_bufferId(bufferId)
        {
        }

        ~RioBufferId() noexcept
        {
            if (m_bufferId != RIO_INVALID_BUFFERID)
            {
                ctl::ctRIODeregisterBuffer(m_bufferId);
                m_bufferId = RIO_INVALID_BUFFERID;
            }
        }

        // only movable to guarantee destruction
        RioBufferId(const RioBufferId&) = delete;
        RioBufferId& operator=(const RioBufferId&) = delete;

        RioBufferId(RioBufferId&& rhs) noexcept :
            m_bufferId(rhs.m_bufferId)
        {
            rhs.m_bufferId = RIO_INVALID_BUFFERID;
        }

        RioBufferId& operator=(RioBufferId&& rhs) noexcept
        {
            this->m_bufferId = rhs.m_bufferId;
            rhs.m_bufferId = RIO_INVALID_BUFFERID;
            return *this;
        }

        RIO_BUFFERID Release() noexcept
        {
            const RIO_BUFFERID returnId = m_bufferId; // NOLINT(misc-misplaced-const)
            m_bufferId = RIO_INVALID_BUFFERID;
            return returnId;
        }
    };

    // RIO buffer Id for sends and recv's
    // cannot use the same RIO_BUFFERID concurrently (not supported by RIO)
    std::vector<RioBufferId> m_receivingRioBufferIds;
    std::vector<RioBufferId> m_sendingRioBufferIds;
    RioBufferId m_rioConnectionId;
    RioBufferId m_rioCompletionMessage;

    // tracking time information for scheduling IO at time offsets
    // (bytes/sec) * (1 sec/1000 ms) * (x ms/Quantum) == (bytes/quantum)
    const int64_t m_bytesSendingPerQuantum;
    int64_t m_bytesSendingThisQuantum{0};
    int64_t m_quantumStartTimeMs{0};

    uint32_t m_lastError = c_statusIoRunning;

protected:
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// protected constructor
    ///
    /// - only applicable for the derived types to indicate if will need send or recv buffers
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    explicit ctsIoPattern(uint32_t recvCount);

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The derived template class for tracking statistics must implement these pure virtual functions
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    virtual void StartStatistics() noexcept = 0;
    virtual void EndStatistics() noexcept = 0;
    virtual char* GetConnectionIdentifier() noexcept = 0;
    /// <summary>
    ///  the templated derived class tracking statistics must call Create* when it's generated
    ///  a connection Id for this instance
    /// </summary>
    void CreateRecvBuffers();
    void CreateSendBuffers();

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// CreateTrackedTask(IOTaskAction, uint32_t _max_transfer)
    /// CreateUntrackedTask(IOTaskAction, uint32_t _max_transfer)
    ///
    /// - returns a ctsIOTask for the next transfer based on the IOAction
    /// - the returned buffer can be contained to maximum size with _max_transfer
    ///
    /// tracked tasks will count that IO towards the max_transfer
    /// untracked tasks will *not* count the IO towards the max_transfer
    /// untracked tasks will *not* have their buffers validated on complete_io
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ctsTask CreateTrackedTask(ctsTaskAction, uint32_t maxTransfer = 0) noexcept;
    ctsTask CreateUntrackedTask(ctsTaskAction, uint32_t maxTransfer = 0) noexcept;

    // Exposing to the derived class the # of bytes to be transferred as tracked in the base class
    // Make it possible for the derived type to also override the total transfer 
    //  - to meet its requirements (e.g. must be an even total # for balanced send & recv's)
    [[nodiscard]] uint64_t GetTotalTransfer() const noexcept
    {
        return m_patternState.GetMaxTransfer();
    }

    // Exposing to the derived classes the total transfer configured for this pattern instance
    void SetTotalTransfer(uint64_t newTotal) noexcept
    {
        m_patternState.SetMaxTransfer(newTotal);
    }

    // Exposing to the derived classes the total ideal send backlog value
    // currently configured for this pattern instance
    [[nodiscard]] uint32_t GetIdealSendBacklog() const noexcept
    {
        return m_patternState.GetIdealSendBacklog();
    }

    // Expose to the derived class the option to verify the buffers in their ctsIOTask which
    // - they created through untracked_task
    static bool VerifyBuffer(const ctsTask& originalTask, uint32_t transferredBytes) noexcept;

    // Expose to the derived class the option to have a ctsIOTask sent OOB to the IO caller
    // - requires the caller to already have the pattern lock
    void SendTaskToCallback(const ctsTask& task) const noexcept
    {
        if (m_callback)
        {
            m_callback(task);
        }
    }

    /// Enabling derived types to update the internally tracked last-error
    //
    // update_last_error will attempt to keep the first error reported
    // - this will only update the value if an error has not yet been report for this state
    uint32_t UpdateLastError(const DWORD error) noexcept
    {
        if (c_statusIoRunning == m_lastError)
        {
            const auto statusError = m_patternState.UpdateError(error);
            if (NO_ERROR == error)
            {
                if (statusError != ctsIoPatternError::ErrorIoFailed)
                {
                    m_lastError = NO_ERROR;
                }
            }
            else
            {
                if (statusError == ctsIoPatternError::ErrorIoFailed)
                {
                    m_lastError = error;
                }
            }
        }
        return m_lastError;
    }

    void UpdateLastPatternError(ctsIoPatternError patternError) noexcept
    {
        switch (patternError)
        {
            case ctsIoPatternError::CorruptedBytes:
                UpdateLastError(c_statusErrorDataDidNotMatchBitPattern);
                break;

            case ctsIoPatternError::TooFewBytes:
                UpdateLastError(c_statusErrorNotAllDataTransferred);
                break;

            case ctsIoPatternError::TooManyBytes:
                UpdateLastError(c_statusErrorTooMuchDataTransferred);
                break;

            case ctsIoPatternError::SuccessfullyCompleted:
                UpdateLastError(NO_ERROR);
                break;

            case ctsIoPatternError::NoError:
                // fall-through - don't modify the current error value
                [[fallthrough]];
            case ctsIoPatternError::ErrorIoFailed:
                break;
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Complex derived patterns that need to obtain a lock can encounter complexities if they have
    ///   their own locks, given that the interface between the base and derived classes allow for
    ///   each to call the other. Base-calling-Drived && Drived-calling-Base patterns have the
    ///   inherant risk of deadlocks.
    ///
    /// Exposing the base class lock for these complex derived types. Most derived types will never
    ///    need this since the lock is always held before a derived interface is invoked by the base
    ///    class.
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    [[nodiscard]] wil::cs_leave_scope_exit AcquireIoPatternLock() const noexcept;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
///
/// The ctsIOPattern tracks what IO should be conducted on the socket
/// * All public methods protect against concurrent calls by taking an object lock
/// * Templated based off of the type of statistics being tracked by the object
///   Currently supporting
///   - ctsTcpStatistics
///   - ctsUdpStatistics
///
///////////////////////////////////////////////////////////////////////////////////////////////////
template <typename S>
class ctsIoPatternStatistics : public ctsIoPattern
{
public:
    explicit ctsIoPatternStatistics(uint32_t recvCount) :
        ctsIoPattern{recvCount}
    {
        // servers need to generate a unique connection ID
        if (ctsConfig::IsListening())
        {
            ctsStatistics::GenerateConnectionId(m_statistics);
        }
        // once m_statistics was created (if it was created), it must indicate to ctsIoPattern
        // that it can now safely finish construction by creating the necessary buffers
        CreateRecvBuffers();
        CreateSendBuffers();
    }

    ~ctsIoPatternStatistics() noexcept override
    {
        // guarantee that end_pattern has been called at least once
        ctsIoPatternStatistics<S>::EndStatistics();
    }

    ctsIoPatternStatistics(const ctsIoPatternStatistics&) = delete;
    ctsIoPatternStatistics& operator=(const ctsIoPatternStatistics&) = delete;
    ctsIoPatternStatistics(ctsIoPatternStatistics&&) = delete;
    ctsIoPatternStatistics& operator=(ctsIoPatternStatistics&&) = delete;

    // Printing of results is controlled by the applicable statistics type
    void PrintStatistics(const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr) noexcept override
    {
        // before printing the final results, make sure the timers are stopped
        if (0 == GetLastPatternError() && 0 == m_statistics.GetBytesReceived())
        {
            PRINT_DEBUG_INFO(L"\t\tctsIOPattern::PrintStatistics : reporting a successful IO completion but transfered zero bytes\n");
            UpdateLastPatternError(ctsIoPatternError::TooFewBytes);
        }

        ctsConfig::PrintConnectionResults(
            localAddr,
            remoteAddr,
            GetLastPatternError(),
            m_statistics);
    }

    void PrintTcpInfo(const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr, SOCKET socket) noexcept override
    {
        ctsConfig::PrintTcpDetails(localAddr, remoteAddr, socket, m_statistics);
    }

    // the caller must guarantee calls to Start and End are serialized
    void StartStatistics() noexcept override
    {
        if (!m_started)
        {
            m_statistics.m_startTime.SetConditionally(ctl::ctTimer::SnapQpcInMillis(), 0LL);
            m_started = true;
        }
    }

    // if the m_endTime was previously zero, will also update historic stats
    void EndStatistics() noexcept override
    {
        m_statistics.m_endTime.SetConditionally(ctl::ctTimer::SnapQpcInMillis(), 0LL);
    }

    // Access the ConnectionId stored in the Stats object
    char* GetConnectionIdentifier() noexcept override
    {
        return m_statistics.m_connectionIdentifier;
    }

    // Statistics type is controlled by the caller as the class template type
    S m_statistics;
    bool m_started = false;
};


///////////////////////////////////////////////////////////////////////////////////////////////////
///
///     - Pull Pattern
///    -- TCP-only
///    -- The server pushes data in 'segments' (the size of which is defined in the class)
///    -- The client pulls data in 'segments'
///
///////////////////////////////////////////////////////////////////////////////////////////////////
class ctsIoPatternPull final : public ctsIoPatternStatistics<ctsTcpStatistics>
{
public:
    ctsIoPatternPull();
    ~ctsIoPatternPull() noexcept override = default;

    ctsIoPatternPull(const ctsIoPatternPull&) = delete;
    ctsIoPatternPull& operator=(const ctsIoPatternPull&) = delete;
    ctsIoPatternPull(ctsIoPatternPull&&) = delete;
    ctsIoPatternPull& operator=(ctsIoPatternPull&&) = delete;

    // required virtual functions
    ctsTask GetNextTaskFromPattern() noexcept override;
    ctsIoPatternError CompleteTaskBackToPattern(const ctsTask& task, uint32_t completedBytes) noexcept override;

private:
    const ctsTaskAction m_ioAction;
    uint32_t m_recvNeeded{0};
    uint32_t m_sendBytesInflight{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////
///
///  - Push Pattern
///    -- TCP-only
///    -- The client pushes data in 'segments' (the size of which is defined in the class)
///    -- The server pulls data in 'segments'
///
///////////////////////////////////////////////////////////////////////////////////////////////////
class ctsIoPatternPush final : public ctsIoPatternStatistics<ctsTcpStatistics>
{
public:
    ctsIoPatternPush();
    ~ctsIoPatternPush() noexcept override = default;

    ctsIoPatternPush(const ctsIoPatternPush&) = delete;
    ctsIoPatternPush& operator=(const ctsIoPatternPush&) = delete;
    ctsIoPatternPush(ctsIoPatternPush&&) = delete;
    ctsIoPatternPush& operator=(ctsIoPatternPush&&) = delete;

    // required virtual functions
    ctsTask GetNextTaskFromPattern() noexcept override;
    ctsIoPatternError CompleteTaskBackToPattern(const ctsTask& task, uint32_t completedBytes) noexcept override;

private:
    const ctsTaskAction m_ioAction;
    uint32_t m_recvNeeded{0};
    uint32_t m_sendBytesInflight{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////
///
///     - PushPull Pattern
///    -- TCP-only
///    -- The client pushes data in 'segments'
///    -- The server pulls data in 'segments'
///    -- At each segment, roles swap (pusher/puller)
///
///////////////////////////////////////////////////////////////////////////////////////////////////
class ctsIoPatternPushPull final : public ctsIoPatternStatistics<ctsTcpStatistics>
{
public:
    ctsIoPatternPushPull();
    ~ctsIoPatternPushPull() noexcept override = default;

    ctsIoPatternPushPull(const ctsIoPatternPushPull&) = delete;
    ctsIoPatternPushPull& operator=(const ctsIoPatternPushPull&) = delete;
    ctsIoPatternPushPull(ctsIoPatternPushPull&&) = delete;
    ctsIoPatternPushPull& operator=(ctsIoPatternPushPull&&) = delete;

    ctsTask GetNextTaskFromPattern() noexcept override;
    ctsIoPatternError CompleteTaskBackToPattern(const ctsTask& task, uint32_t currentTransfer) noexcept override;

private:
    const uint32_t m_pushSegmentSize;
    const uint32_t m_pullSegmentSize;

    uint32_t m_intraSegmentTransfer{0};

    const bool m_listening;
    bool m_ioNeeded{true};
    bool m_sending{false};
};

///////////////////////////////////////////////////////////////////////////////////////////////////
///
///  - Duplex Pattern
///    -- TCP-only
///    -- The client both pushes and pulls data concurrently
///    -- The server both pushes and pulls data concurrently
///
///////////////////////////////////////////////////////////////////////////////////////////////////
class ctsIoPatternDuplex final : public ctsIoPatternStatistics<ctsTcpStatistics>
{
public:
    ctsIoPatternDuplex() noexcept;
    ~ctsIoPatternDuplex() noexcept override = default;

    ctsIoPatternDuplex(const ctsIoPatternDuplex&) = delete;
    ctsIoPatternDuplex& operator=(const ctsIoPatternDuplex&) = delete;
    ctsIoPatternDuplex(ctsIoPatternDuplex&&) = delete;
    ctsIoPatternDuplex& operator=(ctsIoPatternDuplex&&) = delete;

    // required virtual functions
    ctsTask GetNextTaskFromPattern() noexcept override;
    ctsIoPatternError CompleteTaskBackToPattern(const ctsTask& task, uint32_t completedBytes) noexcept override;

private:
    // need to know when to stop sending
    uint64_t m_remainingSendBytes{0};
    uint64_t m_remainingRecvBytes{0};
    uint32_t m_recvNeeded{0};
    uint32_t m_sendBytesInflight{0};
};


///////////////////////////////////////////////////////////////////////////////////////////////////
///
///  - UDP Media server
///    -- Receives a START message from a client to establish a 'connection'
///    -- Streams datagrams at the specified BitRate and FrameRate
///    -- Responds to RESEND requests out-of-band from the normal stream
///    -- Remains alive until the DONE message is sent from the client
///
///////////////////////////////////////////////////////////////////////////////////////////////////
class ctsIoPatternMediaStreamServer final : public ctsIoPatternStatistics<ctsUdpStatistics>
{
public:
    ctsIoPatternMediaStreamServer() noexcept;
    ~ctsIoPatternMediaStreamServer() noexcept override = default;

    ctsIoPatternMediaStreamServer(const ctsIoPatternMediaStreamServer&) = delete;
    ctsIoPatternMediaStreamServer& operator=(const ctsIoPatternMediaStreamServer&) = delete;
    ctsIoPatternMediaStreamServer(ctsIoPatternMediaStreamServer&&) = delete;
    ctsIoPatternMediaStreamServer& operator=(ctsIoPatternMediaStreamServer&&) = delete;

    // required virtual functions
    ctsTask GetNextTaskFromPattern() noexcept override;
    ctsIoPatternError CompleteTaskBackToPattern(const ctsTask& task, uint32_t currentTransfer) noexcept override;

private:
    uint32_t m_frameSizeBytes{0};
    uint32_t m_currentFrameRequested{0};
    uint32_t m_currentFrameCompleted{0};
    uint32_t m_frameRateFps{0};
    uint32_t m_currentFrame{1};
    int64_t m_baseTimeMilliseconds{0};

    enum class ServerState
    {
        NotStarted,
        IdSent,
        IoStarted
    } m_state{ServerState::NotStarted};
};


///////////////////////////////////////////////////////////////////////////////////////////////////
///
///  - UDP Media client
///    -- Sends a START message to the server to establish a 'connection'
///    -- Receives a stream of datagrams at the specified BitRate and FrameRate
///    -- Sends a RESEND requests out-of-band from the normal stream if peeks ahead 
///       and sees a missing frame
///    -- Processes frames after a Buffering period of time
///    -- Sends a DONE message to the server after processing all frames
///
///////////////////////////////////////////////////////////////////////////////////////////////////
class ctsIoPatternMediaStreamClient final : public ctsIoPatternStatistics<ctsUdpStatistics>
{
public:
    ctsIoPatternMediaStreamClient();
    ~ctsIoPatternMediaStreamClient() noexcept override;

    ctsIoPatternMediaStreamClient(const ctsIoPatternMediaStreamClient&) = delete;
    ctsIoPatternMediaStreamClient& operator=(const ctsIoPatternMediaStreamClient&) = delete;
    ctsIoPatternMediaStreamClient(ctsIoPatternMediaStreamClient&&) = delete;
    ctsIoPatternMediaStreamClient& operator=(ctsIoPatternMediaStreamClient&&) = delete;

    // required virtual functions
    ctsTask GetNextTaskFromPattern() noexcept override;
    ctsIoPatternError CompleteTaskBackToPattern(const ctsTask& task, uint32_t completedBytes) noexcept override;

private:
    // private member variables
    PTP_TIMER m_rendererTimer = nullptr;
    PTP_TIMER m_startTimer = nullptr;

    int64_t m_baseTimeMilliseconds = 0LL;
    const double m_frameRateMsPerFrame = 0LL;
    const uint32_t m_frameSizeBytes = ctsConfig::GetMediaStream().FrameSizeBytes;
    const uint32_t m_finalFrame = ctsConfig::GetMediaStream().StreamLengthFrames;

    uint32_t m_initialBufferFrames = ctsConfig::GetMediaStream().BufferedFrames;
    uint32_t m_timerWheelOffsetFrames = 0UL;
    uint32_t m_recvNeeded = ctsConfig::g_configSettings->PrePostRecvs;

    // these must be protected by the base class cs
    // - the base lock is always taken before our virtual functions are called
    // - so this is most important to know in our timer callback

    std::vector<ctsConfig::JitterFrameEntry> m_frameEntries;
    std::vector<ctsConfig::JitterFrameEntry>::iterator m_headEntry;

    // tracking for jitter information
    ctsConfig::JitterFrameEntry m_firstFrame;
    ctsConfig::JitterFrameEntry m_previousFrame;

    bool m_finishedStream = false;

    // member functions - all require the base lock
    std::vector<ctsConfig::JitterFrameEntry>::iterator FindSequenceNumber(int64_t sequenceNumber) noexcept;

    bool ReceivedBufferedFrames() noexcept;

    [[nodiscard]] bool SetNextTimer(bool initialTimer) const noexcept;

    void SetNextStartTimer() const noexcept;

    void RenderFrame() noexcept;

    /// The "Renderer" processes frames at the specified frame rate
    static VOID CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID pContext, PTP_TIMER) noexcept;
    /// Callback to track when the server has actually started sending
    static VOID CALLBACK StartCallback(PTP_CALLBACK_INSTANCE, _In_ PVOID pContext, PTP_TIMER) noexcept;
};
} //namespace
