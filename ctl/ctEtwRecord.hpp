// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

// defining this so if we use Tdh types that are not currently supported
// it will break so we can add that support
#define TDH_FORMAT_FATAL_CONDITION

// CPP Headers
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
	namespace details {
		/** @brief Private methods to print various data types */
		std::wstring PrintHexBinary(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize);

		std::wstring PrintWcharString(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize);
		std::wstring PrintCharString(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize);

	    std::wstring Print64BitInteger(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize);
	    std::wstring Print32BitInteger(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize);
	    std::wstring Print16BitInteger(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize);
	    std::wstring Print8BitInteger(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize);

	}
	/**
	 * @class ctEtwRecord
	 * @brief Encapsulates accessing all the various properties one can potentially
	 *        gather from a EVENT_RECORD structure passed to the consumer from ETW.
	 *
	 * The constructor takes a ptr to the EVENT_RECORD to encapsulate, and makes
	 * a deep copy of all embedded and referenced data structures to access
	 * with the getter member functions.
	 *
	 * There are 2 method-types exposed: get*() and query*()
	 * - get* functions have no parameters, and always return the associated
	 *   value. They will always have a value to return from any event.
	 * - query* functions take applicable [out] as parameters, and return bool.
	 *   The values they retrieve are not guaranteed to be in all events,
	 *   and will return false if they don't exist in the encapsulated event.
	 *
	 * Note that all returned strings are dynamically allocated and returned via
	 * std::shared_ptr<wchar_t> smart pointer objects. Note that
	 * copying the object is very cheap (one Interlocked operation) and cannot
	 * fail. (thus isn't highly suggested to copy these smart pointer objects
	 * by value, not by reference - which defeats the internal ref-counting)
	 *
	 * All methods that do not have a throw() exception specification can throw
	 * std::bad_alloc - which is derived from std::exception. The constructor
	 * can throw ctException, which is also derived from std::exception.
	 */
	class ctEtwRecord
	{
	public:
		using ctPropertyPair = std::pair<std::shared_ptr<BYTE[]>, ULONG>;

		ctEtwRecord() noexcept = default;
		explicit ctEtwRecord(_In_ const EVENT_RECORD* event_record);
		~ctEtwRecord() noexcept = default;

		ctEtwRecord(const ctEtwRecord&) noexcept = default;
		ctEtwRecord&
			operator=(_In_ const ctEtwRecord&) noexcept = default;

		ctEtwRecord&
			operator=(_In_ const EVENT_RECORD* event_record);

		ctEtwRecord(ctEtwRecord&&) noexcept = default;
		ctEtwRecord&
			operator=(ctEtwRecord&&) noexcept = default;

		bool
			operator==(_In_ const ctEtwRecord& rhs) const noexcept;
		bool
			operator!=(_In_ const ctEtwRecord& rhs) const noexcept;

		/**
		 * @brief Implementing swap() to be a friendly container
		 * @param rhs The ctEtwRecord to swap with
		 */
		void
			swap(ctEtwRecord& rhs) noexcept;

		/**
		 * @brief Printing the entire ETW record
		 * @param reusable_string String buffer to use for output
		 */
		void
			writeRecord(std::wstring& reusable_string) const;

		/**
		 * @brief Printing the entire ETW record
		 * @return String containing the formatted record
		 */
		std::wstring
			writeRecord() const
		{
			std::wstring wsRecord;
			writeRecord(wsRecord);
			return wsRecord;
		}

		/**
		 * @brief Printing just the formatted event message
		 * @param reusable_string String buffer to use for output
		 * @param include_message_properties Whether to include full details of each property
		 */
		void
			writeFormattedMessage(std::wstring& reusable_string, bool include_message_properties) const;

		/**
		 * @brief Printing just the formatted event message
		 * @param include_message_properties Whether to include writing out each message property
		 *                                   along with the formatted message.
		 * @return String containing the formatted message
		 */
		std::wstring
			writeFormattedMessage(bool include_message_properties = false) const
		{
			std::wstring wsRecord;
			writeFormattedMessage(wsRecord, include_message_properties);
			return wsRecord;
		}

		/**
		 * @brief Get message properties as a map
		 * @return Map of property names to values
		 */
		std::map<std::wstring, std::wstring>
			writeMessageProperties() const;

		/**
		 * @name EVENT_HEADER field accessors (8)
		 * @{
		 */
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
		bool
			queryKernelTime(_Out_ ULONG*) const noexcept;
		bool
			queryUserTime(_Out_ ULONG*) const noexcept;
		ULONG64
			getProcessorTime() const noexcept;
		/** @} */

		/**
		 * @name EVENT_DESCRIPTOR field accessors (7)
		 * @{
		 */
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
		/** @} */

		/**
		 * @name ETW_BUFFER_CONTEXT field accessors (3)
		 * @{
		 */
		UCHAR
			getProcessorNumber() const noexcept;
		UCHAR
			getAlignment() const noexcept;
		USHORT
			getLoggerId() const noexcept;
		/** @} */

		/**
		 * @name EVENT_HEADER_EXTENDED_DATA_ITEM option accessors (6)
		 * @{
		 */
		bool
			queryRelatedActivityId(_Out_ GUID*) const noexcept;
		bool
			querySID(_Out_ std::vector<BYTE>&) const;
		bool
			queryTerminalSessionId(_Out_ ULONG*) const noexcept;
		bool
			queryTransactionInstanceId(_Out_ ULONG*) const noexcept;
		bool
			queryTransactionParentInstanceId(_Out_ ULONG*) const noexcept;
		bool
			queryTransactionParentGuid(_Out_ GUID*) const noexcept;
		/** @} */

		/**
		 * @name TRACE_EVENT_INFO option accessors (16)
		 * @{
		 */
		bool
			queryProviderGuid(_Out_ GUID*) const noexcept;
		bool
			queryDecodingSource(_Out_ DECODING_SOURCE*) const noexcept;
		bool
			queryProviderName(_Out_ std::wstring&) const;
		bool
			queryLevelName(_Out_ std::wstring&) const;
		bool
			queryChannelName(_Out_ std::wstring&) const;
		bool
			queryKeywords(_Out_ std::vector<std::wstring>&) const;
		bool
			queryTaskName(_Out_ std::wstring&) const;
		bool
			queryOpcodeName(_Out_ std::wstring&) const;
		bool
			queryEventMessage(_Out_ std::wstring&) const;
		bool
			queryProviderMessageName(_Out_ std::wstring&) const;
		bool
			queryPropertyCount(_Out_ ULONG*) const noexcept;
		bool
			queryTopLevelPropertyCount(_Out_ ULONG*) const noexcept;
		bool
			queryEventPropertyStringValue(_Out_ std::wstring&) const;
		bool
			queryEventPropertyName(_In_ ULONG index, _Out_ std::wstring& property_name) const;
		bool
			queryEventProperty(_In_ PCWSTR, _Out_ std::wstring&) const;
		bool
			queryEventProperty(_In_ PCWSTR, _Out_ ctPropertyPair&) const;
		bool
			queryEventProperty(_In_ ULONG, _Out_ std::wstring&) const;
		/** @} */

	private:
		/** @brief Private method to build a formatted string from the specified property offset */
		std::wstring BuildEventPropertyString(ULONG property_index) const;

		/** @brief eventHeader and etwBufferContext are shallow-copies
		 *         of the EVENT_HEADER and ETW_BUFFER_CONTEXT structs. */
		EVENT_HEADER m_eventHeader{};
		ETW_BUFFER_CONTEXT m_etwBufferContext{};

		/** @brief m_eventHeaderExtendedData and m_eventHeaderData stores a deep-copy
		 *         of the EVENT_HEADER_EXTENDED_DATA_ITEM struct. */
		std::vector<EVENT_HEADER_EXTENDED_DATA_ITEM> m_eventHeaderExtendedData;
		std::vector<std::shared_ptr<BYTE[]>> m_eventHeaderData;

		/** @brief m_traceEventInfo stores a deep copy of the TRACE_EVENT_INFO struct. */
		std::vector<BYTE> m_traceEventInfoBuffer;
		TRACE_EVENT_INFO* m_traceEventInfoPtr{ nullptr };

		/** @brief m_traceProperties stores an array of all properties */
		std::vector<ctPropertyPair> m_traceProperties;

		using ctMappingPair = std::pair<std::shared_ptr<WCHAR[]>, ULONG>;
		std::vector<ctMappingPair> m_traceMapping;

		/** @brief need to allow a default empty constructor, so must track initialization status */
		bool m_initialized{ false };
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

				const std::shared_ptr<BYTE[]> pTempBytes(new BYTE[pTempItem->DataSize]);
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
			const BYTE* pUserData = static_cast<const BYTE*>(event_record->UserData);
			m_traceEventInfoBuffer.assign(pUserData, pUserData + event_record->UserDataLength);
			m_traceEventInfoPtr = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfoBuffer.data());
		}
		else {
			ULONG trace_event_size = 0;
			ULONG tdhError =
				TdhGetEventInformation(const_cast<PEVENT_RECORD>(event_record), 0, nullptr, nullptr, &trace_event_size);
			if (ERROR_INSUFFICIENT_BUFFER == tdhError) {
				m_traceEventInfoBuffer.resize(trace_event_size);
				THROW_IF_WIN32_ERROR(TdhGetEventInformation(
					const_cast<PEVENT_RECORD>(event_record),
					0,
					nullptr,
					reinterpret_cast<PTRACE_EVENT_INFO>(m_traceEventInfoBuffer.data()),
					&trace_event_size));
				m_traceEventInfoPtr = reinterpret_cast<TRACE_EVENT_INFO*>(m_traceEventInfoBuffer.data());
			}

			// retrieve all property data points
			// need to do this in the constructor
			// since the original EVENT_RECORD is required to follow embedded pointers

			if (m_traceEventInfoPtr->TopLevelPropertyCount > 0) {
				// variables for TdhFormatProperty
				USHORT UserDataLength = event_record->UserDataLength;
				PBYTE UserData = static_cast<PBYTE>(event_record->UserData);

				// go through event properties, and pull out the necessary data
				for (ULONG property_count = 0; property_count < m_traceEventInfoPtr->TopLevelPropertyCount;
					++property_count) {
					const auto& event_property_info = m_traceEventInfoPtr->EventPropertyInfoArray[property_count];
					if (event_property_info.Flags != 0) {
						// if Flags & PropertyStruct
						// currently not supporting deep-copying event data of structs
#ifdef TDH_FORMAT_FATAL_CONDITION
						DebugBreak();
#endif
						m_traceMapping.emplace_back(nullptr, 0);
						m_traceProperties.emplace_back(nullptr, 0);
					}
					else if (event_property_info.count > 1) {
						// currently not supporting deep-copying event data of arrays
#ifdef TDH_FORMAT_FATAL_CONDITION
						DebugBreak();
#endif
						m_traceMapping.emplace_back(nullptr, 0);
						m_traceProperties.emplace_back(nullptr, 0);
					}
					else {
						// define the event we want with a PROPERTY_DATA_DESCRIPTOR
						PROPERTY_DATA_DESCRIPTOR property_data_descriptor;
						property_data_descriptor.PropertyName =
							reinterpret_cast<ULONGLONG>(m_traceEventInfoBuffer.data() + event_property_info.NameOffset);
						property_data_descriptor.ArrayIndex = ULONG_MAX;
						property_data_descriptor.Reserved = 0UL;

						// get the buffer size first
						ULONG property_data_size_bytes = 0;
						THROW_IF_WIN32_ERROR(TdhGetPropertySize(
							const_cast<PEVENT_RECORD>(event_record),
							0,       // not using WPP or 'classic' ETW
							nullptr, // not using WPP or 'classic' ETW
							1,       // one property at a time - not support structs of data at this time
							&property_data_descriptor,
							&property_data_size_bytes));

						// now allocate the required buffer, and copy the data
						// - only if the buffer size > 0
						std::shared_ptr<BYTE[]> property_data_buffer;
						if (property_data_size_bytes > 0) {
							property_data_buffer.reset(new BYTE[property_data_size_bytes]);
							THROW_IF_WIN32_ERROR(TdhGetProperty(
								const_cast<PEVENT_RECORD>(event_record),
								0,       // not using WPP or 'classic' ETW
								nullptr, // not using WPP or 'classic' ETW
								1,       // one property at a time - not support structs of data at this time
								&property_data_descriptor,
								property_data_size_bytes,
								property_data_buffer.get()));
						}
						m_traceProperties.emplace_back(property_data_buffer, property_data_size_bytes);

						// additionally capture the mapped string for the property, if it exists
						const PCWSTR property_map_name = reinterpret_cast<PCWSTR>(
							m_traceEventInfoBuffer.data() + event_property_info.nonStructType.MapNameOffset);
						std::shared_ptr<BYTE[]> property_map_buffer;
						DWORD property_map_size_bytes = 0;

						// first query the size needed
						tdhError = TdhGetEventMapInformation(
							const_cast<PEVENT_RECORD>(event_record),
							const_cast<PWSTR>(property_map_name),
							nullptr,
							&property_map_size_bytes);
						if (ERROR_INSUFFICIENT_BUFFER == tdhError) {
							property_map_buffer.reset(new BYTE[property_map_size_bytes]);
							tdhError = TdhGetEventMapInformation(
								const_cast<PEVENT_RECORD>(event_record),
								const_cast<PWSTR>(property_map_name),
								reinterpret_cast<PEVENT_MAP_INFO>(property_map_buffer.get()),
								&property_map_size_bytes);
						}

						switch (tdhError) {
						case ERROR_SUCCESS:
							// all good - do nothing
							break;
						case ERROR_NOT_FOUND:
							// this is OK to keep this event - there just wasn't a mapping for a formatted string
							property_map_buffer.reset();
							break;
						default:
							// any other error is an unexpected failure
#ifdef TDH_FORMAT_FATAL_CONDITION
							FAIL_FAST_MSG(
								"TdhGetEventMapInformation failed with error %u, EVENT_RECORD %p, TRACE_EVENT_INFO %p",
								tdhError,
								event_record,
								m_traceEventInfoPtr);
#else
							property_map_buffer.reset();
#endif
						}
						// if we successfully retrieved the property info
						// format the mapped property value
						if (property_map_buffer) {
							auto property_length = event_property_info.length;
							if (TDH_INTYPE_BINARY == event_property_info.nonStructType.InType &&
								TDH_OUTTYPE_IPV6 == event_property_info.nonStructType.OutType) {
								// per MSDN, must manually set the length for TDH_OUTTYPE_IPV6
								property_length = static_cast<USHORT>(sizeof IN6_ADDR);
							}
							const ULONG pointer_size =
								event_record->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER ? 4 : 8;
							ULONG formattedPropertySize = 0;
							USHORT UserDataConsumed = 0;
							std::shared_ptr<WCHAR[]> formatted_value;
							tdhError = TdhFormatProperty(
								m_traceEventInfoPtr,
								reinterpret_cast<PEVENT_MAP_INFO>(property_map_buffer.get()),
								pointer_size,
								event_property_info.nonStructType.InType,
								event_property_info.nonStructType.OutType,
								property_length,
								UserDataLength,
								UserData,
								&formattedPropertySize,
								nullptr,
								&UserDataConsumed);
							if (ERROR_INSUFFICIENT_BUFFER == tdhError) {
								formatted_value.reset(new WCHAR[formattedPropertySize / sizeof(WCHAR)]);
								tdhError = TdhFormatProperty(
									m_traceEventInfoPtr,
									reinterpret_cast<PEVENT_MAP_INFO>(property_map_buffer.get()),
									pointer_size,
									event_property_info.nonStructType.InType,
									event_property_info.nonStructType.OutType,
									property_length,
									UserDataLength,
									UserData,
									&formattedPropertySize,
									formatted_value.get(),
									&UserDataConsumed);
							}
							if (tdhError != ERROR_SUCCESS) {
#ifdef TDH_FORMAT_FATAL_CONDITION
								FAIL_FAST_MSG(
									"TdhFormatProperty failed with error %u, EVENT_RECORD %p, TRACE_EVENT_INFO %p",
									tdhError,
									event_record,
									m_traceEventInfoPtr);
#else
								m_traceMapping.emplace_back(nullptr, 0);
#endif
							}
							else {
								UserDataLength -= UserDataConsumed;
								UserData += UserDataConsumed;
								// now add the value/size pair to the member std::vector storing all properties
								m_traceMapping.emplace_back(formatted_value, formattedPropertySize);
							}
						}
						else {
							// store null values
							m_traceMapping.emplace_back(nullptr, 0);
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
		ctEtwRecord::swap(ctEtwRecord& rhs) noexcept
	{
		using std::swap;
		swap(m_eventHeaderExtendedData, rhs.m_eventHeaderExtendedData);
		swap(m_eventHeaderData, rhs.m_eventHeaderData);
		swap(m_traceEventInfoBuffer, rhs.m_traceEventInfoBuffer);
		swap(m_traceProperties, rhs.m_traceProperties);
		swap(m_traceMapping, rhs.m_traceMapping);
		swap(m_initialized, rhs.m_initialized);
		// manually swap these structures
		EVENT_HEADER tempHeader;
		memcpy_s(
			&tempHeader, // this to temp
			sizeof(EVENT_HEADER),
			&m_eventHeader,
			sizeof(EVENT_HEADER));
		memcpy_s(
			&m_eventHeader, // in_event to this
			sizeof(EVENT_HEADER),
			&rhs.m_eventHeader,
			sizeof(EVENT_HEADER));
		memcpy_s(
			&rhs.m_eventHeader, // temp to in_event
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
			&rhs.m_etwBufferContext,
			sizeof(ETW_BUFFER_CONTEXT));
		memcpy_s(
			&rhs.m_etwBufferContext, // temp to in_event
			sizeof(ETW_BUFFER_CONTEXT),
			&tempBuffContext,
			sizeof(ETW_BUFFER_CONTEXT));
	}

	/**
	 * @brief Non-member swap() function for ctEtwRecord
	 * @param a First ctEtwRecord to swap
	 * @param b Second ctEtwRecord to swap
	 * @details Implementing the non-member swap to be usable generically
	 */
	inline void
		swap(ctEtwRecord& a, ctEtwRecord& b) noexcept
	{
		a.swap(b);
	}

	/**
	 * @brief writeRecord() - simple text dump of all event properties to a std::wstring object
	 * @param reusable_string String buffer to reuse for output
	 */
	inline void
		ctEtwRecord::writeRecord(std::wstring& reusable_string) const
	{
		// write to a temp string - but use the caller's buffer
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
		//  Data from ETW_BUFFER_CONTEXT properties
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
		//  Data from EVENT_HEADER_EXTENDED_DATA_ITEM properties
		//
		if (queryRelatedActivityId(&guidBuf)) {
			wsData += L"\n\tRelated Activity ID ";
			THROW_IF_WIN32_ERROR(::UuidToString(&guidBuf, &pszGuid));
			wsData += reinterpret_cast<LPWSTR>(pszGuid.get());
		}

		std::vector<BYTE> pSID;
		if (querySID(pSID)) {
			wsData += L"\n\tSID ";
			wil::unique_hlocal_string szSID = nullptr;
			if (::ConvertSidToStringSid(pSID.data(), &szSID)) {
				wsData += szSID.get();
			}
			else {
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
		//  Accessors for TRACE_EVENT_INFO properties
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
			case DecodingSourceTlg:
				wsData += L"DecodingSourceTlg";
				break;
			case DecodingSourceMax:
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
				const BYTE* pByteInfo = m_traceEventInfoBuffer.data();
				const TRACE_EVENT_INFO* pTraceInfo =
					reinterpret_cast<const TRACE_EVENT_INFO*>(m_traceEventInfoBuffer.data());
				wsData += L"\n\tProperty Names:";
				for (ULONG ulCount = 0; ulCount < ulData; ++ulCount) {
					wsData.append(L"\n\t\t");
					wsData.append(reinterpret_cast<const wchar_t*>(
						pByteInfo + pTraceInfo->EventPropertyInfoArray[ulCount].NameOffset));
					wsData.append(L": ");
					wsData.append(BuildEventPropertyString(ulCount));
				}
			}
		}

		// swap and return
		reusable_string.swap(wsData);
	}

	inline void
		ctEtwRecord::writeFormattedMessage(std::wstring& reusable_string, bool include_message_properties) const
	{
		// write to a temp string - but use the caller's buffer
		std::wstring wsData;
		wsData.swap(reusable_string);
		wsData.clear();

		ULONG ulData = 0;
		if (queryTopLevelPropertyCount(&ulData) && ulData > 0) {
			const BYTE* pByteInfo = m_traceEventInfoBuffer.data();
			const TRACE_EVENT_INFO* pTraceInfo = reinterpret_cast<const TRACE_EVENT_INFO*>(m_traceEventInfoBuffer.data());

			std::wstring wsProperties;
			std::vector<std::wstring> wsPropertyVector;
			for (ULONG ulCount = 0; ulCount < ulData; ++ulCount) {
				wsProperties.append(L"\n[");
				wsProperties.append(
					reinterpret_cast<const wchar_t*>(pByteInfo + pTraceInfo->EventPropertyInfoArray[ulCount].NameOffset));
				wsProperties.append(L"] ");

				// use the mapped string if it's available
				if (m_traceMapping[ulCount].first) {
					wsProperties.append(m_traceMapping[ulCount].first.get());
					wsPropertyVector.emplace_back(m_traceMapping[ulCount].first.get());
				}
				else {
					const std::wstring wsPropertyValue = BuildEventPropertyString(ulCount);
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
				WCHAR* formattedMessage = nullptr;
				if (0 != FormatMessageW(
					FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY,
					wsEventMessage.c_str(),
					0,
					0,
					reinterpret_cast<LPWSTR>(&formattedMessage), // will be allocated from LocalAlloc
					0,
					reinterpret_cast<va_list*>(messageArguments.data()))) {
					const auto free_message = wil::scope_exit([&] { LocalFree(formattedMessage); });
					wsData.append(formattedMessage);
				}
				else {
					wsData.append(wsEventMessage);
				}
			}
			if (include_message_properties) {
				wsData.append(L"\nEvent Message Properties:");
				wsData.append(wsProperties);
			}
		}
		else {
			wsData.clear();
		}

		// swap and return
		reusable_string.swap(wsData);
	}

	inline std::map<std::wstring, std::wstring>
		ctEtwRecord::writeMessageProperties() const
	{
		std::map<std::wstring, std::wstring> wsProperties;

		ULONG ulData = 0;
		if (queryTopLevelPropertyCount(&ulData) && ulData > 0) {
			const BYTE* pByteInfo = m_traceEventInfoBuffer.data();
			const TRACE_EVENT_INFO* pTraceInfo = reinterpret_cast<const TRACE_EVENT_INFO*>(m_traceEventInfoBuffer.data());

			for (ULONG ulCount = 0; ulCount < ulData; ++ulCount) {
				const std::wstring key =
					reinterpret_cast<const wchar_t*>(pByteInfo + pTraceInfo->EventPropertyInfoArray[ulCount].NameOffset);

				// use the mapped string if it's available
				std::wstring value;
				if (m_traceMapping[ulCount].first) {
					value = m_traceMapping[ulCount].first.get();
				}
				else {
					value = BuildEventPropertyString(ulCount);
				}

				wsProperties[key] = value;
			}
		}

		return wsProperties;
	}

	/**
	 * @brief Comparison operators
	 */
	inline bool
		ctEtwRecord::operator==(_In_ const ctEtwRecord& rhs) const noexcept
		try {
		if (0 != memcmp(&m_eventHeader, &rhs.m_eventHeader, sizeof(EVENT_HEADER))) {
			return false;
		}
		if (0 != memcmp(&m_etwBufferContext, &rhs.m_etwBufferContext, sizeof(ETW_BUFFER_CONTEXT))) {
			return false;
		}
		if (m_initialized != rhs.m_initialized) {
			return false;
		}

		// a deep comparison of the m_eventHeaderExtendedData member
		// can't just do a byte comparison of the structs since the DataPtr member is a raw ptr value
		// - and can be different raw buffers with the same event

		if (m_eventHeaderExtendedData.size() != rhs.m_eventHeaderExtendedData.size()) {
			return false;
		}

		auto this_extendedDataIterator = m_eventHeaderExtendedData.cbegin();
		const auto this_extendedDataEnd = m_eventHeaderExtendedData.cend();
		auto rhs_extendedDataIterator = rhs.m_eventHeaderExtendedData.cbegin();
		const auto rhs_extendedEventDataEnd = rhs.m_eventHeaderExtendedData.cend();

		for (; this_extendedDataIterator != this_extendedDataEnd && rhs_extendedDataIterator != rhs_extendedEventDataEnd;
			++this_extendedDataIterator, ++rhs_extendedDataIterator) {
			if (this_extendedDataIterator->ExtType != rhs_extendedDataIterator->ExtType) {
				return false;
			}
			if (this_extendedDataIterator->DataSize != rhs_extendedDataIterator->DataSize) {
				return false;
			}
			if (0 != memcmp(
				reinterpret_cast<VOID*>(this_extendedDataIterator->DataPtr),
				reinterpret_cast<VOID*>(rhs_extendedDataIterator->DataPtr),
				this_extendedDataIterator->DataSize)) {
				return false;
			}
		}
		if (m_traceEventInfoBuffer.size() != rhs.m_traceEventInfoBuffer.size()) {
			return false;
		}
		if (m_traceEventInfoBuffer != rhs.m_traceEventInfoBuffer) {
			return false;
		}

		return true;
	}
	catch (...) {
		return false;
	}

	inline bool
		ctEtwRecord::operator!=(_In_ const ctEtwRecord& rhs) const noexcept
	{
		return !operator==(rhs);
	}

	/**
	 * @brief Accessors for EVENT_HEADER properties
	 * @details Retrieved from the member variable EVENT_HEADER eventHeader
	 */
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

	inline bool
		ctEtwRecord::queryKernelTime(_Out_ ULONG* kernel_time) const noexcept
	{
		if (!m_initialized) {
			return false;
		}

		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_PRIVATE_SESSION || m_eventHeader.Flags & EVENT_HEADER_FLAG_NO_CPUTIME) {
			return false;
		}

		*kernel_time = m_eventHeader.KernelTime;
		return true;
	}

	inline bool
		ctEtwRecord::queryUserTime(_Out_ ULONG* user_time) const noexcept
	{
		if (!m_initialized) {
			return false;
		}

		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_PRIVATE_SESSION || m_eventHeader.Flags & EVENT_HEADER_FLAG_NO_CPUTIME) {
			return false;
		}

		*user_time = m_eventHeader.UserTime;
		return true;
	}

	inline ULONG64
		ctEtwRecord::getProcessorTime() const noexcept
	{
		return m_eventHeader.ProcessorTime;
	}

	/**
	 * @brief Accessors for EVENT_DESCRIPTOR properties
	 * @details Retrieved from the member variable EVENT_HEADER eventHeader.EventDescriptor
	 */
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

	/**
	 * @brief Accessors for ETW_BUFFER_CONTEXT properties
	 * @details Retrieved from the member variable ETW_BUFFER_CONTEXT etwBufferContext
	 */
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

	/**
	 * @brief Accessors for EVENT_HEADER_EXTENDED_DATA_ITEM properties
	 * @details Retrieved from the member variable
	 *          std::vector<EVENT_HEADER_EXTENDED_DATA_ITEM> m_eventHeaderExtendedData.
	 *          Required to walk the std::vector to determine if the asked-for property
	 *          is in any of the data items stored.
	 */
	inline bool
		ctEtwRecord::queryRelatedActivityId(_Out_ GUID* related_activity_id) const noexcept
	{
		*related_activity_id = {};
		if (!m_initialized) {
			return false;
		}

		bool bFoundProperty = false;
		for (const auto& tempItem : m_eventHeaderExtendedData) {
			if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_RELATED_ACTIVITYID) {
				FAIL_FAST_IF(tempItem.DataSize != sizeof(EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID));
				const EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID* relatedID =
					reinterpret_cast<EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID*>(tempItem.DataPtr);
				*related_activity_id = relatedID->RelatedActivityId;
				bFoundProperty = true;
				break;
			}
		}

		return bFoundProperty;
	}

	inline bool
		ctEtwRecord::querySID(_Out_ std::vector<BYTE>& sid) const
	{
		sid.clear();
		if (!m_initialized) {
			return false;
		}

		bool bFoundProperty = false;
		for (const auto& tempItem : m_eventHeaderExtendedData) {
			if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_SID) {
				sid.assign(
					reinterpret_cast<BYTE*>(tempItem.DataPtr),
					reinterpret_cast<BYTE*>(tempItem.DataPtr) + tempItem.DataSize);
				bFoundProperty = true;
				break;
			}
		}

		return bFoundProperty;
	}

	inline bool
		ctEtwRecord::queryTerminalSessionId(_Out_ ULONG* terminal_session_id) const noexcept
	{
		*terminal_session_id = {};
		if (!m_initialized) {
			return false;
		}

		bool bFoundProperty = false;
		for (const auto& tempItem : m_eventHeaderExtendedData) {
			if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_TS_ID) {
				FAIL_FAST_IF(tempItem.DataSize != sizeof(EVENT_EXTENDED_ITEM_TS_ID));
				const EVENT_EXTENDED_ITEM_TS_ID* ts_ID = reinterpret_cast<EVENT_EXTENDED_ITEM_TS_ID*>(tempItem.DataPtr);
				*terminal_session_id = ts_ID->SessionId;
				bFoundProperty = true;
				break;
			}
		}

		return bFoundProperty;
	}

	inline bool
		ctEtwRecord::queryTransactionInstanceId(_Out_ ULONG* transaction_instance_id) const noexcept
	{
		*transaction_instance_id = {};
		if (!m_initialized) {
			return false;
		}

		bool bFoundProperty = false;
		for (const auto& tempItem : m_eventHeaderExtendedData) {
			if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_INSTANCE_INFO) {
				FAIL_FAST_IF(tempItem.DataSize != sizeof(EVENT_EXTENDED_ITEM_INSTANCE));
				const EVENT_EXTENDED_ITEM_INSTANCE* instanceInfo =
					reinterpret_cast<EVENT_EXTENDED_ITEM_INSTANCE*>(tempItem.DataPtr);
				*transaction_instance_id = instanceInfo->InstanceId;
				bFoundProperty = true;
				break;
			}
		}

		return bFoundProperty;
	}

	inline bool
		ctEtwRecord::queryTransactionParentInstanceId(_Out_ ULONG* transaction_parent_instance_id) const noexcept
	{
		*transaction_parent_instance_id = {};
		if (!m_initialized) {
			return false;
		}

		bool bFoundProperty = false;
		for (const auto& tempItem : m_eventHeaderExtendedData) {
			if (tempItem.ExtType == EVENT_HEADER_EXT_TYPE_INSTANCE_INFO) {
				FAIL_FAST_IF(tempItem.DataSize != sizeof(EVENT_EXTENDED_ITEM_INSTANCE));
				const EVENT_EXTENDED_ITEM_INSTANCE* instanceInfo =
					reinterpret_cast<EVENT_EXTENDED_ITEM_INSTANCE*>(tempItem.DataPtr);
				*transaction_parent_instance_id = instanceInfo->ParentInstanceId;
				bFoundProperty = true;
				break;
			}
		}

		return bFoundProperty;
	}

	inline bool
		ctEtwRecord::queryTransactionParentGuid(_Out_ GUID* transaction_parent_guid) const noexcept
	{
		*transaction_parent_guid = {};
		if (!m_initialized) {
			return false;
		}

		bool bFoundProperty = false;
		for (const auto& extended_data_item : m_eventHeaderExtendedData) {
			if (extended_data_item.ExtType == EVENT_HEADER_EXT_TYPE_INSTANCE_INFO) {
				FAIL_FAST_IF(extended_data_item.DataSize != sizeof(EVENT_EXTENDED_ITEM_INSTANCE));
				const EVENT_EXTENDED_ITEM_INSTANCE* instanceInfo =
					reinterpret_cast<EVENT_EXTENDED_ITEM_INSTANCE*>(extended_data_item.DataPtr);
				*transaction_parent_guid = instanceInfo->ParentGuid;
				bFoundProperty = true;
				break;
			}
		}

		return bFoundProperty;
	}

	/**
	 * @brief Accessors for TRACE_EVENT_INFO properties
	 * @details Only valid if the EVENT_HEADER_FLAG_STRING_ONLY flag is not set in
	 *          the parent EVENT_HEADER struct.
	 */
	inline bool
		ctEtwRecord::queryProviderGuid(_Out_ GUID* provider_guid) const noexcept
	{
		*provider_guid = {};
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}

		*provider_guid = m_traceEventInfoPtr->ProviderGuid;
		return true;
	}

	inline bool
		ctEtwRecord::queryDecodingSource(_Out_ DECODING_SOURCE* decoding_source) const noexcept
	{
		*decoding_source = {};
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}

		*decoding_source = m_traceEventInfoPtr->DecodingSource;
		return true;
	}

	inline bool
		ctEtwRecord::queryProviderName(_Out_ std::wstring& provider_name) const
	{
		provider_name.clear();
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		if (0 == m_traceEventInfoPtr->ProviderNameOffset) {
			return false;
		}

		const wchar_t* szProviderName =
			reinterpret_cast<const wchar_t*>(m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->ProviderNameOffset);
		provider_name.assign(szProviderName);
		return true;
	}

	inline bool
		ctEtwRecord::queryLevelName(_Out_ std::wstring& level_name) const
	{
		level_name.clear();
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		if (0 == m_traceEventInfoPtr->LevelNameOffset) {
			return false;
		}

		const wchar_t* szLevelName =
			reinterpret_cast<const wchar_t*>(m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->LevelNameOffset);
		level_name.assign(szLevelName);
		return true;
	}

	inline bool
		ctEtwRecord::queryChannelName(_Out_ std::wstring& channel_name) const
	{
		channel_name.clear();
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		if (0 == m_traceEventInfoPtr->ChannelNameOffset) {
			return false;
		}

		const wchar_t* szChannelName =
			reinterpret_cast<const wchar_t*>(m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->ChannelNameOffset);
		channel_name.assign(szChannelName);
		return true;
	}

	inline bool
		ctEtwRecord::queryKeywords(_Out_ std::vector<std::wstring>& keywords) const
	{
		keywords.clear();
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		if (0 == m_traceEventInfoPtr->KeywordsNameOffset) {
			return false;
		}

		const wchar_t* key_name =
			reinterpret_cast<const wchar_t*>(m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->KeywordsNameOffset);

		std::vector<std::wstring> temp_keywords;
		while (*key_name != L'\0') {
			temp_keywords.emplace_back(key_name);
			key_name += wcslen(key_name) + 1;
		}

		temp_keywords.swap(keywords);
		return true;
	}

	inline bool
		ctEtwRecord::queryTaskName(_Out_ std::wstring& task_name) const
	{
		task_name.clear();
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		if (0 == m_traceEventInfoPtr->TaskNameOffset) {
			return false;
		}

		const wchar_t* szTaskName =
			reinterpret_cast<const wchar_t*>(m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->TaskNameOffset);
		task_name.assign(szTaskName);
		return true;
	}

	inline bool
		ctEtwRecord::queryOpcodeName(_Out_ std::wstring& opcode_name) const
	{
		opcode_name.clear();
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		if (0 == m_traceEventInfoPtr->OpcodeNameOffset) {
			return false;
		}

		const wchar_t* szOpcodeName =
			reinterpret_cast<const wchar_t*>(m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->OpcodeNameOffset);
		opcode_name.assign(szOpcodeName);
		return true;
	}

	inline bool
		ctEtwRecord::queryEventMessage(_Out_ std::wstring& event_message) const
	{
		event_message.clear();
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		if (0 == m_traceEventInfoPtr->EventMessageOffset) {
			return false;
		}

		const wchar_t* szEventMessage =
			reinterpret_cast<const wchar_t*>(m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->EventMessageOffset);
		event_message.assign(szEventMessage);
		return true;
	}

	inline bool
		ctEtwRecord::queryProviderMessageName(_Out_ std::wstring& provider_message_name) const
	{
		provider_message_name.clear();
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		if (0 == m_traceEventInfoPtr->ProviderMessageOffset) {
			return false;
		}

		const wchar_t* szProviderMessageName =
			reinterpret_cast<const wchar_t*>(m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->ProviderMessageOffset);
		provider_message_name.assign(szProviderMessageName);
		return true;
	}

	inline bool
		ctEtwRecord::queryPropertyCount(_Out_ ULONG* property_count) const noexcept
	{
		*property_count = {};
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		*property_count = m_traceEventInfoPtr->PropertyCount;
		return true;
	}

	inline bool
		ctEtwRecord::queryTopLevelPropertyCount(_Out_ ULONG* top_level_property_count) const noexcept
	{
		*top_level_property_count = {};
		if (!m_initialized) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		*top_level_property_count = m_traceEventInfoPtr->TopLevelPropertyCount;
		return true;
	}

	inline bool
		ctEtwRecord::queryEventPropertyStringValue(_Out_ std::wstring& event_property_string_value) const
	{
		event_property_string_value.clear();
		if (!m_initialized) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}
		if (m_eventHeader.Flags & EVENT_HEADER_FLAG_STRING_ONLY) {
			// per the flags, the byte array is a null-terminated string
			event_property_string_value.assign(reinterpret_cast<const wchar_t*>(m_traceEventInfoBuffer.data()));
			return true;
		}

		return false;
	}

	inline bool
		ctEtwRecord::queryEventPropertyName(_In_ const ULONG index, _Out_ std::wstring& property_name) const
	{
		property_name.clear();
		// immediately fail if no top level property count value or the value is 0
		ULONG top_level_property_count = 0;
		if (!queryTopLevelPropertyCount(&top_level_property_count) || 0 == top_level_property_count) {
			return false;
		}
		if (index >= top_level_property_count) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}

		const auto* szPropertyFound = reinterpret_cast<const wchar_t*>(
			m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->EventPropertyInfoArray[index].NameOffset);
		property_name.assign(szPropertyFound);

		return true;
	}

	inline bool
		ctEtwRecord::queryEventProperty(_In_ PCWSTR property_name, _Out_ std::wstring& property_value) const
	{
		// immediately fail if no top level property count value or the value is 0
		ULONG top_level_property_count = 0;

		if (!queryTopLevelPropertyCount(&top_level_property_count) || 0 == top_level_property_count) {
			property_value.clear();
			return false;
		}
		if (!m_traceEventInfoPtr) {
			property_value.clear();
			return false;
		}

		// iterate through each property name looking for a match
		for (ULONG ulCount = 0; ulCount < top_level_property_count; ++ulCount) {
			const auto* szPropertyFound = reinterpret_cast<const wchar_t*>(
				m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->EventPropertyInfoArray[ulCount].NameOffset);
			if (0 == _wcsicmp(property_name, szPropertyFound)) {
				property_value.assign(BuildEventPropertyString(ulCount));
				return true;
			}
		}
		property_value.clear();
		return false;
	}

	inline bool
		ctEtwRecord::queryEventProperty(_In_ const ULONG index, _Out_ std::wstring& property_value) const
	{
		property_value.clear();

		// immediately fail if no top level property count value or the value is 0 or index is larger than
		// total number of properties
		ULONG top_level_property_count = 0;

		if (!queryTopLevelPropertyCount(&top_level_property_count) || 0 == top_level_property_count) {
			return false;
		}
		if (0 == index || index > top_level_property_count) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}

		bool bFoundProperty = false;
		// get the property value
		const wchar_t* name_value = reinterpret_cast<const wchar_t*>(
			m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->EventPropertyInfoArray[index - 1].NameOffset);
		if (name_value) {
			property_value.assign(BuildEventPropertyString(index - 1));
			bFoundProperty = true;
		}

		return bFoundProperty;
	}

	inline bool
		ctEtwRecord::queryEventProperty(_In_ PCWSTR property_name, _Out_ ctPropertyPair& event_properties) const
	{
		event_properties = {};

		// immediately fail if no top level property count value or the value is 0
		ULONG ulData = 0;
		if (!queryTopLevelPropertyCount(&ulData) || 0 == ulData) {
			return false;
		}
		if (!m_traceEventInfoPtr) {
			return false;
		}

		// iterate through each property name looking for a match
		bool bFoundMatch = false;

		for (ULONG ulCount = 0; !bFoundMatch && ulCount < ulData; ++ulCount) {
			const auto* szPropertyFound = reinterpret_cast<const wchar_t*>(
				m_traceEventInfoBuffer.data() + m_traceEventInfoPtr->EventPropertyInfoArray[ulCount].NameOffset);

			if (0 == _wcsicmp(property_name, szPropertyFound)) {
				FAIL_FAST_IF(ulCount >= m_traceProperties.size());
				if (ulCount < m_traceProperties.size()) {
					event_properties = m_traceProperties[ulCount];
					bFoundMatch = true;
				}
				else {
#ifdef TDH_FORMAT_FATAL_CONDITION
					// something is messed up - the properties found didn't match the # of property values
					DebugBreak();
#endif
					break;
				}
			}
		}
		return bFoundMatch;
	}

	inline std::wstring
	ctEtwRecord::BuildEventPropertyString(ULONG property_index) const
	{
		//
		// immediately fail if no top level property count value or the value asked for is out of range
		ULONG ulData = 0;
		if (!queryTopLevelPropertyCount(&ulData) || property_index >= ulData) {
			throw std::runtime_error("ctEtwRecord - ETW Property value requested is out of range");
		}

		constexpr unsigned cch_StackBuffer = 100;
		wchar_t stackBuffer[cch_StackBuffer]{};

		// retrieve the raw property information
		const BYTE* propertyBuffer = m_traceProperties[property_index].first.get();
		const ULONG propertySizeBytes = m_traceProperties[property_index].second;

		// build a string only if the property data > 0 bytes
		// build the string based on the IN and OUT types
		auto eventPropertyOutType = static_cast<_TDH_OUT_TYPE>(m_traceEventInfoPtr->EventPropertyInfoArray[property_index].nonStructType.OutType);
		const auto& eventPropertyInfo = m_traceEventInfoPtr->EventPropertyInfoArray[property_index];
		switch (eventPropertyInfo.nonStructType.InType) {

		case TDH_INTYPE_NULL: {
			return L"null";
		}

		case TDH_INTYPE_UNICODESTRING: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_STRING;
			}
			return details::PrintWcharString(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_ANSISTRING: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_STRING;
			}
			return details::PrintCharString(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_INT8: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_BYTE;
			}
			return details::Print8BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_UINT8: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_UNSIGNEDBYTE;
			}
			return details::Print8BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_INT16: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_SHORT;
			}
			return details::Print16BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_UINT16: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_UNSIGNEDSHORT;
			}
			return details::Print16BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
			// xs:unsignedShort; win:Port; win:HexInt16
		}

		case TDH_INTYPE_INT32: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_INT;
			}
			return details::Print32BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_UINT32: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_UNSIGNEDINT;
			}
			return details::Print32BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_INT64: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_LONG;
			}
			return details::Print64BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_UINT64: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_UNSIGNEDLONG;
			}
			return details::Print64BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_FLOAT: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_FLOAT;
			}
			FAIL_FAST_IF(4 != propertySizeBytes);
			const auto prop = *reinterpret_cast<const FLOAT*>(propertyBuffer);
			const auto conversion = swprintf_s(stackBuffer, cch_StackBuffer, L"%f", prop);
			FAIL_FAST_IF(conversion <= 0);
			return stackBuffer;
		}

		case TDH_INTYPE_DOUBLE: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_DOUBLE;
			}
			FAIL_FAST_IF(8 != propertySizeBytes);
			const auto prop = *reinterpret_cast<const DOUBLE*>(propertyBuffer);
			const auto conversion = swprintf_s(stackBuffer, cch_StackBuffer, L"%f", prop);
			FAIL_FAST_IF(conversion <= 0);
			return stackBuffer;
		}

		case TDH_INTYPE_BOOLEAN: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_BOOLEAN;
			}
			// TDH_INTYPE_BOOLEAN are defined as 4-byte values
			return details::Print32BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_BINARY: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_HEXBINARY;
			}
			return details::PrintHexBinary(eventPropertyOutType, propertyBuffer, propertySizeBytes);
			break;
		}

		case TDH_INTYPE_GUID: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_GUID;
			}
			FAIL_FAST_IF(sizeof(GUID) != propertySizeBytes);
			FAIL_FAST_IF(TDH_OUTTYPE_GUID != eventPropertyOutType);

			wil::unique_rpc_wstr pszGuid;
			const RPC_STATUS uuidStatus = ::UuidToString(reinterpret_cast<const GUID*>(propertyBuffer), &pszGuid);
			FAIL_FAST_IF(uuidStatus != RPC_S_OK);
			return reinterpret_cast<LPWSTR>(pszGuid.get());
		}

		case TDH_INTYPE_POINTER: // fall-through
		case TDH_INTYPE_SIZET:{
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_HEXINT64;
			}
			if (4 == propertySizeBytes) {
				return details::Print32BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
			}
			
			if (8 == propertySizeBytes) {
				return details::Print64BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
			}

			FAIL_FAST_MSG(
				"Unknown TDH_OUTTYPE [%u] for the TDH_INTYPE_POINTER with a %d -size value",
				eventPropertyOutType,
				propertySizeBytes);
		}

		case TDH_INTYPE_FILETIME: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_DATETIME;
			}
			// xs:dateTime
			FAIL_FAST_IF(sizeof(FILETIME) != propertySizeBytes);
			const FILETIME ft = *reinterpret_cast<const FILETIME*>(propertyBuffer);
			LARGE_INTEGER li;
			li.LowPart = ft.dwLowDateTime;
			li.HighPart = static_cast<LONG>(ft.dwHighDateTime);
			_ui64tow_s(li.QuadPart, stackBuffer, cch_StackBuffer, 16);
			return std::wstring(L"0x") + stackBuffer;
		}

		case TDH_INTYPE_SYSTEMTIME: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_DATETIME;
			}
			FAIL_FAST_IF(sizeof(SYSTEMTIME) != propertySizeBytes);
			const SYSTEMTIME st = *reinterpret_cast<const SYSTEMTIME*>(propertyBuffer);
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
			return stackBuffer;
		}

		case TDH_INTYPE_SID: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_STRING;
			}
			//
			// first write out the raw binary
			std::wstring wsData = L"[";
			const BYTE* buffer = propertyBuffer;
			for (ULONG ulBits = 0; ulBits < propertySizeBytes; ++ulBits) {
				const char chData = static_cast<char>(buffer[ulBits]);
				_itow_s(chData, stackBuffer, 16);
				wsData += stackBuffer;
			}
			wsData += L']';
			//
			// now convert if we can to the friendly name
			// LookupAccountSid is not const correct
			SID* pSid = reinterpret_cast<SID*>(const_cast<BYTE*>(buffer));

			DWORD cchName = 0;
			DWORD cchDomain = 0;
			SID_NAME_USE sidNameUse{};
			WCHAR temp[1];
			DWORD gle = 0;
			if (!::LookupAccountSid(nullptr, pSid, temp, &cchName, temp, &cchDomain, &sidNameUse)) {
				gle = GetLastError();
				if (gle == ERROR_INSUFFICIENT_BUFFER) {
					std::vector account_name(cchName, L'\0');
					std::vector account_domain(cchDomain, L'\0');
					if (::LookupAccountSid(
						nullptr, pSid, account_name.data(), &cchName, account_domain.data(), &cchDomain, &sidNameUse)) {
						wsData += L"  ";
						wsData += account_domain.data();
						wsData += L"\\";
						wsData += account_name.data();
				    } else {
						gle = GetLastError();
				    }
				}
			}
			if (gle != 0) {
				wsData += L"  (LookupAccountSid failed: ";
				_ultow_s(gle, stackBuffer, cch_StackBuffer, 10);
				wsData += stackBuffer;
				wsData += L")";
			}
			return wsData;
		}

		case TDH_INTYPE_HEXINT32: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_HEXINT32;
			}
			return details::Print32BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

		case TDH_INTYPE_HEXINT64: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_HEXINT64;
			}
			return details::Print64BitInteger(eventPropertyOutType, propertyBuffer, propertySizeBytes);
		}

	    /*
	     Field contains a little-endian 16-bit bytecount followed by a WCHAR
	     (16-bit character) string. Default OutType is STRING. Other usable
	     OutTypes include XML, JSON. Field size is determined by reading the
	     first two bytes of the payload, which are then interpreted as a
	     little-endian 16-bit integer which gives the number of additional bytes
	     (not characters) in the field.
	     */
		case TDH_INTYPE_MANIFEST_COUNTEDSTRING:
		case TDH_INTYPE_COUNTEDSTRING:
		case TDH_INTYPE_NONNULLTERMINATEDSTRING: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_STRING;
			}
			FAIL_FAST_IF(propertySizeBytes < 2);
			const auto cbString = *reinterpret_cast<const USHORT*>(propertyBuffer);
			if (cbString == 0) {
			    return {};
			}
            return details::PrintWcharString(eventPropertyOutType, propertyBuffer + sizeof(USHORT), cbString);
		}

		case TDH_INTYPE_MANIFEST_COUNTEDANSISTRING:
		case TDH_INTYPE_COUNTEDANSISTRING:
		case TDH_INTYPE_NONNULLTERMINATEDANSISTRING:{
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_STRING;
			}
			FAIL_FAST_IF(propertySizeBytes < 2);
			const auto cbString = *reinterpret_cast<const USHORT*>(propertyBuffer);
			if (cbString == 0) {
				return {};
			}
            return details::PrintCharString(eventPropertyOutType, propertyBuffer + sizeof(USHORT), cbString);
		}

		case TDH_INTYPE_MANIFEST_COUNTEDBINARY: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_HEXBINARY;
			}
			FAIL_FAST_IF(propertySizeBytes < 2);
			const auto cbString = *reinterpret_cast<const USHORT*>(propertyBuffer);
			if (cbString == 0) {
				return {};
			}
            return details::PrintHexBinary(eventPropertyOutType, propertyBuffer + sizeof(USHORT), cbString);
		}

		case TDH_INTYPE_UNICODECHAR: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_STRING;
			}
			FAIL_FAST_IF(propertySizeBytes != 2);
            return details::PrintWcharString(eventPropertyOutType, propertyBuffer, sizeof(WCHAR));
		}

		case TDH_INTYPE_ANSICHAR: {
			if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_STRING;
			}
			FAIL_FAST_IF(propertySizeBytes != 1);
            return details::PrintCharString(eventPropertyOutType, propertyBuffer, sizeof(CHAR));
		}

        /*Field contains binary data. Default OutType is HEXBINARY. Field size is
          determined by reading the first four bytes of the payload, which are
          then interpreted as a little-endian UINT32 which gives the number of
          additional bytes in the field.
        */
		    case TDH_INTYPE_HEXDUMP: {
		    if (eventPropertyOutType == TDH_OUTTYPE_NULL) {
				eventPropertyOutType = TDH_OUTTYPE_HEXBINARY;
			}
			FAIL_FAST_IF(propertySizeBytes < 4);
			const auto hex_dump_size = *reinterpret_cast<const UINT32*>(propertyBuffer);
			if (hex_dump_size == 0)	{
			    return {};
			}
			return details::PrintHexBinary(eventPropertyOutType, propertyBuffer + sizeof(UINT32), hex_dump_size);
		}

		default:
			FAIL_FAST();
		} // switch statement
	}

	// functions to process conversions to output types
	inline std::wstring
	details::PrintHexBinary(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize)
	{
		constexpr unsigned cch_StackBuffer = 100;
		wchar_t stackBuffer[cch_StackBuffer]{};

		std::wstring wsData;
		// xs:hexBinary, win:IPv6 (16 bytes), win:SocketAddress
		if (TDH_OUTTYPE_HEXBINARY == propertyOutType) {
			wsData = L'[';
			const BYTE* buffer = propertyBuffer;
			for (ULONG ulBits = 0; ulBits < propertyByteSize; ++ulBits) {
				const unsigned char chData = buffer[ulBits];
				_itow_s(chData, stackBuffer, 16);
				wsData += stackBuffer;
			}
			wsData += L']';
		}
		else if (TDH_OUTTYPE_IPV6 == propertyOutType) {
			::RtlIpv6AddressToString(reinterpret_cast<const IN6_ADDR*>(propertyBuffer), stackBuffer);
			wsData += stackBuffer;
		}
		else if (TDH_OUTTYPE_SOCKETADDRESS == propertyOutType) {
			DWORD dwSize = cch_StackBuffer;
			// Winsock APIs are not const-correct
			const int iReturn = ::WSAAddressToString(
				reinterpret_cast<sockaddr*>(const_cast<BYTE*>(propertyBuffer)),
				propertyByteSize,
				nullptr,
				stackBuffer,
				&dwSize);
			if (0 == iReturn) {
				wsData = stackBuffer;
			}
		}
		else {
			FAIL_FAST_MSG("Unknown TDH_OUTTYPE [%u] for the TDH_INTYPE_BINARY value", propertyOutType);
		}

		return wsData;
	}

	inline std::wstring
	details::PrintWcharString(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize)
	{
		FAIL_FAST_IF(propertyOutType != TDH_OUTTYPE_STRING);

		// not guaranteed to be NULL terminated, must use the byte size
		const auto* wszBuffer = reinterpret_cast<const wchar_t*>(propertyBuffer);
		const auto* wszBufferEnd = wszBuffer + (propertyByteSize / 2);

		std::wstring wsData(wszBuffer, wszBufferEnd);
		// remove any embedded nulls (stop at the first null)
		wsData.erase(std::ranges::find(wsData, L'\0'), wsData.end());
		return wsData;
	}

	inline std::wstring
	details::PrintCharString(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize)
	{
		FAIL_FAST_IF(propertyOutType != TDH_OUTTYPE_STRING);

		// not guaranteed to be NULL terminated, must use the byte size
		const auto* szBuffer = reinterpret_cast<const char*>(propertyBuffer);
		const auto* szBufferEnd = szBuffer + propertyByteSize;

		std::string sData(szBuffer, szBufferEnd);
		// remove any embedded nulls (stop at the first null)
		sData.erase(std::ranges::find(sData, L'\0'), sData.end());

		// convert to wide
		auto required_bytes = MultiByteToWideChar(CP_UTF8, 0, sData.c_str(), -1, nullptr, 0);
		FAIL_FAST_IF(required_bytes == 0);

		std::vector conversion(required_bytes, L'\0');
		required_bytes = MultiByteToWideChar(CP_UTF8, 0, sData.c_str(), -1, conversion.data(), required_bytes);
		FAIL_FAST_IF(required_bytes == 0);
		return conversion.data();
	}

    inline std::wstring
    details::Print64BitInteger(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize)
	{
	    FAIL_FAST_IF(propertyByteSize != 8);
	    constexpr unsigned cch_StackBuffer = 100;
		wchar_t stackBuffer[cch_StackBuffer]{};

		switch (propertyOutType)
		{
			// signed 64-bit integer can indicate TDH_OUTPUT_LONG to display as a signed long long
		    case TDH_OUTTYPE_LONG: {
			    const auto prop = *reinterpret_cast<const INT64*>(propertyBuffer);
			    _i64tow_s(prop, stackBuffer, cch_StackBuffer, 10);
			    return stackBuffer;
		    }

		    case TDH_OUTTYPE_HEXINT64: {
			    const auto prop = *reinterpret_cast<const UINT64*>(propertyBuffer);
				_ui64tow_s(prop, stackBuffer, cch_StackBuffer, 16);
				return std::wstring(L"0x") + stackBuffer;
		    }

	        case TDH_OUTTYPE_BOOLEAN: {
		        const auto prop = *reinterpret_cast<const UINT64*>(propertyBuffer);
			    return prop == 0 ? L"false" : L"true";
		    }

		    default:{
			    // for any other 64-bit output type, display as an unsigned 64-bit integer
				const auto prop = *reinterpret_cast<const UINT64*>(propertyBuffer);
				_ui64tow_s(prop, stackBuffer, cch_StackBuffer, 10);
				return stackBuffer;
			}
		}
	}

    inline std::wstring
    details::Print32BitInteger(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize)
	{
		FAIL_FAST_IF(propertyByteSize != 4);
	    constexpr unsigned cch_StackBuffer = 100;
		wchar_t stackBuffer[cch_StackBuffer]{};

		switch (propertyOutType)
		{
		    case TDH_OUTTYPE_INT: {
		        const auto prop = *reinterpret_cast<const INT32*>(propertyBuffer);
		        _itow_s(prop, stackBuffer, 10);
		        return stackBuffer;
			}

		    case TDH_OUTTYPE_IPV4: {
			    // display as a v4 address
			    ::RtlIpv4AddressToString(reinterpret_cast<const IN_ADDR*>(propertyBuffer), stackBuffer);
			    return stackBuffer;
			}

	        case TDH_OUTTYPE_BOOLEAN: {
		        const auto prop = *reinterpret_cast<const UINT32*>(propertyBuffer);
			    return prop == 0 ? L"false" : L"true";
		    }

		    case TDH_OUTTYPE_HEXINT32:
		    case TDH_OUTTYPE_HEXINT64: // can be passed through pointer types
		    case TDH_OUTTYPE_ERRORCODE:
		    case TDH_OUTTYPE_WIN32ERROR:
		    case TDH_OUTTYPE_NTSTATUS:
		    case TDH_OUTTYPE_HRESULT: {
			    // display as a hex value
		        const auto prop = *reinterpret_cast<const UINT32*>(propertyBuffer);
			    _ultow_s(prop, stackBuffer, 16);
			    std::wstring wsData = L"0x";
			    wsData += stackBuffer;
			    return wsData;
		    }

		    default: {
			    // for any other 32-bit output type, display as an unsigned int
		        const auto prop = *reinterpret_cast<const UINT32*>(propertyBuffer);
			    _ultow_s(prop, stackBuffer, 10);
			    return stackBuffer;
			}
		}
	}

    inline std::wstring
    details::Print16BitInteger(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize)
	{
		FAIL_FAST_IF(propertyByteSize != 2);
	    constexpr unsigned cch_StackBuffer = 100;
		wchar_t stackBuffer[cch_StackBuffer]{};

	    switch (propertyOutType)
		{
		case TDH_OUTTYPE_SHORT: {
			const auto prop = *reinterpret_cast<const INT16*>(propertyBuffer);
			_itow_s(prop, stackBuffer, 10);
			return stackBuffer;
		}

	    case TDH_OUTTYPE_PORT: {
			const auto prop = *reinterpret_cast<const UINT16*>(propertyBuffer);
			_itow_s(ntohs(prop), stackBuffer, 10);
			return stackBuffer;
		}

	    case TDH_OUTTYPE_HEXINT16: {
			const auto prop = *reinterpret_cast<const UINT16*>(propertyBuffer);
			_itow_s(prop, stackBuffer, 16);
			return std::wstring(L"0x") + stackBuffer;
		}

	    case TDH_OUTTYPE_BOOLEAN: {
		    const auto prop = *reinterpret_cast<const UINT16*>(propertyBuffer);
			return prop == 0 ? L"false" : L"true";
		}

	    default: {
			// for any other 16-bit output type, display as an unsigned short
			const auto prop = *reinterpret_cast<const UINT16*>(propertyBuffer);
			_itow_s(prop, stackBuffer, 10);
			return stackBuffer;
		}
	    }
	}

	inline std::wstring
	details::Print8BitInteger(_TDH_OUT_TYPE propertyOutType, const BYTE* propertyBuffer, ULONG propertyByteSize)
    {
		FAIL_FAST_IF(propertyByteSize != 1);
	    constexpr unsigned cch_StackBuffer = 100;
		wchar_t stackBuffer[cch_StackBuffer]{};

	    switch (propertyOutType)
		{
		case TDH_OUTTYPE_BYTE: {
			const CHAR prop = *propertyBuffer;
			_itow_s(prop, stackBuffer, 10);
			return stackBuffer;
		}

	    case TDH_OUTTYPE_HEXINT8: {
			const UCHAR prop = *propertyBuffer;
			_itow_s(prop, stackBuffer, 16);
			return std::wstring(L"0x") + stackBuffer;
		}

	    case TDH_OUTTYPE_BOOLEAN: {
			const UCHAR prop = *propertyBuffer;
			return prop == 0 ? L"false" : L"true";
		}

	    default: {
			// for any other 16-bit output type, display as an unsigned short
			const UCHAR prop = *propertyBuffer;
			_itow_s(prop, stackBuffer, 10);
			return stackBuffer;
		}
	    }
	}
} // namespace ctl

#pragma warning(pop)
