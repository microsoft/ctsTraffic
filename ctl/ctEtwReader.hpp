/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/
#pragma once

// clang-format off
#include <algorithm>
#include <cassert>
#include <cwchar>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Windows.h>
#include <guiddef.h>
// these 4 headers needed ETW APIs
#include <evntcons.h>
#include <evntrace.h>
#include <winmeta.h>
#include <wmistr.h>

#include "ctEtwRecord.hpp"

#include "wil/resource.h"
// clang-format on

/// \brief Re-defining this flag from ntwmi.h to avoid pulling in a bunch of dependencies and conflicts
constexpr ULONG EVENT_TRACE_USE_MS_FLUSH_TIMER = 0x00000010; ///< FlushTimer value in milliseconds
constexpr uint32_t SLEEP_MS_WAITING_FOR_EXPECTED_RECORDS = 50;

namespace ctl {
/**
 * @brief Reads and manages ETW trace sessions.
 */
class ctEtwReader
{
  public:
    /**
     * @brief Default constructor.
     */
    ctEtwReader() noexcept = default;

    /**
     * @brief Constructs ctEtwReader with an event filter.
     * @param _eventFilter Event filter function.
     */
    template <typename T> explicit ctEtwReader(T _eventFilter) noexcept : m_eventFilter(std::move(_eventFilter)) {}

    /**
     * @brief Destructor. Stops the session and asserts handles are closed.
     */
    ~ctEtwReader() noexcept
    {
        StopTraceSession();
        assert(NULL == m_sessionHandle);
        assert(INVALID_PROCESSTRACE_HANDLE == m_traceHandle);
        assert(nullptr == m_threadHandle);
    }

    ctEtwReader(const ctEtwReader&) = delete;
    ctEtwReader&
    operator=(const ctEtwReader&) = delete;

    ctEtwReader(ctEtwReader&&) noexcept = default;
    ctEtwReader&
    operator=(ctEtwReader&&) noexcept = default;

    /**
     * @brief Creates and starts a trace session with the specified name.
     * @param szSessionName Null-terminated string for the unique name for the new trace session.
     * @param szFileName Null-terminated string for the ETL trace file to be created. Optional, pass NULL to not create
     * a trace file.
     * @param sessionGUID Unique GUID identifying this trace session to all providers.
     * @param msFlushTimer Value, in milliseconds, of the ETW flush timer. Optional, default is 0 (system default).
     * @return HRESULT status code.
     */
    [[nodiscard]] HRESULT
    StartTraceSession(
        _In_ PCWSTR szSessionName,
        _In_opt_ PCWSTR szFileName,
        const GUID& sessionGUID,
        ULONG msFlushTimer = 0) noexcept;

    /**
     * @brief Opens a trace session from the specific ETL file.
     * @param szFileName Null-terminated string for the ETL trace file to be read.
     * @return HRESULT status code.
     */
    [[nodiscard]] HRESULT
    OpenSavedTraceSession(_In_ PCWSTR szFileName) noexcept;

    /**
     * @brief Waits on the session's thread handle until the thread exits.
     */
    void
    WaitForTraceSessionToExit() const noexcept;

    /**
     * @brief Returns the session's thread handle.
     * @return HANDLE to the session thread.
     */
    [[nodiscard]] HANDLE
    GetTraceSessionHandle() const noexcept
    {
        return m_threadHandle;
    }

    /**
     * @brief Stops the event trace session that was started with StartSession() and disables all providers.
     */
    void
    StopTraceSession() noexcept;

    /**
     * @brief Stops the event trace session that was started with the provided name and disables all providers.
     * @param szSessionName Null-terminated string for the unique name for the trace session.
     */
    static void
    StopTraceSession(_In_ PCWSTR szSessionName) noexcept;

    /**
     * @brief Enables the specified ETW providers in the existing trace session.
     * Fails if StartSession() has not been called successfully, or if the worker thread pumping events has stopped
     * unexpectedly.
     * @param providerGUIDs Vector of ETW Provider GUIDs to enable. An empty vector enables no providers.
     * @param uLevel The "Level" parameter passed to EnableTraceEx for providers specified. Default is
     * TRACE_LEVEL_VERBOSE (all).
     * @param uMatchAnyKeyword The "MatchAnyKeyword" parameter passed to EnableTraceEx. Default is 0 (none).
     * @param uMatchAllKeyword The "MatchAllKeyword" parameter passed to EnableTraceEx. Default is 0 (none).
     * @return HRESULT status code.
     */
    [[nodiscard]] HRESULT
    EnableTraceProviders(
        _In_ const std::vector<GUID>& providerGUIDs,
        UCHAR uLevel = TRACE_LEVEL_VERBOSE,
        ULONGLONG uMatchAnyKeyword = 0,
        ULONGLONG uMatchAllKeyword = 0) noexcept;

    /**
     * @brief Disables the specified ETW providers in the existing trace session.
     * Fails if StartSession() has not been called successfully, or if the worker thread pumping events has stopped
     * unexpectedly.
     * @param providerGUIDs Vector of ETW Provider GUIDs to disable. An empty vector disables no providers.
     * @return HRESULT status code.
     */
    [[nodiscard]] HRESULT
    DisableTraceProviders(const std::vector<GUID>& providerGUIDs) noexcept;

    /**
     * @brief Explicitly flushes events from the internal ETW buffers.
     * Called internally when trying to Find or Remove, and exposed publicly for callers who need it in other scenarios.
     * @return HRESULT status code.
     */
    [[nodiscard]] HRESULT
    FlushTraceSession() const noexcept;

  private:
    /**
     * @brief Callback for for ETW indicating a new record was traced.
     * @param pEventRecord Pointer to the event record.
     */
    static VOID WINAPI
    EventRecordCallback(PEVENT_RECORD pEventRecord) noexcept;

    /**
     * @brief Callback for event trace buffers.
     * @param Buffer Pointer to the event trace logfile buffer.
     * @return ULONG indicating buffer status.
     */
    static ULONG WINAPI
    BufferCallback(PEVENT_TRACE_LOGFILE Buffer) noexcept;

    /**
     * @brief Builds the EVENT_TRACE_PROPERTIES structure for a trace session.
     * @param pPropertyBuffer Shared pointer to the buffer for properties.
     * @param szSessionName Session name.
     * @param szFileName File name (optional).
     * @param msFlushTimer Flush timer value.
     * @return Pointer to EVENT_TRACE_PROPERTIES.
     */
    EVENT_TRACE_PROPERTIES*
    BuildEventTraceProperties(
        _Inout_ std::shared_ptr<BYTE[]>& pPropertyBuffer,
        _In_ PCWSTR szSessionName,
        _In_opt_ PCWSTR szFileName,
        ULONG msFlushTimer) const;

    /**
     * @brief Verifies the trace session and checks for thread errors.
     */
    void
    VerifyTraceSessionIsRunning();

    /**
     * @brief Opens the trace and starts receiving events.
     * @param eventLogfile Reference to EVENT_TRACE_LOGFILE structure.
     */
    void
    OpenTraceImpl(EVENT_TRACE_LOGFILE& eventLogfile);

    std::function<void(const EVENT_RECORD* pRecord)> m_eventFilter{};
    TRACEHANDLE m_sessionHandle{NULL};
    TRACEHANDLE m_traceHandle{INVALID_PROCESSTRACE_HANDLE};
    HANDLE m_threadHandle{nullptr};
    GUID m_sessionGUID{};
    UINT m_numBuffers{0};
    bool m_initNumBuffers{false};
};

inline HRESULT
ctEtwReader::StartTraceSession(
    _In_ PCWSTR szSessionName, _In_opt_ PCWSTR szFileName, const GUID& sessionGUID, ULONG msFlushTimer) noexcept
try {
    // block improper reentrancy
    if (m_sessionHandle != NULL || m_traceHandle != INVALID_PROCESSTRACE_HANDLE || m_threadHandle != nullptr) {
        throw std::runtime_error("ctEtwReader::StartSession is called while a session is already started");
    }

    // Need a copy of the session GUID to enable/disable providers
    m_sessionGUID = sessionGUID;

    // allocate the buffer for the EVENT_TRACE_PROPERTIES structure
    // - plus the session name and file name appended to the end of the struct
    std::shared_ptr<BYTE[]> pPropertyBuffer;
    EVENT_TRACE_PROPERTIES* pProperties =
        BuildEventTraceProperties(pPropertyBuffer, szSessionName, szFileName, msFlushTimer);

    // Now start the trace session
    auto error_code = StartTrace(
        &m_sessionHandle, // session handle
        szSessionName,    // session name
        pProperties       // trace properties struct
    );
    if (ERROR_ALREADY_EXISTS == error_code) {
        // Try to stop the session by its session name
        EVENT_TRACE_PROPERTIES tempProperties{};
        tempProperties.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
        // best effort to stop the existing session
        ControlTrace(NULL, szSessionName, &tempProperties, EVENT_TRACE_CONTROL_STOP);
        // Try to start the session again
        error_code = StartTrace(
            &m_sessionHandle, // session handle
            szSessionName,    // session name
            pProperties       // trace properties struct
        );
    }
    if (error_code != ERROR_SUCCESS) {
        THROW_WIN32(error_code);
    }

    // forced to make a copy of szSessionName in a non-const string for the EVENT_TRACE_LOGFILE.LoggerName member
    const auto sessionSize = wcslen(szSessionName) + 1;
    const std::shared_ptr<wchar_t[]> localSessionName(new wchar_t[sessionSize]);
    wcscpy_s(localSessionName.get(), sessionSize, szSessionName);
    localSessionName.get()[sessionSize - 1] = L'\0';

    // Set up the EVENT_TRACE_LOGFILE to prepare the callback for real-time notification
    EVENT_TRACE_LOGFILE eventLogfile{};
    eventLogfile.LogFileName = nullptr;
    eventLogfile.LoggerName = localSessionName.get();
    eventLogfile.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    eventLogfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_REAL_TIME;
    eventLogfile.BufferCallback = nullptr;
    eventLogfile.EventCallback = nullptr;
    eventLogfile.EventRecordCallback = EventRecordCallback;
    eventLogfile.Context = reinterpret_cast<VOID*>(this); // a PVOID to pass to the callback function
    OpenTraceImpl(eventLogfile);

    return S_OK;
}
CATCH_RETURN();

inline HRESULT
ctEtwReader::OpenSavedTraceSession(_In_ PCWSTR szFileName) noexcept
try {
    if (m_sessionHandle != NULL || m_traceHandle != INVALID_PROCESSTRACE_HANDLE || m_threadHandle != nullptr) {
        throw std::runtime_error("ctEtwReader::StartSession is called while a session is already started");
    }

    // forced to make a copy of szFileName in a non-const string for the EVENT_TRACE_LOGFILE.LogFileName member
    const size_t fileNameSize = wcslen(szFileName) + 1;
    const std::shared_ptr<wchar_t[]> localFileName(new wchar_t[fileNameSize]);
    wcscpy_s(localFileName.get(), fileNameSize, szFileName);
    localFileName.get()[fileNameSize - 1] = L'\0';

    // Set up the EVENT_TRACE_LOGFILE to prepare the callback for real-time notification
    EVENT_TRACE_LOGFILE eventLogfile{};
    eventLogfile.LogFileName = localFileName.get();
    eventLogfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    eventLogfile.BufferCallback = BufferCallback;
    eventLogfile.EventCallback = nullptr;
    eventLogfile.EventRecordCallback = EventRecordCallback;
    eventLogfile.Context = reinterpret_cast<VOID*>(this); // a PVOID to pass to the callback function
    OpenTraceImpl(eventLogfile);

    return S_OK;
}
CATCH_RETURN();

inline void
ctEtwReader::OpenTraceImpl(EVENT_TRACE_LOGFILE& eventLogfile)
{
    // Open a trace handle to start receiving events on the callback
    // - need to define "Invalid handle value" as a TRACEHANDLE,
    //   since they are different types (ULONG64 versus void*)
    m_traceHandle = ::OpenTrace(&eventLogfile);
    if (m_traceHandle == INVALID_PROCESSTRACE_HANDLE) {
        THROW_LAST_ERROR();
    }
    m_threadHandle = CreateThread(
        nullptr,
        0,
        [](LPVOID lpParameter) -> DWORD {
            // Must call ProcessTrace to start the events going to the callback
            // ProcessTrace will put the thread into an alertable state while it waits for events
            // and only exit when CloseTrace is called on the trace handle
            return ProcessTrace(static_cast<TRACEHANDLE*>(lpParameter), 1, nullptr, nullptr);
        },
        &m_traceHandle,
        0,
        nullptr);
    if (nullptr == m_threadHandle) {
        THROW_LAST_ERROR();
    }
    // Quick check to see that the worker tread calling ProcessTrace didn't fail out
    VerifyTraceSessionIsRunning();
}

inline void
ctEtwReader::VerifyTraceSessionIsRunning()
{
    // traceHandle will be reset after the user explicitly calls StopSession(),
    // - so only verify the thread if the user hasn't stopped the session
    if (m_traceHandle != INVALID_PROCESSTRACE_HANDLE && m_threadHandle != nullptr) {
        // Quick check to see that the worker tread calling ProcessTrace didn't fail out
        const DWORD dwWait = WaitForSingleObject(m_threadHandle, 0);
        if (WAIT_OBJECT_0 == dwWait) {
            // The worker thread already exited - ProcessTrace() failed
            // - see what the error code was from that thread
            DWORD dwError = 0;
            if (!GetExitCodeThread(m_threadHandle, &dwError)) {
                dwError = GetLastError();
            }
            // Close the thread handle now that it's dead
            CloseHandle(m_threadHandle);
            m_threadHandle = nullptr;
            THROW_WIN32(dwError);
        }
    }
}

inline void
ctEtwReader::WaitForTraceSessionToExit() const noexcept
{
    if (m_threadHandle) {
        const DWORD dwWait = WaitForSingleObject(m_threadHandle, INFINITE);
        FAIL_FAST_IF_MSG(
            dwWait != WAIT_OBJECT_0,
            "Failed waiting on ctEtwReader::StopSession thread to stop [%u - gle %u]",
            dwWait,
            ::GetLastError());
    }
}

inline void
ctEtwReader::StopTraceSession() noexcept
{
    if (m_sessionHandle != NULL) {
        // Stop the session
        EVENT_TRACE_PROPERTIES tempProperties{};
        tempProperties.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
        tempProperties.Wnode.Guid = m_sessionGUID;
        tempProperties.Wnode.ClientContext = 1; // QPC
        tempProperties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        const auto error_code = ::ControlTrace(m_sessionHandle, nullptr, &tempProperties, EVENT_TRACE_CONTROL_STOP);
        // stops the session even when returned ERROR_MORE_DATA
        // - if this fails, there's nothing we can do to compensate
        FAIL_FAST_IF_MSG(
            (error_code != ERROR_MORE_DATA) && (error_code != ERROR_SUCCESS),
            "ctEtwReader::StopSession - ControlTrace failed [%u] : cannot stop the trace session",
            error_code);
        m_sessionHandle = NULL;
    }

    // Close the handle from OpenTrace
    if (m_traceHandle != INVALID_PROCESSTRACE_HANDLE) {
        // ProcessTrace is still unblocked and returns success when
        //    ERROR_CTX_CLOSE_PENDING is returned
        const auto error_code = CloseTrace(m_traceHandle);
        FAIL_FAST_IF_MSG(
            (ERROR_SUCCESS != error_code) && (ERROR_CTX_CLOSE_PENDING != error_code),
            "CloseTrace failed [%u] - thus will not unblock the APC thread processing events",
            error_code);
        m_traceHandle = INVALID_PROCESSTRACE_HANDLE;
    }

    // the above call to CloseTrace should exit the thread
    if (m_threadHandle) {
        const auto error_code = WaitForSingleObject(m_threadHandle, INFINITE);
        FAIL_FAST_IF_MSG(
            error_code != WAIT_OBJECT_0,
            "Failed waiting on ctEtwReader::StopSession thread to stop [%u - gle %u]",
            error_code,
            ::GetLastError());
        CloseHandle(m_threadHandle);
        m_threadHandle = nullptr;
    }
}

inline void
ctEtwReader::StopTraceSession(_In_ PCWSTR szSessionName) noexcept
{
    EVENT_TRACE_PROPERTIES tempProperties{};
    tempProperties.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
    const auto error_code = ControlTrace(NULL, szSessionName, &tempProperties, EVENT_TRACE_CONTROL_STOP);
    FAIL_FAST_IF_MSG(
        (error_code != ERROR_MORE_DATA) && (error_code != ERROR_SUCCESS),
        "ctEtwReader::StopSession - ControlTrace failed [%u] : cannot stop the trace session",
        error_code);
}

inline HRESULT
ctEtwReader::EnableTraceProviders(
    _In_ const std::vector<GUID>& providerGUIDs,
    UCHAR uLevel,
    ULONGLONG uMatchAnyKeyword,
    ULONGLONG uMatchAllKeyword) noexcept
try {
    // Block calling if an open session is not running
    VerifyTraceSessionIsRunning();
    // iterate through the std::vector of GUIDs, enabling each provider
    for (const auto& providerGUID : providerGUIDs) {
        THROW_IF_WIN32_ERROR(EnableTraceEx(
            &providerGUID,
            &m_sessionGUID,
            m_sessionHandle,
            TRUE,
            uLevel,
            uMatchAnyKeyword,
            uMatchAllKeyword,
            0,
            nullptr));
    }

    return S_OK;
}
CATCH_RETURN();

inline EVENT_TRACE_PROPERTIES*
ctEtwReader::BuildEventTraceProperties(
    _Inout_ std::shared_ptr<BYTE[]>& pPropertyBuffer,
    _In_ PCWSTR szSessionName,
    _In_opt_ PCWSTR szFileName,
    ULONG msFlushTimer) const
{
    // Get buffer sizes in bytes and characters
    //     +1 for null-terminators
    const size_t cchSessionLength = wcslen(szSessionName) + 1;
    const size_t cbSessionSize = cchSessionLength * sizeof(wchar_t);
    if (cbSessionSize < cchSessionLength) {
        throw std::runtime_error("Overflow passing Session string to ctEtwReader");
    }
    size_t cchFileNameLength = 0;
    if (szFileName) {
        cchFileNameLength = wcslen(szFileName) + 1;
    }
    const size_t cbFileNameSize = cchFileNameLength * sizeof(wchar_t);
    if (cbFileNameSize < cchFileNameLength) {
        throw std::runtime_error("Overflow passing Filename string to ctEtwReader");
    }

    const size_t cbProperties = sizeof(EVENT_TRACE_PROPERTIES) + cbSessionSize + cbFileNameSize;
    pPropertyBuffer.reset(new BYTE[cbProperties]);
    ::ZeroMemory(pPropertyBuffer.get(), cbProperties);
    // Append the strings to the end of the struct
    if (szFileName) {
        // append the filename to the end of the struct
        ::CopyMemory(pPropertyBuffer.get() + sizeof(EVENT_TRACE_PROPERTIES), szFileName, cbFileNameSize);
        // append the session name to the end of the struct
        ::CopyMemory(
            pPropertyBuffer.get() + sizeof(EVENT_TRACE_PROPERTIES) + cbFileNameSize, szSessionName, cbSessionSize);
    } else {
        // append the session name to the end of the struct
        ::CopyMemory(pPropertyBuffer.get() + sizeof(EVENT_TRACE_PROPERTIES), szSessionName, cbSessionSize);
    }

    // Set the required fields for starting a new session:
    //   Wnode.BufferSize
    //   Wnode.Guid
    //   Wnode.ClientContext
    //   Wnode.Flags
    //   LogFileMode
    //   LogFileNameOffset
    //   LoggerNameOffset
    EVENT_TRACE_PROPERTIES* pProperties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(pPropertyBuffer.get());
    pProperties->MinimumBuffers = 1; // smaller will make it easier to flush - explicitly not performance sensitive
    pProperties->Wnode.BufferSize = static_cast<ULONG>(cbProperties);
    pProperties->Wnode.Guid = m_sessionGUID;
    pProperties->Wnode.ClientContext = 1; // QPC
    pProperties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    pProperties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    pProperties->LogFileNameOffset = nullptr == szFileName ? 0 : static_cast<ULONG>(sizeof(EVENT_TRACE_PROPERTIES));
    pProperties->LoggerNameOffset = nullptr == szFileName
                                        ? static_cast<ULONG>(sizeof(EVENT_TRACE_PROPERTIES))
                                        : static_cast<ULONG>(sizeof(EVENT_TRACE_PROPERTIES) + cbFileNameSize);
    if (msFlushTimer != 0) {
        pProperties->LogFileMode |= EVENT_TRACE_USE_MS_FLUSH_TIMER;
        pProperties->FlushTimer = msFlushTimer;
    }

    return pProperties;
}

inline HRESULT
ctEtwReader::DisableTraceProviders(const std::vector<GUID>& providerGUIDs) noexcept
try {
    // Block calling if an open session is not running
    VerifyTraceSessionIsRunning();
    for (const auto& providerGUID : providerGUIDs) {
        THROW_IF_WIN32_ERROR(
            ::EnableTraceEx(&providerGUID, &m_sessionGUID, m_sessionHandle, FALSE, 0, 0, 0, 0, nullptr));
    }

    return S_OK;
}
CATCH_RETURN();

inline HRESULT
ctEtwReader::FlushTraceSession() const noexcept
try {
    if (m_sessionHandle) {
        // Stop the session
        EVENT_TRACE_PROPERTIES tempProperties{};
        tempProperties.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
        tempProperties.Wnode.Guid = m_sessionGUID;
        tempProperties.Wnode.ClientContext = 1; // QPC
        tempProperties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        const auto error_code = ::ControlTrace(m_sessionHandle, nullptr, &tempProperties, EVENT_TRACE_CONTROL_FLUSH);
        // stops the session even when returned ERROR_MORE_DATA
        // - if this fails, there's nothing we can do to compensate
        if (error_code != ERROR_MORE_DATA && error_code != ERROR_SUCCESS) {
            THROW_WIN32(error_code);
        }
    }

    return S_OK;
}
CATCH_RETURN();

inline VOID WINAPI
ctEtwReader::EventRecordCallback(PEVENT_RECORD pEventRecord) noexcept
try {
    ctEtwReader* pEventReader = static_cast<ctEtwReader*>(pEventRecord->UserContext);

    // When opening a saved session from an ETL file, the first event record
    // contains diagnostic information about the contents of the trace - the
    // most important (for us) field being the number of buffers written. By
    // saving this value, we can consume it later on inside BufferCallback to
    // force ProcessTrace() to return when the entire contents of the session
    // have been read.
    bool process = true;
    if (!pEventReader->m_initNumBuffers) {
        const ctEtwRecord eventMessage(pEventRecord);
        std::wstring task;
        if (eventMessage.queryTaskName(task) && task == L"EventTrace") {
            process = false;
            ctEtwRecord::ctPropertyPair pair;
            if (eventMessage.queryEventProperty(L"BuffersWritten", pair)) {
                pEventReader->m_initNumBuffers = true;
                pEventReader->m_numBuffers = *reinterpret_cast<int*>(pair.first.get());
            }
        }
    }

    if (process) {
        pEventReader->m_eventFilter(pEventRecord);
    }
} catch (...) {
}

inline ULONG WINAPI
ctEtwReader::BufferCallback(PEVENT_TRACE_LOGFILE Buffer) noexcept
{
    const ctEtwReader* pEventReader = static_cast<ctEtwReader*>(Buffer->Context);
    return Buffer->BuffersRead != pEventReader->m_numBuffers;
}
} // namespace ctl
