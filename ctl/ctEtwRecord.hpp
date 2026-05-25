// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

// defining this so if we use Tdh types that are not currently supported
// it will break so we can add that support
#define CTL_TDHFORMAT_FATALCONDITION

// CPP Headers
#include <map>
#include <memory>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
#include <cwchar>

// OS Headers
#include <winsock2.h>
#include <Windows.h>
#include <ws2tcpip.H>
#include <Rpc.h>
#include <Sddl.h>
#include <Tdh.h>
#include <mstcpip.h>
// these headers 4 needed ETW APIs
#include <evntcons.h>
#include <evntrace.h>
#include <winmeta.h>
#include <wmistr.h>

#include "wil/resource.h"

#pragma warning(push)
#pragma prefast(     \
    disable : 24007, \
    "Both IPv4 and IPv6 are supported: RtlIpv4AddressToStringW is called on IPv4 addresses; RtlIpv6AddressStringW is called on IPv6 addresses")

namespace ctl {

////////////////////////////////////////////////////////////////////////////////
//
//  class ctEtwRecord
//
//  Encapsulates accessing all the various properties one can potentially
//      gather from a EVENT_RECORD structure passed to the consumer from ETW.
//
//  The constructor takes a ptr to the EVENT_RECORD to encapsulate, and makes
//      a deep copy of all embedded and referenced data structures to access
//      with the   getter member functions.
//
//  There are 2 method-types exposed:    get*() and  query*()
//        get* functions have no parameters, and always return the associated
//          value.  They will always have a value to return from any event.
//
//       query* functions take applicable [out] as parameters, and return bool.
//          The values they retrieve are not guaranteed to be in all events,
//          and will return false if they don't exist in the encapsulated event.
//
//  Note that all returned strings are dynamically allocated and returned via
//      std::shared_ptr<wchar_t> smart pointer objects. Note that
//      copying the object is very cheap (one Interlocked operation) and cannot
//      fail. (thus isn't highly suggested to copy these smart pointer objects
//      by value, not by reference - which defeats the internal ref-counting)
//
//  All methods that do not have a throw() exception specification can throw
//      std::bad_alloc - which is derived from std::exception.  The constructor
//      can throw ctException, which is also derived from std::exception.
//
////////////////////////////////////////////////////////////////////////////////

class ctEtwRecord
{
  public:
    ////////////////////////////////////////////////////////////
    //
    // A public typedef to access the pair class containing the property data
    //
    ////////////////////////////////////////////////////////////
    using ctPropertyPair = std::pair<std::shared_ptr<BYTE[]>, ULONG>;

    ////////////////////////////////////////////////////////////
    //
    // Constructors
    //  - default
    //  - specifying the EVENT_RECORD* to deep-copy
    //
    // Destructor
    // Copy Constructor
    // Copy Assignment operator
    //
    // taking an EVENT_RECORD*
    //  - replaces the existing encapsulated EVENT_RECORD info
    //    with the specified EVENT_RECORD.
    //
    ////////////////////////////////////////////////////////////
    ctEtwRecord() noexcept = default;
    explicit ctEtwRecord(_In_ const EVENT_RECORD*);
    ~ctEtwRecord() noexcept = default;

    ctEtwRecord(const ctEtwRecord&) noexcept = default;
    ctEtwRecord&
    operator=(_In_ const ctEtwRecord&) noexcept = default;
    ctEtwRecord&
    operator=(_In_ const EVENT_RECORD*);

    ctEtwRecord(ctEtwRecord&&) noexcept = default;
    ctEtwRecord&
    operator=(ctEtwRecord&&) noexcept = default;

    ////////////////////////////////////////////////////////////
    //
    // Implementing swap() to be a friendly container
    //
    ////////////////////////////////////////////////////////////
    void
    swap(ctEtwRecord&) noexcept;
    ////////////////////////////////////////////////////////////
    //
    // Printing the entire ETW record
    // Printing just the formatted event message
    // - optionally with full details of each property
    //
    ////////////////////////////////////////////////////////////
    void
    writeRecord(std::wstring& reusable_string) const;
    std::wstring
    writeRecord() const
    {
        std::wstring wsRecord;
        writeRecord(wsRecord);
        return wsRecord;
    }
    void
    writeFormattedMessage(std::wstring& reusable_string, bool include_message_properties) const;
    std::wstring
    writeFormattedMessage(bool _details = false) const
    {
        std::wstring wsRecord;
        writeFormattedMessage(wsRecord, _details);
        return wsRecord;
    }
    std::map<std::wstring, std::wstring>
    writeMessageProperties() const;
    ////////////////////////////////////////////////////////////
    //
    // comparison operators
    //
    ////////////////////////////////////////////////////////////
    bool
    operator==(_In_ const ctEtwRecord&) const;
    bool
    operator!=(_In_ const ctEtwRecord&) const;
    ////////////////////////////////////////////////////////////
    //
    // EVENT_HEADER fields (8)
    //
    ////////////////////////////////////////////////////////////
    ULONG
    getThreadId() const noexcept;
    ULONG
    getProcessId() const noexcept;
    LARGE_INTEGER
    getTimeStamp() const noexcept;
    GUID
    getProviderId() const noexcept;
    GUID
    getActivityId() const noexcept;
    _Success_(return) bool queryKernelTime(_Out_ ULONG*) const noexcept;
    _Success_(return) bool queryUserTime(_Out_ ULONG*) const noexcept;
    ULONG64
    getProcessorTime() const noexcept;
    ////////////////////////////////////////////////////////////
    //
    // EVENT_DESCRIPTOR fields (7)
    //
    ////////////////////////////////////////////////////////////
    USHORT
    getEventId() const noexcept;
    UCHAR
    getVersion() const noexcept;
    UCHAR
    getChannel() const noexcept;
    UCHAR
    getLevel() const noexcept;
    UCHAR
    getOpcode() const noexcept;
    USHORT
    getTask() const noexcept;
    ULONGLONG
    getKeyword() const noexcept;
    ////////////////////////////////////////////////////////////
    //
    // ETW_BUFFER_CONTEXT fields (3)
    //
    ////////////////////////////////////////////////////////////
    UCHAR
    getProcessorNumber() const noexcept;
    UCHAR
    getAlignment() const noexcept;
    USHORT
    getLoggerId() const noexcept;
    ////////////////////////////////////////////////////////////
    //
    // EVENT_HEADER_EXTENDED_DATA_ITEM options (6)
    //
    ////////////////////////////////////////////////////////////
    _Success_(return) bool queryRelatedActivityId(_Out_ GUID*) const noexcept;
    _Success_(return) bool querySID(_Out_ std::shared_ptr<BYTE[]>&, _Out_ size_t*) const;
    _Success_(return) bool queryTerminalSessionId(_Out_ ULONG*) const noexcept;
    _Success_(return) bool queryTransactionInstanceId(_Out_ ULONG*) const noexcept;
    _Success_(return) bool queryTransactionParentInstanceId(_Out_ ULONG*) const noexcept;
    _Success_(return) bool queryTransactionParentGuid(_Out_ GUID*) const noexcept;
    ////////////////////////////////////////////////////////////
    //
    // TRACE_EVENT_INFO options (16)
    //
    ////////////////////////////////////////////////////////////
    _Success_(return) bool queryProviderGuid(_Out_ GUID*) const noexcept;
    _Success_(return) bool queryDecodingSource(_Out_ DECODING_SOURCE*) const noexcept;
    _Success_(return) bool queryProviderName(_Out_ std::wstring&) const;
    _Success_(return) bool queryLevelName(_Out_ std::wstring&) const;
    _Success_(return) bool queryChannelName(_Out_ std::wstring&) const;
    _Success_(return) bool queryKeywords(_Out_ std::vector<std::wstring>&) const;
    _Success_(return) bool queryTaskName(_Out_ std::wstring&) const;
    _Success_(return) bool queryOpcodeName(_Out_ std::wstring&) const;
    _Success_(return) bool queryEventMessage(_Out_ std::wstring&) const;
    _Success_(return) bool queryProviderMessageName(_Out_ std::wstring&) const;
    _Success_(return) bool queryPropertyCount(_Out_ ULONG*) const noexcept;
    _Success_(return) bool queryTopLevelPropertyCount(_Out_ ULONG*) const noexcept;
    _Success_(return) bool queryEventPropertyStringValue(_Out_ std::wstring&) const;
    _Success_(return) bool queryEventPropertyName(
        _In_ unsigned long ulIndex, _Out_ std::wstring& out_wsPropertyName) const;
    _Success_(return) bool queryEventProperty(_In_ PCWSTR, _Out_ std::wstring&) const;
    _Success_(return) bool queryEventProperty(_In_ PCWSTR, _Out_ ctPropertyPair&) const;
    _Success_(return) bool queryEventProperty(_In_ unsigned long, _Out_ std::wstring&) const;

  private:
    // private method to build a formatted string from the specified property offset
    std::wstring buildEventPropertyString(ULONG) const;
    // eventHeader and etwBufferContext are just shallow-copies
    //      of the EVENT_HEADER and ETW_BUFFER_CONTEXT structs.
    EVENT_HEADER m_eventHeader{};
    ETW_BUFFER_CONTEXT m_etwBufferContext{};

    // v_eventHeaderExtendedData and v_pEventHeaderData stores a deep-copy
    //      of the EVENT_HEADER_EXTENDED_DATA_ITEM struct.
    std::vector<EVENT_HEADER_EXTENDED_DATA_ITEM> m_eventHeaderExtendedData;
    std::vector<std::shared_ptr<BYTE[]>> m_eventHeaderData;

    // ptraceEventInfo stores a deep copy of the TRACE_EVENT_INFO struct.
    std::shared_ptr<BYTE[]> m_traceEventInfo;
    ULONG m_cbTraceEventInfo{0};

    // vPropertyInfo stores an array of all properties
    std::vector<ctPropertyPair> m_traceProperties;

    typedef std::pair<std::shared_ptr<WCHAR[]>, ULONG> ctMappingPair;
    std::vector<ctMappingPair> m_traceMapping;
    //
    // need to allow a default empty constructor, so must track initialization status
    //
    bool m_initialized{false};
};

inline ctEtwRecord::ctEtwRecord(_In_ const EVENT_RECORD* event_record)
    : m_eventHeader(event_record->EventHeader), m_etwBufferContext(event_record->BufferContext)
{
    if (event_record->ExtendedDataCount > 0) {
        // Copying the EVENT_HEADER_EXTENDED_DATA_ITEM requires a deep-copy its data buffer
        //    and to point the local struct at the locally allocated and copied buffer
        //    since we won't have direct access to the original buffer later
        m_eventHeaderExtendedData.resize(event_record->ExtendedDataCount);
        m_eventHeaderData.resize(event_record->ExtendedDataCount);

        for (unsigned uCount = 0; uCount < m_eventHeaderExtendedData.size(); ++uCount) {
            PEVENT_HEADER_EXTENDED_DATA_ITEM pTempItem = event_record->ExtendedData;
            pTempItem += uCount;

            std::shared_ptr<BYTE[]> pTempBytes(new BYTE[pTempItem->DataSize]);
            memcpy_s(
                pTempBytes.get(),
                pTempItem->DataSize,
                reinterpret_cast<BYTE*>(pTempItem->DataPtr),
                pTempItem->DataSize);

            m_eventHeaderData[uCount] = pTempBytes;
            m_eventHeaderExtendedData[uCount] = *pTempItem;
            m_eventHeaderExtendedData[uCount].DataPtr =
                reinterpret_cast<ULONGLONG>(0ULL + m_eventHeaderData[uCount].get());
        }
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
        m_cbTraceEventInfo = event_record->UserDataLength;
        m_traceEventInfo.reset(new BYTE[m_cbTraceEventInfo]);
        memcpy_s(m_traceEventInfo.get(), m_cbTraceEventInfo, event_record->UserData, m_cbTraceEventInfo);
    } else {
        m_cbTraceEventInfo = 0;
        ULONG tdhError =
            ::TdhGetEventInformation(const_cast<PEVENT_RECORD>(event_record), 0, nullptr, nullptr, &m_cbTraceEventInfo);
        if (ERROR_INSUFFICIENT_BUFFER == tdhError) {
            m_traceEventInfo.reset(new BYTE[m_cbTraceEventInfo]);
            THROW_IF_WIN32_ERROR(::TdhGetEventInformation(
                const_cast<PEVENT_RECORD>(event_record),
                0,
                nullptr,
                reinterpret_cast<PTRACE_EVENT_INFO>(m_traceEventInfo.get()),
                &m_cbTraceEventInfo));
        }

        //
        // retrieve all property data points - need to do this in the constructor since the original EVENT_RECORD is
        // required
        //
        BYTE* const pByteInfo = m_traceEventInfo.get();
        TRACE_EVENT_INFO* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
        const unsigned long total_properties = pTraceInfo->TopLevelPropertyCount;
        if (total_properties > 0) {
            //
            // variables for TdhFormatProperty
            //
            USHORT UserDataLength = event_record->UserDataLength;
            PBYTE UserData = static_cast<PBYTE>(event_record->UserData);
            //
            // go through event properties, and pull out the necessary data
            //
            for (unsigned long property_count = 0; property_count < total_properties; ++property_count) {
                if (pTraceInfo->EventPropertyInfoArray[property_count].Flags & PropertyStruct) {
                    //
                    // TODO
                    // currently not supporting deep-copying event data of structs
                    //
#ifdef CTL_TDHFORMAT_FATALCONDITION
                    ::DebugBreak();
#endif
                    m_traceMapping.emplace_back(std::shared_ptr<WCHAR[]>(), 0);
                    m_traceProperties.emplace_back(std::shared_ptr<BYTE[]>(), 0);
                } else if (pTraceInfo->EventPropertyInfoArray[property_count].count > 1) {
                    //
                    // TODO
                    // currently not supporting deep-copying event data of arrays
                    //
#ifdef CTL_TDHFORMAT_FATALCONDITION
                    ::DebugBreak();
#endif
                    m_traceMapping.emplace_back(std::shared_ptr<WCHAR[]>(), 0);
                    m_traceProperties.emplace_back(std::shared_ptr<BYTE[]>(), 0);
                } else {
                    // define the event we want with a PROPERTY_DATA_DESCRIPTOR
                    PROPERTY_DATA_DESCRIPTOR dataDescriptor;
                    dataDescriptor.PropertyName = reinterpret_cast<ULONGLONG>(
                        pByteInfo + pTraceInfo->EventPropertyInfoArray[property_count].NameOffset);
                    dataDescriptor.ArrayIndex = ULONG_MAX;
                    dataDescriptor.Reserved = 0UL;

                    // get the buffer size first
                    ULONG cbPropertyData = 0;
                    THROW_IF_WIN32_ERROR(::TdhGetPropertySize(
                        const_cast<PEVENT_RECORD>(event_record),
                        0,       // not using WPP or 'classic' ETW
                        nullptr, // not using WPP or 'classic' ETW
                        1,       // one property at a time - not support structs of data at this time
                        &dataDescriptor,
                        &cbPropertyData));

                    //
                    // now allocate the required buffer, and copy the data
                    // - only if the buffer size > 0
                    //
                    std::shared_ptr<BYTE[]> pPropertyData;
                    if (cbPropertyData > 0) {
                        pPropertyData.reset(new BYTE[cbPropertyData]);
                        THROW_IF_WIN32_ERROR(::TdhGetProperty(
                            const_cast<PEVENT_RECORD>(event_record),
                            0,       // not using WPP or 'classic' ETW
                            nullptr, // not using WPP or 'classic' ETW
                            1,       // one property at a time - not support structs of data at this time
                            &dataDescriptor,
                            cbPropertyData,
                            pPropertyData.get()));
                    }
                    m_traceProperties.emplace_back(pPropertyData, cbPropertyData);

                    //
                    // additionally capture the mapped string for the property, if it exists
                    //
                    DWORD dwMapInfoSize = 0;
                    std::shared_ptr<BYTE[]> pPropertyMap;
                    PWSTR szMapName = reinterpret_cast<PWSTR>(
                        pByteInfo + pTraceInfo->EventPropertyInfoArray[property_count].nonStructType.MapNameOffset);
                    // first query the size needed
                    tdhError = ::TdhGetEventMapInformation(
                        const_cast<PEVENT_RECORD>(event_record), szMapName, nullptr, &dwMapInfoSize);
                    if (ERROR_INSUFFICIENT_BUFFER == tdhError) {
                        pPropertyMap.reset(new BYTE[dwMapInfoSize]);
                        tdhError = ::TdhGetEventMapInformation(
                            const_cast<PEVENT_RECORD>(event_record),
                            szMapName,
                            reinterpret_cast<PEVENT_MAP_INFO>(pPropertyMap.get()),
                            &dwMapInfoSize);
                    }
                    switch (tdhError) {
                    case ERROR_SUCCESS:
                        // all good - do nothing
                        break;
                    case ERROR_NOT_FOUND:
                        // this is OK to keep this event - there just wasn't a mapping for a formatted string
                        pPropertyMap.reset();
                        break;
                    default:
                        // any other error is an unexpected failure
#ifdef CTL_TDHFORMAT_FATALCONDITION
                        FAIL_FAST_MSG(
                            "TdhGetEventMapInformation failed with error %u, EVENT_RECORD %p, TRACE_EVENT_INFO %p",
                            tdhError,
                            event_record,
                            pTraceInfo);
#else
                        pPropertyMap.reset();
#endif
                    }
                    //
                    // if we successfully retrieved the property info
                    // format the mapped property value
                    //
                    if (pPropertyMap) {
                        USHORT property_length = pTraceInfo->EventPropertyInfoArray[property_count].length;
                        // per MSDN, must manually set the length for TDH_OUTTYPE_IPV6
                        if (TDH_INTYPE_BINARY ==
                                pTraceInfo->EventPropertyInfoArray[property_count].nonStructType.InType &&
                            TDH_OUTTYPE_IPV6 ==
                                pTraceInfo->EventPropertyInfoArray[property_count].nonStructType.OutType) {
                            property_length = static_cast<USHORT>(sizeof IN6_ADDR);
                        }
                        const ULONG pointer_size = event_record->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER ? 4 : 8;
                        ULONG formattedPropertySize = 0;
                        USHORT UserDataConsumed = 0;
                        std::shared_ptr<WCHAR[]> formatted_value;
                        tdhError = ::TdhFormatProperty(
                            pTraceInfo,
                            reinterpret_cast<PEVENT_MAP_INFO>(pPropertyMap.get()),
                            pointer_size,
                            pTraceInfo->EventPropertyInfoArray[property_count].nonStructType.InType,
                            pTraceInfo->EventPropertyInfoArray[property_count].nonStructType.OutType,
                            property_length,
                            UserDataLength,
                            UserData,
                            &formattedPropertySize,
                            nullptr,
                            &UserDataConsumed);
                        if (ERROR_INSUFFICIENT_BUFFER == tdhError) {
                            formatted_value.reset(new WCHAR[formattedPropertySize / sizeof(WCHAR)]);
                            tdhError = ::TdhFormatProperty(
                                pTraceInfo,
                                reinterpret_cast<PEVENT_MAP_INFO>(pPropertyMap.get()),
                                pointer_size,
                                pTraceInfo->EventPropertyInfoArray[property_count].nonStructType.InType,
                                pTraceInfo->EventPropertyInfoArray[property_count].nonStructType.OutType,
                                property_length,
                                UserDataLength,
                                UserData,
                                &formattedPropertySize,
                                formatted_value.get(),
                                &UserDataConsumed);
                        }
                        if (tdhError != ERROR_SUCCESS) {
#ifdef CTL_TDHFORMAT_FATALCONDITION
                            FAIL_FAST_MSG(
                                "TdhFormatProperty failed with error %u, EVENT_RECORD %p, TRACE_EVENT_INFO %p",
                                tdhError,
                                event_record,
                                pTraceInfo);
#else
                            m_traceMapping.emplace_back(std::shared_ptr<WCHAR[]>(), 0);
#endif
                        } else {
                            UserDataLength -= UserDataConsumed;
                            UserData += UserDataConsumed;
                            //
                            // now add the value/size pair to the member std::vector storing all properties
                            //
                            m_traceMapping.emplace_back(formatted_value, formattedPropertySize);
                        }
                    } else {
                        // store null values
                        m_traceMapping.emplace_back(std::shared_ptr<WCHAR[]>(), 0);
                    }
                }
            }
        }
    }

    m_initialized = true;
}

inline ctEtwRecord&
ctEtwRecord::operator=(_In_ const EVENT_RECORD* event_record)
{
    ctEtwRecord temp(event_record);
    swap(temp);
    m_initialized = true; // explicitly flag to true
    return *this;
}

inline void
ctEtwRecord::swap(ctEtwRecord& in_event) noexcept
{
    using std::swap;
    swap(m_eventHeaderExtendedData, in_event.m_eventHeaderExtendedData);
    swap(m_eventHeaderData, in_event.m_eventHeaderData);
    swap(m_traceEventInfo, in_event.m_traceEventInfo);
    swap(m_cbTraceEventInfo, in_event.m_cbTraceEventInfo);
    swap(m_traceProperties, in_event.m_traceProperties);
    swap(m_traceMapping, in_event.m_traceMapping);
    swap(m_initialized, in_event.m_initialized);
    //
    // manually swap these structures
    //
    EVENT_HEADER tempHeader;
    memcpy_s(
        &tempHeader, // this to temp
        sizeof(EVENT_HEADER),
        &m_eventHeader,
        sizeof(EVENT_HEADER));
    memcpy_s(
        &m_eventHeader, // in_event to this
        sizeof(EVENT_HEADER),
        &in_event.m_eventHeader,
        sizeof(EVENT_HEADER));
    memcpy_s(
        &in_event.m_eventHeader, // temp to in_event
        sizeof(EVENT_HEADER),
        &tempHeader,
        sizeof(EVENT_HEADER));

    ETW_BUFFER_CONTEXT tempBuffContext;
    memcpy_s(
        &tempBuffContext, // this to temp
        sizeof(ETW_BUFFER_CONTEXT),
        &m_etwBufferContext,
        sizeof(ETW_BUFFER_CONTEXT));
    memcpy_s(
        &m_etwBufferContext, // in_event to this
        sizeof(ETW_BUFFER_CONTEXT),
        &in_event.m_etwBufferContext,
        sizeof(ETW_BUFFER_CONTEXT));
    memcpy_s(
        &in_event.m_etwBufferContext, // temp to in_event
        sizeof(ETW_BUFFER_CONTEXT),
        &tempBuffContext,
        sizeof(ETW_BUFFER_CONTEXT));
}

////////////////////////////////////////////////////////////////////////////////
//
//  Non-member swap() function for ctEtwRecord
//
//  - Implementing the non-member swap to be usable generically
//
////////////////////////////////////////////////////////////////////////////////
inline void
swap(ctEtwRecord& a, ctEtwRecord& b) noexcept
{
    a.swap(b);
}

////////////////////////////////////////////////////////////////////////////////
//
//  writeRecord()
//
//  - simple text dump of all event properties to a std::wstring object
//
////////////////////////////////////////////////////////////////////////////////
inline void
ctEtwRecord::writeRecord(std::wstring& reusable_string) const
{
    //
    // write to a temp string - but use the caller's buffer
    //
    std::wstring wsData;
    wsData.swap(reusable_string);
    wsData.clear();

    constexpr unsigned cch_StackBuffer = 100;
    wchar_t stackBuffer[cch_StackBuffer]{};
    GUID guidBuf{};
    ULONG ulData = 0;
    std::wstring wsText;
    wil::unique_rpc_wstr pszGuid;

    //  Data from EVENT_HEADER properties
    wsData += L"\n\tThread ID ";
    _ultow_s(getThreadId(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tProcess ID ";
    _ultow_s(getProcessId(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tTime Stamp ";
    _ui64tow_s(getTimeStamp().QuadPart, stackBuffer, cch_StackBuffer, 16);
    wsData += L"0x";
    wsData += stackBuffer;

    wsData += L"\n\tProvider ID ";
    guidBuf = getProviderId();
    THROW_IF_WIN32_ERROR(::UuidToString(&guidBuf, &pszGuid));
    wsData += reinterpret_cast<LPWSTR>(pszGuid.get());

    wsData += L"\n\tActivity ID ";
    guidBuf = getActivityId();
    THROW_IF_WIN32_ERROR(::UuidToString(&guidBuf, &pszGuid));
    wsData += reinterpret_cast<LPWSTR>(pszGuid.get());

    if (queryKernelTime(&ulData)) {
        wsData += L"\n\tKernel Time ";
        _ultow_s(ulData, stackBuffer, 16);
        wsData += L"0x";
        wsData += stackBuffer;
    }

    if (queryUserTime(&ulData)) {
        wsData += L"\n\tUser Time ";
        _ultow_s(ulData, stackBuffer, 16);
        wsData += L"0x";
        wsData += stackBuffer;
    }

    wsData += L"\n\tProcessor Time: ";
    _ui64tow_s(getProcessorTime(), stackBuffer, cch_StackBuffer, 16);
    wsData += L"0x";
    wsData += stackBuffer;

    //  Data from EVENT_DESCRIPTOR properties
    wsData += L"\n\tEvent ID ";
    _itow_s(getEventId(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tVersion ";
    _itow_s(getVersion(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tChannel ";
    _itow_s(getChannel(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tLevel ";
    _itow_s(getLevel(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tOpcode ";
    _itow_s(getOpcode(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tTask ";
    _itow_s(getTask(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tKeyword ";
    _ui64tow_s(getKeyword(), stackBuffer, cch_StackBuffer, 16);
    wsData += L"0x";
    wsData += stackBuffer;

    //
    //
    //  Data from ETW_BUFFER_CONTEXT properties
    //
    //
    wsData += L"\n\tProcessor ";
    _itow_s(getProcessorNumber(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tAlignment ";
    _itow_s(getAlignment(), stackBuffer, 10);
    wsData += stackBuffer;

    wsData += L"\n\tLogger ID ";
    _itow_s(getLoggerId(), stackBuffer, 10);
    wsData += stackBuffer;

    //
    //
    //  Data from EVENT_HEADER_EXTENDED_DATA_ITEM properties
    //
    //
    if (queryRelatedActivityId(&guidBuf)) {
        wsData += L"\n\tRelated Activity ID ";
        THROW_IF_WIN32_ERROR(::UuidToString(&guidBuf, &pszGuid));
        wsData += reinterpret_cast<LPWSTR>(pszGuid.get());
    }

    std::shared_ptr<BYTE[]> pSID;
    size_t cbSID = 0;
    if (querySID(pSID, &cbSID)) {
        wsData += L"\n\tSID ";
        LPWSTR szSID = nullptr;
        if (::ConvertSidToStringSid(pSID.get(), &szSID)) {
            wsData += szSID;
            ::LocalFree(szSID);
        } else {
            THROW_LAST_ERROR();
        }
    }

    if (queryTerminalSessionId(&ulData)) {
        wsData += L"\n\tTerminal Session ID ";
        _ultow_s(ulData, stackBuffer, 10);
        wsData += stackBuffer;
    }

    if (queryTransactionInstanceId(&ulData)) {
        wsData += L"\n\tTransaction Instance ID ";
        _ultow_s(ulData, stackBuffer, 10);
        wsData += stackBuffer;
    }

    if (queryTransactionParentInstanceId(&ulData)) {
        wsData += L"\n\tTransaction Parent Instance ID ";
        _ultow_s(ulData, stackBuffer, 10);
        wsData += stackBuffer;
    }

    if (queryTransactionParentGuid(&guidBuf)) {
        wsData += L"\n\tTransaction Parent GUID ";
        THROW_IF_WIN32_ERROR(UuidToString(&guidBuf, &pszGuid));
        wsData += reinterpret_cast<LPWSTR>(pszGuid.get());
    }

    //
    //
    //  Accessors for TRACE_EVENT_INFO properties
    //
    //
    if (queryProviderGuid(&guidBuf)) {
        wsData += L"\n\tProvider GUID ";
        THROW_IF_WIN32_ERROR(::UuidToString(&guidBuf, &pszGuid));
        wsData += reinterpret_cast<LPWSTR>(pszGuid.get());
    }

    DECODING_SOURCE decoding_source{};
    if (queryDecodingSource(&decoding_source)) {
        wsData += L"\n\tDecoding Source ";
        switch (decoding_source) {
        case DecodingSourceXMLFile:
            wsData += L"DecodingSourceXMLFile";
            break;
        case DecodingSourceWbem:
            wsData += L"DecodingSourceWbem";
            break;
        case DecodingSourceWPP:
            wsData += L"DecodingSourceWPP";
            break;
        }
    }

    if (queryProviderName(wsText)) {
        wsData += L"\n\tProvider Name " + wsText;
    }

    if (queryLevelName(wsText)) {
        wsData += L"\n\tLevel Name " + wsText;
    }

    if (queryChannelName(wsText)) {
        wsData += L"\n\tChannel Name " + wsText;
    }

    std::vector<std::wstring> keywordData;
    if (queryKeywords(keywordData)) {
        wsData += L"\n\tKeywords [";
        for (auto& keyword : keywordData) {
            wsData += keyword;
        }
        wsData += L"]";
    }

    if (queryTaskName(wsText)) {
        wsData += L"\n\tTask Name " + wsText;
    }

    if (queryOpcodeName(wsText)) {
        wsData += L"\n\tOpcode Name " + wsText;
    }

    if (queryEventMessage(wsText)) {
        wsData += L"\n\tEvent Message " + wsText;
    }

    if (queryProviderMessageName(wsText)) {
        wsData += L"\n\tProvider Message Name " + wsText;
    }

    if (queryPropertyCount(&ulData)) {
        wsData += L"\n\tTotal Property Count ";
        _ultow_s(ulData, stackBuffer, 10);
        wsData += stackBuffer;
    }

    if (queryTopLevelPropertyCount(&ulData)) {
        wsData += L"\n\tTop Level Property Count ";
        _ultow_s(ulData, stackBuffer, 10);
        wsData += stackBuffer;

        if (ulData > 0) {
            BYTE* const pByteInfo = m_traceEventInfo.get();
            TRACE_EVENT_INFO* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
            wsData += L"\n\tProperty Names:";
            for (unsigned long ulCount = 0; ulCount < ulData; ++ulCount) {
                wsData.append(L"\n\t\t");
                wsData.append(
                    reinterpret_cast<wchar_t*>(pByteInfo + pTraceInfo->EventPropertyInfoArray[ulCount].NameOffset));
                wsData.append(L": ");
                wsData.append(buildEventPropertyString(ulCount));
            }
        }
    }

    //
    // swap and return
    //
    reusable_string.swap(wsData);
}
inline void
ctEtwRecord::writeFormattedMessage(std::wstring& reusable_string, bool include_message_properties) const
{
    //
    // write to a temp string - but use the caller's buffer
    //
    std::wstring wsData;
    wsData.swap(reusable_string);
    wsData.clear();

    ULONG ulData;
    if (queryTopLevelPropertyCount(&ulData) && ulData > 0) {
        BYTE* const pByteInfo = m_traceEventInfo.get();
        TRACE_EVENT_INFO* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());

        std::wstring wsProperties;
        std::vector<std::wstring> wsPropertyVector;
        for (unsigned long ulCount = 0; ulCount < ulData; ++ulCount) {
            wsProperties.append(L"\n[");
            wsProperties.append(
                reinterpret_cast<wchar_t*>(pByteInfo + pTraceInfo->EventPropertyInfoArray[ulCount].NameOffset));
            wsProperties.append(L"] ");

            // use the mapped string if it's available
            if (m_traceMapping[ulCount].first) {
                wsProperties.append(m_traceMapping[ulCount].first.get());
                wsPropertyVector.emplace_back(m_traceMapping[ulCount].first.get());
            } else {
                const std::wstring wsPropertyValue = buildEventPropertyString(ulCount);
                wsProperties.append(wsPropertyValue);
                wsPropertyVector.push_back(wsPropertyValue);
            }
        }
        // need an array of wchar_t* to pass to FormatMessage
        std::vector<LPWSTR> messageArguments;
        for (auto& wsProperty : wsPropertyVector) {
            messageArguments.push_back(wsProperty.data());
        }

        wsData.assign(L"Event Message: ");
        std::wstring wsEventMessage;
        if (queryEventMessage(wsEventMessage)) {
            WCHAR* formattedMessage;
            if (0 != FormatMessageW(
                         FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                         wsEventMessage.c_str(),
                         0,
                         0,
                         reinterpret_cast<LPWSTR>(&formattedMessage), // will be allocated from LocalAlloc
                         0,
                         reinterpret_cast<va_list*>(messageArguments.data()))) {
                const auto free_message = wil::scope_exit([&] { LocalFree(formattedMessage); });
                UNREFERENCED_PARAMETER(free_message); // will not dismiss it - it will always free
                wsData.append(formattedMessage);
            } else {
                wsData.append(wsEventMessage);
            }
        }
        if (include_message_properties) {
            wsData.append(L"\nEvent Message Properties:");
            wsData.append(wsProperties);
        }
    } else {
        wsData.clear();
    }

    //
    // swap and return
    //
    reusable_string.swap(wsData);
}

inline std::map<std::wstring, std::wstring>
ctEtwRecord::writeMessageProperties() const
{
    std::map<std::wstring, std::wstring> wsProperties;

    ULONG ulData;
    if (queryTopLevelPropertyCount(&ulData) && ulData > 0) {
        BYTE* const pByteInfo = m_traceEventInfo.get();
        TRACE_EVENT_INFO* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());

        for (unsigned long ulCount = 0; ulCount < ulData; ++ulCount) {
            const std::wstring key =
                reinterpret_cast<wchar_t*>(pByteInfo + pTraceInfo->EventPropertyInfoArray[ulCount].NameOffset);

            // use the mapped string if it's available
            std::wstring value;
            if (m_traceMapping[ulCount].first) {
                value = m_traceMapping[ulCount].first.get();
            } else {
                value = buildEventPropertyString(ulCount);
            }

            wsProperties[key] = value;
        }
    }

    return wsProperties;
}

////////////////////////////////////////////////////////////////////////////////
//
//  Comparison operators
//
////////////////////////////////////////////////////////////////////////////////
inline bool
ctEtwRecord::operator==(_In_ const ctEtwRecord& inEvent) const
{
    if (0 != memcmp(&m_eventHeader, &inEvent.m_eventHeader, sizeof(EVENT_HEADER))) {
        return false;
    }
    if (0 != memcmp(&m_etwBufferContext, &inEvent.m_etwBufferContext, sizeof(ETW_BUFFER_CONTEXT))) {
        return false;
    }
    if (m_initialized != inEvent.m_initialized) {
        return false;
    }
    //
    // a deep comparison of the v_eventHeaderExtendedData member
    //
    // can't just do a byte comparison of the structs since the DataPtr member is a raw ptr value
    // - and can be different raw buffers with the same event
    //
    if (m_eventHeaderExtendedData.size() != inEvent.m_eventHeaderExtendedData.size()) {
        return false;
    }
    std::vector<EVENT_HEADER_EXTENDED_DATA_ITEM>::const_iterator thisDataIterator = m_eventHeaderExtendedData.begin();
    const std::vector<EVENT_HEADER_EXTENDED_DATA_ITEM>::const_iterator thisDataEnd = m_eventHeaderExtendedData.end();

    std::vector<EVENT_HEADER_EXTENDED_DATA_ITEM>::const_iterator inEventDataIterator =
        inEvent.m_eventHeaderExtendedData.begin();
    const std::vector<EVENT_HEADER_EXTENDED_DATA_ITEM>::const_iterator inEventDataEnd =
        inEvent.m_eventHeaderExtendedData.end();

    for (; thisDataIterator != thisDataEnd && inEventDataIterator != inEventDataEnd;
         ++thisDataIterator, ++inEventDataIterator) {
        if (thisDataIterator->ExtType != inEventDataIterator->ExtType) {
            return false;
        }
        if (thisDataIterator->DataSize != inEventDataIterator->DataSize) {
            return false;
        }
        if (0 != memcmp(
                     reinterpret_cast<VOID*>(thisDataIterator->DataPtr),
                     reinterpret_cast<VOID*>(inEventDataIterator->DataPtr),
                     thisDataIterator->DataSize)) {
            return false;
        }
    }
    //
    // a deep comparison of the m_traceEventInfo member
    //
    if (m_cbTraceEventInfo != inEvent.m_cbTraceEventInfo) {
        return false;
    }
    if (0 != memcmp(m_traceEventInfo.get(), inEvent.m_traceEventInfo.get(), m_cbTraceEventInfo)) {
        return false;
    }

    return true;
}

inline bool
ctEtwRecord::operator!=(_In_ const ctEtwRecord& inEvent) const
{
    return !operator==(inEvent);
}

////////////////////////////////////////////////////////////////////////////////
//
//  Accessors for EVENT_HEADER properties
//
//  - retrieved from the member variable
//    EVENT_HEADER eventHeader;
//
////////////////////////////////////////////////////////////////////////////////
inline ULONG
ctEtwRecord::getThreadId() const noexcept
{
    return m_eventHeader.ThreadId;
}
inline ULONG
ctEtwRecord::getProcessId() const noexcept
{
    return m_eventHeader.ProcessId;
}
inline LARGE_INTEGER
ctEtwRecord::getTimeStamp() const noexcept
{
    return m_eventHeader.TimeStamp;
}
inline GUID
ctEtwRecord::getProviderId() const noexcept
{
    return m_eventHeader.ProviderId;
}
inline GUID
ctEtwRecord::getActivityId() const noexcept
{
    return m_eventHeader.ActivityId;
}
inline _Success_(return) bool ctEtwRecord::queryKernelTime(_Out_ ULONG* pout_Time) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_PRIVATE_SESSION || m_eventHeader.Flags & EVENT_HEADER_FLAG_NO_CPUTIME) {
        return false;
    } else {
        *pout_Time = m_eventHeader.KernelTime;
        return true;
    }
}
inline _Success_(return) bool ctEtwRecord::queryUserTime(_Out_ ULONG* pout_Time) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_PRIVATE_SESSION || m_eventHeader.Flags & EVENT_HEADER_FLAG_NO_CPUTIME) {
        return false;
    } else {
        *pout_Time = m_eventHeader.UserTime;
        return true;
    }
}
inline ULONG64
ctEtwRecord::getProcessorTime() const noexcept
{
    return m_eventHeader.ProcessorTime;
}
////////////////////////////////////////////////////////////////////////////////
//
//  Accessors for EVENT_DESCRIPTOR properties
//
//  - retrieved from the member variable
//    EVENT_HEADER eventHeader.EventDescriptor;
//
////////////////////////////////////////////////////////////////////////////////
inline USHORT
ctEtwRecord::getEventId() const noexcept
{
    return m_eventHeader.EventDescriptor.Id;
}
inline UCHAR
ctEtwRecord::getVersion() const noexcept
{
    return m_eventHeader.EventDescriptor.Version;
}
inline UCHAR
ctEtwRecord::getChannel() const noexcept
{
    return m_eventHeader.EventDescriptor.Channel;
}
inline UCHAR
ctEtwRecord::getLevel() const noexcept
{
    return m_eventHeader.EventDescriptor.Level;
}
inline UCHAR
ctEtwRecord::getOpcode() const noexcept
{
    return m_eventHeader.EventDescriptor.Opcode;
}
inline USHORT
ctEtwRecord::getTask() const noexcept
{
    return m_eventHeader.EventDescriptor.Task;
}
inline ULONGLONG
ctEtwRecord::getKeyword() const noexcept
{
    return m_eventHeader.EventDescriptor.Keyword;
}
////////////////////////////////////////////////////////////////////////////////
//
//  Accessors for ETW_BUFFER_CONTEXT properties
//
//  - retrieved from the member variable
//    ETW_BUFFER_CONTEXT etwBufferContext;
//
////////////////////////////////////////////////////////////////////////////////
inline UCHAR
ctEtwRecord::getProcessorNumber() const noexcept
{
    return m_etwBufferContext.ProcessorNumber;
}
inline UCHAR
ctEtwRecord::getAlignment() const noexcept
{
    return m_etwBufferContext.Alignment;
}
inline USHORT
ctEtwRecord::getLoggerId() const noexcept
{
    return m_etwBufferContext.LoggerId;
}
////////////////////////////////////////////////////////////////////////////////
//
//  Accessors for EVENT_HEADER_EXTENDED_DATA_ITEM properties
//
//  - retrieved from the member variable
//    std::vector<EVENT_HEADER_EXTENDED_DATA_ITEM> v_eventHeaderExtendedData;
//
//  - required to walk the std::vector to determine if the asked-for property
//    is in any of the data items stored.
//
////////////////////////////////////////////////////////////////////////////////
inline _Success_(return) bool ctEtwRecord::queryRelatedActivityId(_Out_ GUID* pout_GUID) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    bool bFoundProperty = false;
    for (const auto& tempItem : m_eventHeaderExtendedData) {
        if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_RELATED_ACTIVITYID) {
            assert(tempItem.DataSize == sizeof(EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID));
            EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID* const relatedID =
                reinterpret_cast<EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID*>(tempItem.DataPtr);
            *pout_GUID = relatedID->RelatedActivityId;
            bFoundProperty = true;
            break;
        }
    }

    return bFoundProperty;
}
inline _Success_(return) bool ctEtwRecord::querySID(
    _Out_ std::shared_ptr<BYTE[]>& out_pSID, _Out_ size_t* pout_cbSize) const
{
    if (!m_initialized) {
        return false;
    }

    bool bFoundProperty = false;
    for (const auto& tempItem : m_eventHeaderExtendedData) {
        if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_SID) {
            SID* const p_temp_SID = reinterpret_cast<SID*>(tempItem.DataPtr);
            out_pSID.reset(new BYTE[tempItem.DataSize]);
            *pout_cbSize = tempItem.DataSize;
            memcpy_s(out_pSID.get(), tempItem.DataSize, p_temp_SID, *pout_cbSize);
            bFoundProperty = true;
            break;
        }
    }

    return bFoundProperty;
}
inline _Success_(return) bool ctEtwRecord::queryTerminalSessionId(_Out_ ULONG* pout_ID) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    bool bFoundProperty = false;
    for (const auto& tempItem : m_eventHeaderExtendedData) {
        if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_TS_ID) {
            assert(tempItem.DataSize == sizeof(EVENT_EXTENDED_ITEM_TS_ID));
            EVENT_EXTENDED_ITEM_TS_ID* const ts_ID = reinterpret_cast<EVENT_EXTENDED_ITEM_TS_ID*>(tempItem.DataPtr);
            *pout_ID = ts_ID->SessionId;
            bFoundProperty = true;
            break;
        }
    }

    return bFoundProperty;
}
inline _Success_(return) bool ctEtwRecord::queryTransactionInstanceId(_Out_ ULONG* pout_ID) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    bool bFoundProperty = false;
    for (const auto& tempItem : m_eventHeaderExtendedData) {
        if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_INSTANCE_INFO) {
            assert(tempItem.DataSize == sizeof(EVENT_EXTENDED_ITEM_INSTANCE));
            EVENT_EXTENDED_ITEM_INSTANCE* const instanceInfo =
                reinterpret_cast<EVENT_EXTENDED_ITEM_INSTANCE*>(tempItem.DataPtr);
            *pout_ID = instanceInfo->InstanceId;
            bFoundProperty = true;
            break;
        }
    }

    return bFoundProperty;
}
inline _Success_(return) bool ctEtwRecord::queryTransactionParentInstanceId(_Out_ ULONG* pout_ID) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    bool bFoundProperty = false;
    for (const auto& tempItem : m_eventHeaderExtendedData) {
        if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_INSTANCE_INFO) {
            assert(tempItem.DataSize == sizeof(EVENT_EXTENDED_ITEM_INSTANCE));
            EVENT_EXTENDED_ITEM_INSTANCE* const instanceInfo =
                reinterpret_cast<EVENT_EXTENDED_ITEM_INSTANCE*>(tempItem.DataPtr);
            *pout_ID = instanceInfo->ParentInstanceId;
            bFoundProperty = true;
            break;
        }
    }

    return bFoundProperty;
}
inline _Success_(return) bool ctEtwRecord::queryTransactionParentGuid(_Out_ GUID* pout_GUID) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    bool bFoundProperty = false;
    for (const auto& tempItem : m_eventHeaderExtendedData) {
        if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_INSTANCE_INFO) {
            assert(tempItem.DataSize == sizeof(EVENT_EXTENDED_ITEM_INSTANCE));
            EVENT_EXTENDED_ITEM_INSTANCE* const instanceInfo =
                reinterpret_cast<EVENT_EXTENDED_ITEM_INSTANCE*>(tempItem.DataPtr);
            *pout_GUID = instanceInfo->ParentGuid;
            bFoundProperty = true;
            break;
        }
    }

    return bFoundProperty;
}
////////////////////////////////////////////////////////////////////////////////
//
//  Accessors for TRACE_EVENT_INFO properties
//
//  - only valid if the EVENT_HEADER_FLAG_STRING_ONLY flag is not set in
//    the parent EVENT_HEADER struct.
//
////////////////////////////////////////////////////////////////////////////////
inline _Success_(return) bool ctEtwRecord::queryProviderGuid(_Out_ GUID* pout_GUID) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    *pout_GUID = pTraceInfo->ProviderGuid;
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryDecodingSource(_Out_ DECODING_SOURCE* pout_SOURCE) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    *pout_SOURCE = pTraceInfo->DecodingSource;
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryProviderName(_Out_ std::wstring& out_wsName) const
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    if (0 == pTraceInfo->ProviderNameOffset) {
        return false;
    }

    wchar_t* const szProviderName =
        reinterpret_cast<wchar_t*>(m_traceEventInfo.get() + pTraceInfo->ProviderNameOffset);
    out_wsName.assign(szProviderName);
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryLevelName(_Out_ std::wstring& out_wsName) const
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    if (0 == pTraceInfo->LevelNameOffset) {
        return false;
    }

    wchar_t* const szLevelName = reinterpret_cast<wchar_t*>(m_traceEventInfo.get() + pTraceInfo->LevelNameOffset);
    out_wsName.assign(szLevelName);
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryChannelName(_Out_ std::wstring& out_wsName) const
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    if (0 == pTraceInfo->ChannelNameOffset) {
        return false;
    }

    wchar_t* const szChannelName =
        reinterpret_cast<wchar_t*>(m_traceEventInfo.get() + pTraceInfo->ChannelNameOffset);
    out_wsName.assign(szChannelName);
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryKeywords(_Out_ std::vector<std::wstring>& out_vKeywords) const
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    if (0 == pTraceInfo->KeywordsNameOffset) {
        return false;
    }

    wchar_t* szKeyName = reinterpret_cast<wchar_t*>(m_traceEventInfo.get() + pTraceInfo->KeywordsNameOffset);
    std::vector<std::wstring> vTemp;
    std::wstring wsTemp;
    while (*szKeyName != L'\0') {
        const size_t cchKeySize = wcslen(szKeyName) + 1;
        wsTemp.assign(szKeyName);
        vTemp.push_back(wsTemp);
        szKeyName += cchKeySize;
    }
    vTemp.swap(out_vKeywords);
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryTaskName(_Out_ std::wstring& out_wsName) const
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    if (0 == pTraceInfo->TaskNameOffset) {
        return false;
    }

    wchar_t* const szTaskName = reinterpret_cast<wchar_t*>(m_traceEventInfo.get() + pTraceInfo->TaskNameOffset);
    out_wsName.assign(szTaskName);
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryOpcodeName(_Out_ std::wstring& out_wsName) const
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    if (0 == pTraceInfo->OpcodeNameOffset) {
        return false;
    }

    wchar_t* const szOpcodeName =
        reinterpret_cast<wchar_t*>(m_traceEventInfo.get() + pTraceInfo->OpcodeNameOffset);
    out_wsName.assign(szOpcodeName);
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryEventMessage(_Out_ std::wstring& out_wsName) const
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    if (0 == pTraceInfo->EventMessageOffset) {
        return false;
    }

    wchar_t* const szEventMessage =
        reinterpret_cast<wchar_t*>(m_traceEventInfo.get() + pTraceInfo->EventMessageOffset);
    out_wsName.assign(szEventMessage);
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryProviderMessageName(_Out_ std::wstring& out_wsName) const
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    if (0 == pTraceInfo->ProviderMessageOffset) {
        return false;
    }

    wchar_t* const szProviderMessageName =
        reinterpret_cast<wchar_t*>(m_traceEventInfo.get() + pTraceInfo->ProviderMessageOffset);
    out_wsName.assign(szProviderMessageName);
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryPropertyCount(_Out_ ULONG* pout_Properties) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    *pout_Properties = pTraceInfo->PropertyCount;
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryTopLevelPropertyCount(
    _Out_ ULONG* pout_TopLevelProperties) const noexcept
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY || !m_traceEventInfo) {
        return false;
    }

    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    *pout_TopLevelProperties = pTraceInfo->TopLevelPropertyCount;
    return true;
}
inline _Success_(return) bool ctEtwRecord::queryEventPropertyStringValue(
    _Out_ std::wstring& out_wsUserEventString) const
{
    if (!m_initialized) {
        return false;
    }

    if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
        // per the flags, the byte array is a null-terminated string
        out_wsUserEventString.assign(reinterpret_cast<wchar_t*>(m_traceEventInfo.get()));
        return true;
    }
    return false;
}

inline _Success_(return) bool ctEtwRecord::queryEventPropertyName(
    _In_ const unsigned long ulIndex, _Out_ std::wstring& out_wsPropertyName) const
{
    // immediately fail if no top level property count value or the value is 0
    unsigned long ulData = 0;
    if (!queryTopLevelPropertyCount(&ulData) || 0 == ulData) {
        out_wsPropertyName.clear();
        return false;
    }
    if (ulIndex >= ulData) {
        out_wsPropertyName.clear();
        return false;
    }

    BYTE* const pByteInfo = m_traceEventInfo.get();
    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    const auto* const szPropertyFound =
        reinterpret_cast<wchar_t*>(pByteInfo + pTraceInfo->EventPropertyInfoArray[ulIndex].NameOffset);
    out_wsPropertyName.assign(szPropertyFound);

    return true;
}

inline _Success_(return) bool ctEtwRecord::queryEventProperty(
    _In_ PCWSTR szPropertyName, _Out_ std::wstring& out_wsPropertyValue) const
{
    // immediately fail if no top level property count value or the value is 0
    unsigned long ulData = 0;

    if (!queryTopLevelPropertyCount(&ulData) || 0 == ulData) {
        out_wsPropertyValue.clear();
        return false;
    }
    //
    // iterate through each property name looking for a match
    BYTE* const pByteInfo = m_traceEventInfo.get();
    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    for (unsigned long ulCount = 0; ulCount < ulData; ++ulCount) {
        const auto* const szPropertyFound =
            reinterpret_cast<const wchar_t*>(pByteInfo + pTraceInfo->EventPropertyInfoArray[ulCount].NameOffset);
        if (0 == _wcsicmp(szPropertyName, szPropertyFound)) {
            out_wsPropertyValue.assign(buildEventPropertyString(ulCount));
            return true;
        }
    }
    out_wsPropertyValue.clear();
    return false;
}
inline _Success_(return) bool ctEtwRecord::queryEventProperty(
    _In_ const unsigned long ulIndex, _Out_ std::wstring& out_wsPropertyValue) const
{
    // immediately fail if no top level property count value or the value is 0 or ulIndex is larger than
    // total number of properties
    unsigned long ulData = 0;

    if (!queryTopLevelPropertyCount(&ulData) || 0 == ulData || 0 == ulIndex || ulIndex > ulData) {
        out_wsPropertyValue.clear();
        return false;
    }
    //
    // get the property value
    BYTE* const pByteInfo = m_traceEventInfo.get();
    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    const bool bFoundMatch = nullptr != reinterpret_cast<const wchar_t*>(
                                        pByteInfo + pTraceInfo->EventPropertyInfoArray[ulIndex - 1].NameOffset);
    if (bFoundMatch) {
        out_wsPropertyValue.assign(buildEventPropertyString(ulIndex - 1));
    } else {
        out_wsPropertyValue.clear();
    }
    return bFoundMatch;
}
inline _Success_(return) bool ctEtwRecord::queryEventProperty(
    _In_ PCWSTR szPropertyName, _Out_ ctPropertyPair& out_eventPair) const
{
    //
    // immediately fail if no top level property count value or the value is 0
    unsigned long ulData = 0;
    if (!queryTopLevelPropertyCount(&ulData) || 0 == ulData) {
        return false;
    }
    //
    // iterate through each property name looking for a match
    bool bFoundMatch = false;
    BYTE* const pByteInfo = m_traceEventInfo.get();
    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());

    for (unsigned long ulCount = 0; !bFoundMatch && ulCount < ulData; ++ulCount) {
        const auto* const szPropertyFound =
            reinterpret_cast<wchar_t*>(pByteInfo + pTraceInfo->EventPropertyInfoArray[ulCount].NameOffset);
        if (0 == _wcsicmp(szPropertyName, szPropertyFound)) {
            assert(ulCount < m_traceProperties.size());
            if (ulCount < m_traceProperties.size()) {
                out_eventPair = m_traceProperties[ulCount];
                bFoundMatch = true;
            } else {
                // TODO: conditional break here
                // something is messed up - the properties found didn't match the # of property values
                // break and exit now
                break;
            }
        }
    }
    return bFoundMatch;
}

inline std::wstring
ctEtwRecord::buildEventPropertyString(ULONG ulProperty) const
{
    //
    // immediately fail if no top level property count value or the value asked for is out of range
    unsigned long ulData = 0;
    if (!queryTopLevelPropertyCount(&ulData) || ulProperty >= ulData) {
        throw std::runtime_error("ctEtwRecord - ETW Property value requested is out of range");
    }

    constexpr unsigned cch_StackBuffer = 100;
    wchar_t stackBuffer[cch_StackBuffer]{};
    std::wstring wsData;

    // retrieve the raw property information
    const auto* const pTraceInfo = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfo.get());
    USHORT propertyOutType = pTraceInfo->EventPropertyInfoArray[ulProperty].nonStructType.OutType;
    const ULONG propertySize = m_traceProperties[ulProperty].second;
    const BYTE* const propertyBuf = m_traceProperties[ulProperty].first.get();

    // build a string only if the property data > 0 bytes
    if (propertySize > 0) {
        // build the string based on the IN and OUT types
        switch (pTraceInfo->EventPropertyInfoArray[ulProperty].nonStructType.InType) {
        case TDH_INTYPE_NULL: {
            wsData = L"null";
            break;
        }

        case TDH_INTYPE_UNICODESTRING: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_STRING;
            }
            // xs:string
            assert(propertyOutType == TDH_OUTTYPE_STRING);
            // - not guaranteed to be NULL terminated
            const auto* const wszBuffer = reinterpret_cast<const wchar_t*>(propertyBuf);
            const auto* wszBufferEnd = wszBuffer + propertySize / 2;
            // don't assign over the final NULL terminator (will embed the null in the std::wstring)
            while (wszBuffer < wszBufferEnd && L'\0' == *(wszBufferEnd - 1)) {
                --wszBufferEnd;
            }
            wsData.assign(wszBuffer, wszBufferEnd);
            break;
        }

        case TDH_INTYPE_ANSISTRING: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_STRING;
            }
            // xs:string
            assert(propertyOutType == TDH_OUTTYPE_STRING);
            // - not guaranteed to be NULL terminated
            const auto* const szBuffer = reinterpret_cast<const char*>(propertyBuf);
            const auto* szBufferEnd = szBuffer + propertySize;
            // don't assign over the final NULL terminator (will embed the null in the std::wstring)
            while (szBuffer < szBufferEnd && L'\0' == *(szBufferEnd - 1)) {
                --szBufferEnd;
            }
            const std::string sData(szBuffer, szBufferEnd);
            // convert to wide
            int iResult = ::MultiByteToWideChar(CP_ACP, 0, sData.c_str(), -1, nullptr, 0);
            if (iResult != 0) {
                std::vector<wchar_t> conversion(iResult, L'\0');
                iResult = ::MultiByteToWideChar(CP_ACP, 0, sData.c_str(), -1, conversion.data(), iResult);
                if (iResult != 0) {
                    wsData = conversion.data();
                }
            }
            break;
        }

        case TDH_INTYPE_INT8: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_BYTE;
            }
            // xs:byte
            assert(1 == propertySize);
            const char prop = *reinterpret_cast<const char*>(propertyBuf);
            assert(propertyOutType == TDH_OUTTYPE_BYTE);
            _itow_s(prop, stackBuffer, 10);
            wsData = stackBuffer;
            break;
        }

        case TDH_INTYPE_UINT8: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_UNSIGNEDBYTE;
            }
            // xs:unsignedByte; win:hexInt8
            assert(1 == propertySize);
            const unsigned char prop = *reinterpret_cast<const unsigned char*>(propertyBuf);
            if (TDH_OUTTYPE_UNSIGNEDBYTE == propertyOutType) {
                _itow_s(prop, stackBuffer, 10);
                wsData = stackBuffer;
            } else if (TDH_OUTTYPE_HEXINT8 == propertyOutType) {
                _itow_s(prop, stackBuffer, 16);
                wsData = L"0x";
                wsData += stackBuffer;
            } else if (TDH_OUTTYPE_BOOLEAN == propertyOutType) {
                if (prop == 0) {
                    wsData = L"false";
                } else {
                    wsData = L"true";
                }
            } else {
                assert(!"Unknown OUT type for TDH_INTYPE_UINT8" && propertyOutType);
            }
            break;
        }

        case TDH_INTYPE_INT16: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_SHORT;
            }
            // xs:short
            assert(2 == propertySize);
            const short prop = *reinterpret_cast<const short*>(propertyBuf);
            assert(propertyOutType == TDH_OUTTYPE_SHORT);
            _itow_s(prop, stackBuffer, 10);
            wsData = stackBuffer;
            break;
        }

        case TDH_INTYPE_UINT16: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_UNSIGNEDSHORT;
            }
            // xs:unsignedShort; win:Port; win:HexInt16
            assert(2 == propertySize);
            const unsigned short prop = *reinterpret_cast<const unsigned short*>(propertyBuf);
            if (TDH_OUTTYPE_UNSIGNEDSHORT == propertyOutType) {
                _itow_s(prop, stackBuffer, 10);
                wsData = stackBuffer;
            } else if (TDH_OUTTYPE_PORT == propertyOutType) {
                _itow_s(::ntohs(prop), stackBuffer, 10);
                wsData = stackBuffer;
            } else if (TDH_OUTTYPE_HEXINT16 == propertyOutType) {
                _itow_s(prop, stackBuffer, 16);
                wsData = L"0x";
                wsData += stackBuffer;
            } else {
                assert(!"Unknown OUT type for TDH_INTYPE_UINT16" && propertyOutType);
            }
            break;
        }

        case TDH_INTYPE_INT32: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_INT;
            }
            // xs:int
            assert(4 == propertySize);
            const int prop = *reinterpret_cast<const int*>(propertyBuf);
            assert(propertyOutType == TDH_OUTTYPE_INT);
            _itow_s(prop, stackBuffer, 10);
            wsData = stackBuffer;
            break;
        }

        case TDH_INTYPE_UINT32: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_UNSIGNEDINT;
            }
            // xs:unsignedInt, win:PID, win:TID, win:IPv4, win:ETWTIME, win:ErrorCode, win:HexInt32
            assert(4 == propertySize);
            const unsigned int prop = *reinterpret_cast<const unsigned int*>(propertyBuf);
            if (TDH_OUTTYPE_UNSIGNEDINT == propertyOutType || TDH_OUTTYPE_UNSIGNEDLONG == propertyOutType ||
                TDH_OUTTYPE_PID == propertyOutType || TDH_OUTTYPE_TID == propertyOutType ||
                TDH_OUTTYPE_ETWTIME == propertyOutType) {
                // display as an unsigned int
                _ultow_s(prop, stackBuffer, 10);
                wsData = stackBuffer;
            } else if (TDH_OUTTYPE_IPV4 == propertyOutType) {
                // display as a v4 address
                ::RtlIpv4AddressToString(reinterpret_cast<const IN_ADDR*>(propertyBuf), stackBuffer);
                wsData += stackBuffer;
            } else if (
                TDH_OUTTYPE_HEXINT32 == propertyOutType || TDH_OUTTYPE_ERRORCODE == propertyOutType ||
                TDH_OUTTYPE_WIN32ERROR == propertyOutType || TDH_OUTTYPE_NTSTATUS == propertyOutType ||
                TDH_OUTTYPE_HRESULT == propertyOutType) {
                // display as a hex value
                _ultow_s(prop, stackBuffer, 16);
                wsData = L"0x";
                wsData += stackBuffer;
            } else {
                FAIL_FAST_MSG("Unknown TDH_OUTTYPE [%u] for the TDH_INTYPE_UINT32 value [%u]", propertyOutType, prop);
            }
            break;
        }

        case TDH_INTYPE_INT64: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_LONG;
            }
            // xs:long
            assert(8 == propertySize);
            const INT64 prop = *reinterpret_cast<const INT64*>(propertyBuf);
            assert(propertyOutType == TDH_OUTTYPE_LONG);
            _i64tow_s(prop, stackBuffer, cch_StackBuffer, 10);
            wsData = stackBuffer;
            break;
        }

        case TDH_INTYPE_UINT64: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_UNSIGNEDLONG;
            }
            // xs:unsignedLong, win:HexInt64
            assert(8 == propertySize);
            const UINT64 prop = *reinterpret_cast<const UINT64*>(propertyBuf);
            if (TDH_OUTTYPE_UNSIGNEDLONG == propertyOutType) {
                _ui64tow_s(prop, stackBuffer, cch_StackBuffer, 10);
                wsData = stackBuffer;
            } else if (TDH_OUTTYPE_HEXINT64 == propertyOutType) {
                _ui64tow_s(prop, stackBuffer, cch_StackBuffer, 16);
                wsData = L"0x";
                wsData += stackBuffer;
            } else {
                assert(!"Unknown OUT type for TDH_INTYPE_UINT64" && propertyOutType);
            }
            break;
        }

        case TDH_INTYPE_FLOAT: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_FLOAT;
            }
            // xs:float
            const float prop = *reinterpret_cast<const float*>(propertyBuf);
            assert(propertyOutType == TDH_OUTTYPE_FLOAT);
            swprintf_s(stackBuffer, cch_StackBuffer, L"%f", prop);
            wsData += stackBuffer;
            break;
        }

        case TDH_INTYPE_DOUBLE: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_DOUBLE;
            }
            // xs:double
            const double prop = *reinterpret_cast<const double*>(propertyBuf);
            assert(propertyOutType == TDH_OUTTYPE_DOUBLE);
            swprintf_s(stackBuffer, cch_StackBuffer, L"%f", prop);
            wsData += stackBuffer;
            break;
        }

        case TDH_INTYPE_BOOLEAN: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_BOOLEAN;
            }
            // xs:boolean
            assert(propertyOutType == TDH_OUTTYPE_BOOLEAN);
            const int prop = *reinterpret_cast<const int*>(propertyBuf);
            if (0 == prop) {
                wsData = L"false";
            } else {
                wsData = L"true";
            }
            break;
        }

        case TDH_INTYPE_BINARY: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_HEXBINARY;
            }
            // xs:hexBinary, win:IPv6 (16 bytes), win:SocketAddress
            if (TDH_OUTTYPE_HEXBINARY == propertyOutType) {
                wsData = L'[';
                const BYTE* const buffer = propertyBuf;
                for (unsigned long ulBits = 0; ulBits < propertySize; ++ulBits) {
                    const unsigned char chData = buffer[ulBits];
                    _itow_s(chData, stackBuffer, 16);
                    wsData += stackBuffer;
                }
                wsData += L']';
            } else if (TDH_OUTTYPE_IPV6 == propertyOutType) {
                ::RtlIpv6AddressToString(reinterpret_cast<const IN6_ADDR*>(propertyBuf), stackBuffer);
                wsData += stackBuffer;
            } else if (TDH_OUTTYPE_SOCKETADDRESS == propertyOutType) {
                DWORD dwSize = cch_StackBuffer;
                // Winsock APIs are not const-correct
                const int iReturn = ::WSAAddressToString(
                    reinterpret_cast<sockaddr*>(const_cast<BYTE*>(propertyBuf)),
                    propertySize,
                    nullptr,
                    stackBuffer,
                    &dwSize);
                if (0 == iReturn) {
                    wsData = stackBuffer;
                }
            } else {
                assert(!"Unknown OUT type for TDH_INTYPE_BINARY" && propertyOutType);
            }
            break;
        }

        case TDH_INTYPE_GUID: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_GUID;
            }
            // xs:GUID
            assert(TDH_OUTTYPE_GUID == propertyOutType);
            assert(sizeof(GUID) == propertySize);
            if (sizeof(GUID) == propertySize) {
                RPC_WSTR pszGuid = nullptr;
                const RPC_STATUS uuidStatus = ::UuidToString(reinterpret_cast<const GUID*>(propertyBuf), &pszGuid);
                if (RPC_S_OK == uuidStatus) {
                    wsData = reinterpret_cast<LPWSTR>(pszGuid);
                    ::RpcStringFree(&pszGuid);
                }
            }
            break;
        }

        case TDH_INTYPE_POINTER: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_HEXINT64;
            }
            // win:hexInt64
            if (4 == propertySize) {
                assert(TDH_OUTTYPE_HEXINT64 == propertyOutType);
                const unsigned long prop = *reinterpret_cast<const unsigned long*>(propertyBuf);
                _ultow_s(prop, stackBuffer, 16);
                wsData = L"0x";
                wsData += stackBuffer;
            } else if (8 == propertySize) {
                assert(TDH_OUTTYPE_HEXINT64 == propertyOutType);
                const UINT64 prop = *reinterpret_cast<const UINT64*>(propertyBuf);
                _ui64tow_s(prop, stackBuffer, cch_StackBuffer, 16);
                wsData = L"0x";
                wsData += stackBuffer;
            } else {
                wprintf(L"TDH_INTYPE_POINTER was called with a %d -size value\n", propertySize);
            }
            break;
        }

        case TDH_INTYPE_FILETIME: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_DATETIME;
            }
            // xs:dateTime
            assert(sizeof(FILETIME) == propertySize);
            if (sizeof(FILETIME) == propertySize) {
                const FILETIME ft = *reinterpret_cast<const FILETIME*>(propertyBuf);
                LARGE_INTEGER li;
                li.LowPart = ft.dwLowDateTime;
                li.HighPart = static_cast<LONG>(ft.dwHighDateTime);
                _ui64tow_s(li.QuadPart, stackBuffer, cch_StackBuffer, 16);
                wsData = L"0x";
                wsData += stackBuffer;
            }
            break;
        }

        case TDH_INTYPE_SYSTEMTIME: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_DATETIME;
            }
            assert(sizeof(SYSTEMTIME) == propertySize);
            if (sizeof(SYSTEMTIME) == propertySize) {
                const SYSTEMTIME st = *reinterpret_cast<const SYSTEMTIME*>(propertyBuf);
                _snwprintf_s(
                    stackBuffer,
                    cch_StackBuffer,
                    99,
                    L"%d/%d/%d - %d:%d:%d::%d",
                    st.wYear,
                    st.wMonth,
                    st.wDay,
                    st.wHour,
                    st.wMinute,
                    st.wSecond,
                    st.wMilliseconds);
                wsData = stackBuffer;
            }
            break;
        }

        case TDH_INTYPE_SID: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_STRING;
            }
            //
            // first write out the raw binary
            wsData = L'[';
            const BYTE* const buffer = propertyBuf;
            for (unsigned long ulBits = 0; ulBits < propertySize; ++ulBits) {
                const char chData = static_cast<char>(buffer[ulBits]);
                _itow_s(chData, stackBuffer, 16);
                wsData += stackBuffer;
            }
            wsData += L']';
            //
            // now convert if we can to the friendly name
            // LookupAccountSid is not const correct
            SID* pSid = reinterpret_cast<SID*>(const_cast<BYTE*>(buffer));
            std::shared_ptr<wchar_t[]> szName;
            std::shared_ptr<wchar_t[]> szDomain;
            DWORD cchName = 0;
            DWORD cchDomain = 0;
            SID_NAME_USE sidNameUse;
            wchar_t temp[1];
            if (!::LookupAccountSid(nullptr, pSid, temp, &cchName, temp, &cchDomain, &sidNameUse)) {
                if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    szName.reset(new wchar_t[cchName]);
                    szDomain.reset(new wchar_t[cchDomain]);
                    if (::LookupAccountSid(
                            nullptr, pSid, szName.get(), &cchName, szDomain.get(), &cchDomain, &sidNameUse)) {
                        wsData += L"  ";
                        wsData += szDomain.get();
                        wsData += L"\\";
                        wsData += szName.get();
                    }
                }
            }
            break;
        }

        case TDH_INTYPE_HEXINT32: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_HEXINT32;
            }
            if (4 == propertySize) {
                assert(TDH_OUTTYPE_HEXINT32 == propertyOutType);
                const unsigned short prop = *reinterpret_cast<const unsigned short*>(propertyBuf);
                _itow_s(prop, stackBuffer, 10);
                wsData = stackBuffer;
            }
            break;
        }

        case TDH_INTYPE_HEXINT64: {
            if (propertyOutType == TDH_OUTTYPE_NULL) {
                propertyOutType = TDH_OUTTYPE_HEXINT64;
            }
            if (8 == propertySize) {
                assert(TDH_OUTTYPE_HEXINT64 == propertyOutType);
                UINT64 prop = *reinterpret_cast<const UINT64*>(propertyBuf);
                _ui64tow_s(prop, stackBuffer, cch_StackBuffer, 16);
                wsData = L"0x";
                wsData += stackBuffer;
            }
            break;
        }
        } // switch statement
    }
    return wsData;
}

} // namespace ctl

#pragma warning(pop)
