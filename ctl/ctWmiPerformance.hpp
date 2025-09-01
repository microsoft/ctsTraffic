/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// ReSharper disable CppInconsistentNaming
#pragma once

// cpp headers
#include <iterator>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <string>
#include <algorithm>
// os headers
#include <Windows.h>
#include <objbase.h>
#include <oleauto.h>
// ctl headers
#include <ctString.hpp>
#include <ctWmiInitialize.hpp>
// wil headers always included last
#include <wil/stl.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

// Concepts for this class:
// - WMI Classes expose performance counters through hi-performance WMI interfaces
// - ctWmiPerformanceCounter exposes one counter within one WMI performance counter class
// - Every performance counter object contains a 'Name' key field, uniquely identifying a 'set' of data points for that counter
// - Counters are 'snapped' every one second, with the timeslot tracked with the data
//
// ctWmiPerformanceCounter is exposed to the user through factory functions defined per-counter class (ctShared<class>PerfCounter)
// - the factory functions takes in the counter name that the user wants to capture data for
// - the factory function has a template type matching the data type of the counter data for that counter name
//
// Internally, the factory function instantiates a ctWmiPerformanceCounterImpl:
// - has 3 template arguments:
// 1. The IWbem* interface used to enumerate instances of this performance class (either IWbemHiPerfEnum or IWbemClassObject)
// 2. The IWbem* interface used to access data in perf instances of this performance class (either IWbemObjectAccess or IWbemClassObject)
// 3. The data type of the values for the counter name being recorded
// - has 2 function arguments
// 1. The string value of the WMI class to be used
// 2. The string value of the counter name to be recorded
//
// Methods exposed publicly off of ctWmiPerformanceCounter:
// - add_filter(): allows the caller to only capture instances which match the parameter/value combination for that object
// - reference_range() : takes an Instance Name by which to return values
// -- returns begin/end iterators to reference the data
//
// ctWmiPerformanceCounter populates data by invoking a pure virtual function (update_counter_data) every one second.
// - update_counter_data takes a boolean parameter: true will invoke the virtual function to update the data, false will clear the data.
// That pure virtual function (ctWmiPerformanceCounterImpl::update_counter_data) refreshes its counter (through its template accessor interface)
// - then iterates through each instance recorded for that counter and invokes add_instance() in the base class.
//
// add_instance() takes a WMI* of the Accessor type: if that instance wasn't explicitly filtered out (through an instance_filter object),
// - it looks to see if this is a new instance or if we have already been tracking that instance
// - if new, we create a new ctWmiPerformanceCounterData object and add it to our counter_data
// - if not new, we just add this object to the counter_data object that we already created
//
// There are 2 possible sets of WMI interfaces to access and enumerate performance data, these are defined in ctWmiPerformanceDataAccessor
// - this is instantiated in ctWmiPerformanceCounterImpl as it knows the Access and Enum template types
//
// ctWmiPerformanceCounterData encapsulates the data points for one instance of one counter.
// - exposes match() taking a string to check if it matches the instance it contains
// - exposes add() taking both types of Access objects + a ULONGLONG time parameter to retrieve the data and add it to the internal map

namespace ctl
{
	enum class ctWmiPerformanceCollectionType : std::uint8_t
	{
		Detailed,
		MeanOnly,
		FirstLast
	};

	inline bool operator ==(const wil::unique_variant& rhs, const wil::unique_variant& lhs) noexcept
	{
		if (rhs.vt == VT_NULL || lhs.vt == VT_NULL)
		{
			return rhs.vt == VT_NULL && lhs.vt == VT_NULL;
		}

		if (rhs.vt == VT_EMPTY || lhs.vt == VT_EMPTY)
		{
			return rhs.vt == VT_EMPTY && lhs.vt == VT_EMPTY;
		}

		if (rhs.vt == VT_BSTR || lhs.vt == VT_BSTR)
		{
			if (rhs.vt == VT_BSTR && lhs.vt == VT_BSTR)
			{
				return 0 == _wcsicmp(rhs.bstrVal, lhs.bstrVal);
			}
			return false;
		}

		if (rhs.vt == VT_DATE || lhs.vt == VT_DATE)
		{
			if (rhs.vt == VT_DATE && lhs.vt == VT_DATE)
			{
				return rhs.date == lhs.date; // NOLINT(clang-diagnostic-float-equal)
			}
			return false;
		}

		if (rhs.vt == VT_BOOL || lhs.vt == VT_BOOL)
		{
			if (rhs.vt == VT_BOOL && lhs.vt == VT_BOOL)
			{
				return rhs.boolVal == lhs.boolVal;
			}
			return false;
		}

		//
		// intentionally not supporting comparing floating-point types
		// - it's not going to provide a correct value
		// - the proper comparison should be < or  >
		//
		if (rhs.vt == VT_R4 || lhs.vt == VT_R4 ||
			rhs.vt == VT_R8 || lhs.vt == VT_R8)
		{
			FAIL_FAST_MSG("Not making equality comparisons on floating-point numbers");
		}

		//
		// Comparing integer types - not tightly enforcing type by default
		//
		uint32_t rhsInteger = 0;
		switch (rhs.vt)
		{
		case VT_I1:
			rhsInteger += rhs.cVal;
			break;
		case VT_UI1:
			rhsInteger += rhs.bVal;
			break;
		case VT_I2:
			rhsInteger += rhs.iVal;
			break;
		case VT_UI2:
			rhsInteger += rhs.uiVal;
			break;
		case VT_I4:
			rhsInteger += rhs.lVal;
			break;
		case VT_UI4:
			rhsInteger += rhs.ulVal;
			break;
		case VT_INT:
			rhsInteger += rhs.intVal;
			break;
		case VT_UINT:
			rhsInteger += rhs.uintVal;
			break;
		default:
			return false;
		}
		uint32_t lhsInteger = 0;
		switch (lhs.vt)
		{
		case VT_I1:
			lhsInteger += lhs.cVal;
			break;
		case VT_UI1:
			lhsInteger += lhs.bVal;
			break;
		case VT_I2:
			lhsInteger += lhs.iVal;
			break;
		case VT_UI2:
			lhsInteger += lhs.uiVal;
			break;
		case VT_I4:
			lhsInteger += lhs.lVal;
			break;
		case VT_UI4:
			lhsInteger += lhs.ulVal;
			break;
		case VT_INT:
			lhsInteger += lhs.intVal;
			break;
		case VT_UINT:
			lhsInteger += lhs.uintVal;
			break;
		default:
			return false;
		}

		return lhsInteger == rhsInteger;
	}

	inline bool operator !=(const wil::unique_variant& rhs, const wil::unique_variant& lhs) noexcept
	{
		return !(rhs == lhs);
	}

	namespace details
	{
		inline wil::unique_variant ReadCounterFromWbemObjectAccess(
			_In_ IWbemObjectAccess* instance,
			_In_ PCWSTR counterName)
		{
			LONG propertyHandle{};
			CIMTYPE propertyType{};
			THROW_IF_FAILED(instance->GetPropertyHandle(counterName, &propertyType, &propertyHandle));

			wil::unique_variant currentValue;
			switch (propertyType)
			{
			case CIM_SINT32:
			case CIM_UINT32:
			{
				ULONG value{};
				THROW_IF_FAILED(instance->ReadDWORD(propertyHandle, &value));
				currentValue = ctWmiMakeVariant(value);
				break;
			}

			case CIM_SINT64:
			case CIM_UINT64:
			{
				ULONGLONG value{};
				THROW_IF_FAILED(instance->ReadQWORD(propertyHandle, &value));
				currentValue = ctWmiMakeVariant(value);
				break;
			}

			case CIM_STRING:
			{
				constexpr long cimStringDefaultSize = 64;
				std::wstring value(cimStringDefaultSize, L'\0');
				long valueSize = cimStringDefaultSize * sizeof(WCHAR);
				long returnedSize{};
				auto hr = instance->ReadPropertyValue(
					propertyHandle,
					valueSize,
					&returnedSize,
					reinterpret_cast<BYTE*>(value.data()));
				if (WBEM_E_BUFFER_TOO_SMALL == hr)
				{
					valueSize = returnedSize;
					value.resize(valueSize / sizeof(WCHAR));
					hr = instance->ReadPropertyValue(
						propertyHandle,
						valueSize,
						&returnedSize,
						reinterpret_cast<BYTE*>(value.data()));
				}
				THROW_IF_FAILED(hr);
				currentValue = ctWmiMakeVariant(value.c_str());
				break;
			}

			default:
				THROW_HR_MSG(
					HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
					"ctWmiPerformance only supports data of type INT32, INT64, and BSTR: counter %ws is of type %u",
					counterName, static_cast<unsigned>(propertyType));
			}

			return currentValue;
		}

		// template class ctWmiPerformanceDataAccessor
		//
		// Refreshes performance data for the target specified based off the classname
		// - and the template types specified [the below are the only types supported]:
		//
		// Note: caller *MUST* provide thread safety
		//       this class is not providing locking at this level
		//
		// Note: callers *MUST* guarantee connections with the WMI service stay connected
		//       for the lifetime of this object [e.g. guaranteed ctWmiService is instantiated]
		//
		// Note: callers *MUST* guarantee that COM is CoInitialized on this thread before calling
		//
		// Note: the ctWmiPerformance class *will* retain WMI service instance
		//       it's recommended to guarantee it stays alive
		//
		// Template typename options:
		//
		// typename TEnum == IWbemHiPerfEnum
		// - encapsulates the processing of IWbemHiPerfEnum instances of type _classname
		//
		// typename TEnum == IWbemClassObject
		// - encapsulates the processing of a single refreshable IWbemClassObject of type _classname
		//
		// typename TAccess == IWbemObjectAccess
		// - begin/end return an iterator to a vector<IWbemObjectAccess> of refreshed perf data
		// - Note: could be N number of instances
		//
		// typename TAccess == IWbemClassObject
		// - begin/end return an iterator to a vector<IWbemClassObject> of refreshed perf data
		// - Note: will only ever be a single instance
		template <typename TEnum, typename TAccess>
		class ctWmiPerformanceDataAccessor
		{
		public:
			using ctAccessIterator = typename std::vector<TAccess*>::const_iterator;

			ctWmiPerformanceDataAccessor(
				ctWmiService wmi,
				const wil::com_ptr<IWbemConfigureRefresher>& config,
				_In_ PCWSTR classname);

			~ctWmiPerformanceDataAccessor() noexcept
			{
				clear();
			}

			// refreshes internal data with the latest performance data
			void refresh();

			[[nodiscard]] ctAccessIterator begin() const noexcept
			{
				return m_accessorObjects.cbegin();
			}

			[[nodiscard]] ctAccessIterator end() const noexcept
			{
				return m_accessorObjects.cend();
			}

			// non-copyable
			ctWmiPerformanceDataAccessor(const ctWmiPerformanceDataAccessor&) = delete;
			ctWmiPerformanceDataAccessor& operator=(const ctWmiPerformanceDataAccessor&) = delete;

			// movable
			ctWmiPerformanceDataAccessor(ctWmiPerformanceDataAccessor&& rhs) noexcept :
				m_enumerationObject(std::move(rhs.m_enumerationObject)),
				m_accessorObjects(std::move(rhs.m_accessorObjects)),
				m_currentIterator(std::move(rhs.m_currentIterator))
			{
				// since accessor_objects is storing raw pointers, manually clear out the rhs object,
				// so they won't be double-deleted
				rhs.m_accessorObjects.clear();
				rhs.m_currentIterator = rhs.m_accessorObjects.end();
			}

			ctWmiPerformanceDataAccessor& operator=(ctWmiPerformanceDataAccessor&& rhs) noexcept
			{
				m_enumerationObject = std::move(rhs.m_enumerationObject);
				m_accessorObjects = std::move(rhs.m_accessorObjects);
				m_currentIterator = std::move(rhs.m_currentIterator);
				// since accessor_objects is storing raw pointers, manually clear out the rhs object,
				// so they won't be double-deleted
				rhs.m_accessorObjects.clear();
				rhs.m_currentIterator = rhs.m_accessorObjects.end();
				return *this;
			}

		private:
			wil::com_ptr<TEnum> m_enumerationObject;
			// TAccess pointers are returned through enumeration_object::GetObjects, reusing the same vector for each refresh call
			std::vector<TAccess*> m_accessorObjects;
			ctAccessIterator m_currentIterator;

			void clear() noexcept;
		};

		inline ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::ctWmiPerformanceDataAccessor(
			ctWmiService wmi, const wil::com_ptr<IWbemConfigureRefresher>& config, _In_ PCWSTR classname) :
			m_currentIterator(m_accessorObjects.end())
		{
			LONG lid{};
			THROW_IF_FAILED(config->AddEnum(wmi.get(), classname, 0, nullptr, m_enumerationObject.addressof(), &lid));
		}

		inline ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::ctWmiPerformanceDataAccessor(
			ctWmiService wmi, const wil::com_ptr<IWbemConfigureRefresher>& config, _In_ PCWSTR classname) :
			m_currentIterator(m_accessorObjects.end())
		{
			ctWmiEnumerate enumInstances(wmi);
			enumInstances.query(wil::str_printf<std::wstring>(L"SELECT * FROM %ws", classname).c_str());
			if (enumInstances.begin() == enumInstances.end())
			{
				THROW_HR_MSG(
					HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
					"Failed to refresh a static instances of the WMI class %ws", classname);
			}

			const auto instance = *enumInstances.begin();
			LONG lid{};
			THROW_IF_FAILED(
				config->AddObjectByTemplate(
					wmi.get(),
					instance.get_instance().get(),
					0,
					nullptr,
					m_enumerationObject.addressof(),
					&lid));

			// setting the raw pointer in the access vector to behave with the iterator
			m_accessorObjects.push_back(m_enumerationObject.get());
		}

		template <>
		inline void ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::refresh()
		{
			clear();

			ULONG objectsReturned = 0;
			auto hr = m_enumerationObject->GetObjects(
				0,
				static_cast<ULONG>(m_accessorObjects.size()),
				m_accessorObjects.empty() ? nullptr : m_accessorObjects.data(),
				&objectsReturned);

			if (WBEM_E_BUFFER_TOO_SMALL == hr)
			{
				m_accessorObjects.resize(objectsReturned);
				hr = m_enumerationObject->GetObjects(
					0,
					static_cast<ULONG>(m_accessorObjects.size()),
					m_accessorObjects.data(),
					&objectsReturned);
			}
			THROW_IF_FAILED(hr);

			m_accessorObjects.resize(objectsReturned);
			m_currentIterator = m_accessorObjects.begin();
		}

		template <>
		inline void ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::refresh()
		{
			// the underlying IWbemClassObject is already refreshed
			// accessor_objects will only ever have a singe instance
			FAIL_FAST_IF_MSG(
				m_accessorObjects.size() != 1,
				"ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>: for IWbemClassObject performance classes there can only ever have the single instance being tracked - instead has %Iu",
				m_accessorObjects.size());

			m_currentIterator = m_accessorObjects.begin();
		}

		template <>
		inline void ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::clear() noexcept
		{
			for (IWbemObjectAccess* object : m_accessorObjects)
			{
				object->Release();
			}
			m_accessorObjects.clear();
			m_currentIterator = m_accessorObjects.end();
		}

		template <>
		inline void ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::clear() noexcept
		{
			m_currentIterator = m_accessorObjects.end();
		}

		// Structure to track the performance data for each property desired for the instance being tracked
		//
		// typename T : the data type of the counter to be stored
		//
		// Note: callers *MUST* guarantee connections with the WMI service stay connected
		//       for the lifetime of this object [e.g. guaranteed ctWmiService is instantiated]
		// Note: callers *MUST* guarantee that COM is CoInitialized on this thread before calling
		// Note: the ctWmiPerformance class *will* retain WMI service instance
		//       it's recommended to guarantee it stays alive
		template <typename T>
		class ctWmiPerformanceCounterData
		{
		public:
			ctWmiPerformanceCounterData(
				const ctWmiPerformanceCollectionType collectionType,
				_In_ IWbemObjectAccess* instance,
				_In_ PCWSTR counter) :
				m_collectionType(collectionType),
				m_instanceName(V_BSTR(ReadCounterFromWbemObjectAccess(instance, L"Name").addressof())),
				m_counterName(counter)
			{
			}

			ctWmiPerformanceCounterData(
				const ctWmiPerformanceCollectionType collectionType,
				_In_ IWbemClassObject* instance,
				_In_ PCWSTR counter) :
				m_collectionType(collectionType),
				m_counterName(counter)
			{
				wil::unique_variant value;
				THROW_IF_FAILED(instance->Get(L"Name", 0, value.addressof(), nullptr, nullptr));

				// Name is expected to be NULL in this case
				// - since IWbemClassObject is expected to be a single instance
				THROW_HR_IF_MSG(
					HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
					value.vt != VT_NULL,
					"ctWmiPerformanceCounterData was given an IWbemClassObject to track that had a non-null 'Name' key field ['%ws']. Expected to be a NULL key field as to only support single-instances",
					V_BSTR(value.addressof()));
			}

			~ctWmiPerformanceCounterData() noexcept = default;

			// instanceName == nullptr means match everything
			bool match(_In_opt_ PCWSTR instanceName) const
			{
				if (!instanceName)
				{
					return true;
				}
				if (m_instanceName.empty())
				{
					return nullptr == instanceName;
				}
				return ctString::iordinal_equals(instanceName, m_instanceName);
			}

			void add(_In_ IWbemObjectAccess* instance)
			{
				T instanceData{};
				if (ctWmiReadFromVariant(
					ReadCounterFromWbemObjectAccess(instance, m_counterName.c_str()).addressof(),
					&instanceData))
				{
					add_data(instanceData);
				}
			}

			void add(_In_ IWbemClassObject* instance)
			{
				wil::unique_variant value;
				THROW_IF_FAILED(instance->Get(m_counterName.c_str(), 0, value.addressof(), nullptr, nullptr));
				// the instance could return null if there's no data
				if (value.vt != VT_NULL)
				{
					T instanceData;
					if (ctWmiReadFromVariant(value.addressof(), &instanceData))
					{
						add_data(instanceData);
					}
				}
			}

			typename std::vector<T>::const_iterator begin() noexcept
			{
				const auto lock = m_guardData.lock();
				return access_begin();
			}

			typename std::vector<T>::const_iterator end() const noexcept
			{
				const auto lock = m_guardData.lock();
				return access_end();
			}

			size_t count() noexcept
			{
				const auto lock = m_guardData.lock();
				return access_end() - access_begin();
			}

			void clear() noexcept
			{
				const auto lock = m_guardData.lock();
				m_counterData.clear();
				m_counterSum = 0;
			}

			// non-copyable
			ctWmiPerformanceCounterData(const ctWmiPerformanceCounterData&) = delete;
			ctWmiPerformanceCounterData& operator=(const ctWmiPerformanceCounterData&) = delete;

			// not movable
			ctWmiPerformanceCounterData(ctWmiPerformanceCounterData&&) = delete;
			ctWmiPerformanceCounterData& operator=(ctWmiPerformanceCounterData&&) = delete;

		private:
			mutable wil::critical_section m_guardData{ 500 };
			const ctWmiPerformanceCollectionType m_collectionType = ctWmiPerformanceCollectionType::Detailed;
			const std::wstring m_instanceName;
			const std::wstring m_counterName;
			std::vector<T> m_counterData;
			uint64_t m_counterSum = 0;

			void add_data(const T& instanceData)
			{
				const auto lock = m_guardData.lock();
				switch (m_collectionType)
				{
				case ctWmiPerformanceCollectionType::Detailed:
					m_counterData.push_back(instanceData);
					break;

				case ctWmiPerformanceCollectionType::MeanOnly:
					// vector is formatted as:
					// [0] == count
					// [1] == min
					// [2] == max
					// [3] == mean
					if (m_counterData.empty())
					{
						m_counterData.push_back(1);
						m_counterData.push_back(instanceData);
						m_counterData.push_back(instanceData);
						m_counterData.push_back(0);
					}
					else
					{
						++m_counterData[0];
						if (instanceData < m_counterData[1])
						{
							m_counterData[1] = instanceData;
						}
						if (instanceData > m_counterData[2])
						{
							m_counterData[2] = instanceData;
						}
					}

					m_counterSum += instanceData;
					break;

				case ctWmiPerformanceCollectionType::FirstLast:
					// the first data point write both min and max
					// [0] == count
					// [1] == first
					// [2] == last
					if (m_counterData.empty())
					{
						m_counterData.push_back(1);
						m_counterData.push_back(instanceData);
						m_counterData.push_back(instanceData);
					}
					else
					{
						++m_counterData[0];
						m_counterData[2] = instanceData;
					}
					break;

				default:
					FAIL_FAST_MSG(
						"Unknown ctWmiPerformanceCollectionType (%d)", m_collectionType);
				}
			}

			typename std::vector<T>::const_iterator access_begin() noexcept
			{
				const auto lock = m_guardData.lock();
				// when accessing data, calculate the mean
				if (ctWmiPerformanceCollectionType::MeanOnly == m_collectionType)
				{
					m_counterData[3] = static_cast<T>(m_counterSum / m_counterData[0]);
				}
				return m_counterData.cbegin();
			}

			typename std::vector<T>::const_iterator access_end() const noexcept
			{
				const auto lock = m_guardData.lock();
				return m_counterData.cend();
			}
		};

		inline wil::unique_variant ctQueryInstanceName(_In_ IWbemObjectAccess* instance)
		{
			return ReadCounterFromWbemObjectAccess(instance, L"Name");
		}

		inline wil::unique_variant ctQueryInstanceName(_In_ IWbemClassObject* instance)
		{
			wil::unique_variant value;
			THROW_IF_FAILED(instance->Get(L"Name", 0, value.addressof(), nullptr, nullptr));
			return value;
		}

		// type for the callback implemented in all ctWmiPerformanceCounter classes
		enum class CallbackAction : std::uint8_t
		{
			Start,
			Stop,
			Update,
			Clear
		};

		using ctWmiPerformanceCallback = std::function<void(CallbackAction)>;
	} // unnamed namespace

	// forward-declaration to reference ctWmiPerformance
	class ctWmiPerformance;

	// class ctWmiPerformanceCounter
	// - The abstract base class contains the WMI-specific code which all templated instances will derive from
	// - Using public inheritance + protected members over composition as we need a common type which we can pass to
	//   ctWmiPerformance
	// - Exposes the iterator class for users to traverse the data points gathered
	//
	// Note: callers *MUST* guarantee connections with the WMI service stay connected
	//       for the lifetime of this object [e.g. guaranteed ctWmiService is instantiated]
	// Note: callers *MUST* guarantee that COM is CoInitialized on this thread before calling
	// Note: the ctWmiPerformance class *will* retain WMI service instance
	//       it's recommended to guarantee it stays alive
	template <typename T>
	class ctWmiPerformanceCounter
	{
	public:
		// iterates across *time-slices* captured over from ctWmiPerformance
		class iterator
		{
		public:
			// iterator_traits - allows <algorithm> functions to be used
			using iterator_category = std::forward_iterator_tag;
			using value_type = T;
			using difference_type = size_t;
			using pointer = T*;
			using reference = T&;

			explicit iterator(typename std::vector<T>::const_iterator&& instance) noexcept :
				m_current(std::move(instance)), m_isEmpty(false)
			{
			}

			iterator() = default;
			~iterator() noexcept = default;

			iterator(iterator&& i) noexcept :
				m_current(std::move(i.m_current)),
				m_isEmpty(i.m_isEmpty)
			{
			}

			iterator& operator =(iterator&& i) noexcept
			{
				m_current = std::move(i.m_current);
				m_isEmpty = i.m_isEmpty;
				return *this;
			}

			iterator(const iterator& i) noexcept :
				m_current(i.m_current),
				m_isEmpty(i.m_isEmpty)
			{
			}

			iterator& operator =(const iterator& i) noexcept // NOLINT(cert-oop54-cpp)
			{
				iterator localCopy(i);
				*this = std::move(localCopy);
				return *this;
			}

			const T& operator*() const
			{
				if (m_isEmpty)
				{
					throw std::runtime_error(
						"ctWmiPerformanceCounter::iterator : dereferencing an iterator referencing an empty container");
				}
				return *m_current;
			}

			bool operator==(const iterator& iter) const noexcept
			{
				if (m_isEmpty || iter.m_isEmpty)
				{
					return m_isEmpty == iter.m_isEmpty;
				}
				return m_current == iter.m_current;
			}

			bool operator!=(const iterator& iter) const noexcept
			{
				return !(*this == iter);
			}

			// pre-increment
			iterator& operator++()
			{
				if (m_isEmpty)
				{
					throw std::runtime_error(
						"ctWmiPerformanceCounter::iterator : pre-incrementing an iterator referencing an empty container");
				}
				++m_current;
				return *this;
			}

			// post-increment
			iterator operator++(int)
			{
				if (m_isEmpty)
				{
					throw std::runtime_error(
						"ctWmiPerformanceCounter::iterator : post-incrementing an iterator referencing an empty container");
				}
				iterator temp(*this);
				++m_current;
				return temp;
			}

			// increment by integer
			iterator& operator+=(size_t inc)
			{
				if (m_isEmpty)
				{
					throw std::runtime_error(
						"ctWmiPerformanceCounter::iterator : post-incrementing an iterator referencing an empty container");
				}
				for (size_t loop = 0; loop < inc; ++loop)
				{
					++m_current;
				}
				return *this;
			}

		private:
			typename std::vector<T>::const_iterator m_current;
			bool m_isEmpty = true;
		};

		ctWmiPerformanceCounter(_In_ PCWSTR counterName, const ctWmiPerformanceCollectionType collectionType) :
			m_collectionType(collectionType),
			m_counterName(counterName)
		{
			m_refresher = wil::CoCreateInstance<WbemRefresher, IWbemRefresher>();
			m_configRefresher = m_refresher.query<IWbemConfigureRefresher>();
		}

		virtual ~ctWmiPerformanceCounter() noexcept = default;

		ctWmiPerformanceCounter(const ctWmiPerformanceCounter&) = delete;
		ctWmiPerformanceCounter& operator=(const ctWmiPerformanceCounter&) = delete;
		ctWmiPerformanceCounter(ctWmiPerformanceCounter&&) = delete;
		ctWmiPerformanceCounter& operator=(ctWmiPerformanceCounter&&) = delete;

		//
		// *not* thread-safe: caller must guarantee sequential access to add_filter()
		//
		template <typename V>
		void add_filter(_In_ PCWSTR counterName, V propertyValue)
		{
			FAIL_FAST_IF_MSG(
				!m_dataStopped,
				"ctWmiPerformanceCounter: must call stop_all_counters on the ctWmiPerformance class containing this counter");
			m_instanceFilter.emplace_back(counterName, std::move(ctWmiMakeVariant(propertyValue)));
		}

		//
		// returns a pair<begin,end> of iterators that exposes data for each time-slice
		// - static classes will have a null instance name
		//
		std::pair<iterator, iterator> reference_range(_In_opt_ PCWSTR instanceName = nullptr)
		{
			FAIL_FAST_IF_MSG(
				!m_dataStopped,
				"ctWmiPerformanceCounter: must call stop_all_counters on the ctWmiPerformance class containing this counter");

			const auto lock = m_guardCounterData.lock();
			const auto foundInstance = std::ranges::find_if(
				m_counterData,
				[&](const auto& instance)
				{
					return instance->match(instanceName);
				});
			if (std::end(m_counterData) == foundInstance)
			{
				// nothing matching that instance name
				// return the end iterator (default constructor == end)
				return std::pair<iterator, iterator>(iterator(), iterator());
			}

			const std::unique_ptr<details::ctWmiPerformanceCounterData<T>>& instanceReference = *foundInstance;
			return std::pair<iterator, iterator>(instanceReference->begin(), instanceReference->end());
		}

	private:
		//
		// private structure to track the 'filter' which instances to track
		//
		struct ctWmiPerformanceInstanceFilter
		{
			const std::wstring m_counterName;
			const wil::unique_variant m_propertyValue;

			ctWmiPerformanceInstanceFilter(_In_ PCWSTR counterName, wil::unique_variant&& propertyValue) :
				m_counterName(counterName),
				m_propertyValue(std::move(propertyValue))
			{
			}

			~ctWmiPerformanceInstanceFilter() = default;

			ctWmiPerformanceInstanceFilter(const ctWmiPerformanceInstanceFilter& rhs) = delete;
			ctWmiPerformanceInstanceFilter& operator=(const ctWmiPerformanceInstanceFilter& rhs) = delete;

			// must const-cast to allow the const objects to be movable
			// this is safe as these are const only for the accessors of this object
			ctWmiPerformanceInstanceFilter(ctWmiPerformanceInstanceFilter&& rhs) noexcept :
				m_counterName(std::move(*const_cast<std::wstring*>(&rhs.m_counterName))),
				m_propertyValue(std::move(*const_cast<wil::unique_variant*>(&rhs.m_propertyValue)))
			{
			}

			ctWmiPerformanceInstanceFilter& operator=(ctWmiPerformanceInstanceFilter&&) noexcept = delete;

			bool operator==(_In_ IWbemObjectAccess* instance) const
			{
				return m_propertyValue == details::ReadCounterFromWbemObjectAccess(instance, m_counterName.c_str());
			}

			bool operator!=(_In_ IWbemObjectAccess* instance) const
			{
				return !(*this == instance);
			}

			bool operator==(_In_ IWbemClassObject* instance) const
			{
				wil::unique_variant value;
				THROW_IF_FAILED(instance->Get(m_counterName.c_str(), 0, value.addressof(), nullptr, nullptr));
				// if the filter currently doesn't match anything we have, return not equal
				if (value.vt == VT_NULL)
				{
					return false;
				}

				FAIL_FAST_IF_MSG(
					value.vt != m_propertyValue.vt,
					"VARIANT types do not match to make a comparison : Counter name '%ws', retrieved type '%u', expected type '%u'",
					m_counterName.c_str(), value.vt, m_propertyValue.vt);

				return m_propertyValue == value;
			}

			bool operator!=(_In_ IWbemClassObject* instance) const
			{
				return !(*this == instance);
			}
		};

		const ctWmiPerformanceCollectionType m_collectionType;
		const std::wstring m_counterName;
		wil::com_ptr<IWbemRefresher> m_refresher;
		wil::com_ptr<IWbemConfigureRefresher> m_configRefresher;
		std::vector<ctWmiPerformanceInstanceFilter> m_instanceFilter;
		// Must lock access to counter_data
		mutable wil::critical_section m_guardCounterData{ 500 };
		std::vector<std::unique_ptr<details::ctWmiPerformanceCounterData<T>>> m_counterData;
		bool m_dataStopped = true;

	protected:
		virtual void update_counter_data() = 0;

		// ctWmiPerformance needs private access to invoke register_callback in the derived type
		friend class ctWmiPerformance;

		details::ctWmiPerformanceCallback register_callback()
		{
			// the callback function must be no-except - it can't leak an exception to the caller
			// as it shouldn't break calling all other callbacks if one happens to fail an update
			return [this](const details::CallbackAction updateData) noexcept
				{
					try
					{
						switch (updateData)
						{
						case details::CallbackAction::Start:
							update_counter_data();
							m_dataStopped = false;
							break;

						case details::CallbackAction::Stop:
							m_dataStopped = true;
							break;

						case details::CallbackAction::Update:
							// only the derived class has appropriate the accessor class to update the data
							update_counter_data();
							break;

						case details::CallbackAction::Clear:
						{
							FAIL_FAST_IF_MSG(
								!m_dataStopped,
								"ctWmiPerformanceCounter: must call stop_all_counters on the ctWmiPerformance class containing this counter");

							const auto lock = m_guardCounterData.lock();
							for (auto& counterData : m_counterData)
							{
								counterData->clear();
							}
							break;
						}
						}
					}
					CATCH_LOG()
						// if failed to update the counter data this pass
						// will try again the next timer callback
				};
		}

		//
		// the derived classes need to use the same refresher object
		//
		wil::com_ptr<IWbemConfigureRefresher> access_refresher() const noexcept
		{
			return m_configRefresher;
		}

		//
		// this function is how one looks to see if the data machines requires knowing how to access the data from that specific WMI class
		// - it's also how to access the data is captured with the TAccess template type
		//
		void add_instance(_In_ IWbemObjectAccess* instance)
		{
			bool fAddData = m_instanceFilter.empty();
			if (!fAddData)
			{
				fAddData = std::any_of(
					std::cbegin(m_instanceFilter),
					std::cend(m_instanceFilter),
					[&](const auto& filter)
					{
						return filter == instance;
					});
			}

			// add the counter data for this instance if:
			// - have no filters [not filtering instances at all]
			// - matches at least one filter
			if (fAddData)
			{
				wil::unique_variant instanceName = details::ctQueryInstanceName(instance);
				// this would be odd but technically possible
				// - if this isn't a static class and this instance currently doesn't have a name
				if (instanceName.vt == VT_NULL)
				{
					return;
				}

				const auto lock = m_guardCounterData.lock();
				const auto trackedInstance = std::ranges::find_if(
					m_counterData,
					[&](const auto& counterData)
					{
						return counterData->match(instanceName.bstrVal);
					});

				// if this instance of this counter is new [new unique instance for this counter]
				// - we must add a new ctWmiPerformanceCounterData to the parent's counter_data vector
				// else
				// - we just add this counter value to the already-tracked instance
				if (trackedInstance == std::end(m_counterData))
				{
					m_counterData.push_back(
						std::make_unique<details::ctWmiPerformanceCounterData<T>>(
							m_collectionType, instance, m_counterName.c_str()));
					(*m_counterData.rbegin())->add(instance);
				}
				else
				{
					(*trackedInstance)->add(instance);
				}
			}
		}

		void add_instance(_In_ IWbemClassObject* instance)
		{
			// static WMI objects have only one instance
			const auto lock = m_guardCounterData.lock();
			if (m_counterData.empty())
			{
				m_counterData.push_back(
					std::make_unique<details::ctWmiPerformanceCounterData<T>>(
						m_collectionType, instance, m_counterName.c_str()));
			}

			m_counterData[0]->add(instance);
		}
	};

	// ctWmiPerformanceCounterImpl
	// - derived from the pure-virtual ctWmiPerformanceCounter class
	//   shares the same data type template typename with the parent class
	//
	// Template typename details:
	//
	// - TEnum: the IWbem* type to refresh the performance data
	// - TAccess: the IWbem* type used to access the performance data once refreshed
	// - TData: the data type of the counter of the class specified in the c'tor
	//
	// Only 2 combinations currently supported:
	// : ctWmiPerformanceCounter<IWbemHiPerfEnum, IWbemObjectAccess, TData>
	//   - refreshes N of instances of a counter
	// : ctWmiPerformanceCounter<IWbemClassObject, IWbemClassObject, TData>
	//   - refreshes a single instance of a counter
	template <typename TEnum, typename TAccess, typename TData>
	class ctWmiPerformanceCounterImpl final : public ctWmiPerformanceCounter<TData>
	{
	public:
		ctWmiPerformanceCounterImpl(
			const ctWmiService& wmi,
			_In_ PCWSTR className,
			_In_ PCWSTR counterName,
			const ctWmiPerformanceCollectionType collectionType) :
			ctWmiPerformanceCounter<TData>(counterName, collectionType),
			m_accessor(wmi, this->access_refresher(), className)
			// must qualify 'this' name lookup to access access_refresher since it's in the base class
		{
		}

		~ctWmiPerformanceCounterImpl() override = default;

		// non-copyable
		ctWmiPerformanceCounterImpl(const ctWmiPerformanceCounterImpl&) = delete;
		ctWmiPerformanceCounterImpl& operator=(const ctWmiPerformanceCounterImpl&) = delete;
		ctWmiPerformanceCounterImpl(ctWmiPerformanceCounterImpl&&) = delete;
		ctWmiPerformanceCounterImpl& operator=(ctWmiPerformanceCounterImpl&&) = delete;

	private:
		//
		// this concrete template class serves to capture the Enum and Access template types
		// - so can instantiate the appropriate accessor object
		details::ctWmiPerformanceDataAccessor<TEnum, TAccess> m_accessor;

		//
		// invoked from the parent class to add data matching any/all filters
		//
		// private function required to be implemented from the abstract base class
		// - concrete class must pass back a function callback for adding data points for the specified counter
		//
		void update_counter_data() override
		{
			const auto coInit = wil::CoInitializeEx();

			// refresh this hi-perf object to get the current values
			// requires the invoker serializes all calls
			m_accessor.refresh();

			// the accessor object exposes begin() / end() to allow access to instances
			// - of the specified hi-performance counter
			for (const auto& instance : m_accessor)
			{
				// must qualify this name lookup to access add_instance since it's in the base class
				this->add_instance(instance);
			}
		}
	};

	// ctWmiPerformance
	//
	// class to register for and collect performance counters
	// - captures counter data into the ctWmiPerformanceCounter objects passed through add()
	//
	// CAUTION:
	// - do not access the ctWmiPerformanceCounter instances while between calling start() and stop()
	// - any iterators returned can be invalidated when more data is added on the next cycle
	class ctWmiPerformance final
	{
	public:
		explicit ctWmiPerformance(ctWmiService wmiService) :
			m_wmiService(std::move(wmiService))
		{
			m_lockedData = std::make_unique<LockedData>();
			m_refresher = wil::CoCreateInstance<WbemRefresher, IWbemRefresher>();
			m_configRefresher = m_refresher.query<IWbemConfigureRefresher>();
		}

		~ctWmiPerformance() noexcept
		{
			// when being destroyed, don't invoke callbacks
			// the counters might be destroyed, and we don't hold a ref on them
			if (m_lockedData)
			{
				auto lock = m_lockedData->m_lock.lock();
				m_lockedData->m_countersStarted = false;
			}
			m_timer.reset();
		}

		template <typename T>
		void add_counter(const std::shared_ptr<ctWmiPerformanceCounter<T>>& perfCounterObject)
		{
			m_callbacks.push_back(perfCounterObject->register_callback());
			auto revertCallback = wil::scope_exit([&]() noexcept
				{
					m_callbacks.pop_back();
				});

			THROW_IF_FAILED(m_configRefresher->AddRefresher(perfCounterObject->m_refresher.get(), 0, nullptr));
			// dismiss scope-guard - successfully added refresher
			revertCallback.release();
		}

		void start_all_counters(uint32_t interval)
		{
			if (!m_timer)
			{
				m_timer.reset(CreateThreadpoolTimer(TimerCallback, this, nullptr));
				THROW_LAST_ERROR_IF(!m_timer);
			}

			for (auto& callback : m_callbacks)
			{
				callback(details::CallbackAction::Start);
			}

			{
				auto lock = m_lockedData->m_lock.lock();
				m_lockedData->m_countersStarted = true;
				m_timerIntervalMs = interval;
				FILETIME relativeTimeout = wil::filetime::from_int64(
					-1 * wil::filetime_duration::one_millisecond * m_timerIntervalMs);
				SetThreadpoolTimer(m_timer.get(), &relativeTimeout, 0, 0);
			}
		}

		// no-throw / no-fail
		void stop_all_counters() noexcept
		{
			{
				auto lock = m_lockedData->m_lock.lock();
				m_lockedData->m_countersStarted = false;
			}

			m_timer.reset();

			for (auto& callback : m_callbacks)
			{
				callback(details::CallbackAction::Stop);
			}
		}

		// no-throw / no-fail
		void clear_counter_data() const noexcept
		{
			for (const auto& callback : m_callbacks)
			{
				callback(details::CallbackAction::Clear);
			}
		}

		void reset_counters()
		{
			m_callbacks.clear();

			// release this Refresher and ConfigRefresher, so future counters will be added cleanly
			m_refresher = wil::CoCreateInstance<WbemRefresher, IWbemRefresher>();
			m_configRefresher = m_refresher.query<IWbemConfigureRefresher>();
		}

		// non-copyable
		ctWmiPerformance(const ctWmiPerformance&) = delete;
		ctWmiPerformance& operator=(const ctWmiPerformance&) = delete;
		ctWmiPerformance& operator=(ctWmiPerformance&& rhs) noexcept = delete;

		// movable
		ctWmiPerformance(ctWmiPerformance&& rhs) noexcept = default;

	private:
		ctWmiService m_wmiService;
		wil::com_ptr<IWbemRefresher> m_refresher;
		wil::com_ptr<IWbemConfigureRefresher> m_configRefresher;
		// for each interval, callback each of the registered aggregators
		std::vector<details::ctWmiPerformanceCallback> m_callbacks;
		uint32_t m_timerIntervalMs{};
		// timer to fire to indicate when to Refresh the data
		// declare last to guarantee will be destroyed first
		wil::unique_threadpool_timer m_timer;

		// must dynamically allocate this as the critical_section isn't movable
		// and the ctWmiPerformance objects must be movable
		struct LockedData
		{
			wil::critical_section m_lock{ 500 };
			bool m_countersStarted = false;
		};

		std::unique_ptr<LockedData> m_lockedData;

		static void NTAPI TimerCallback(PTP_CALLBACK_INSTANCE, PVOID pContext, PTP_TIMER) noexcept
		{
			const auto* pThis = static_cast<ctWmiPerformance*>(pContext);
			try
			{
				// must guarantee COM is initialized on this thread
				const auto com = wil::CoInitializeEx();
				pThis->m_refresher->Refresh(0);
				for (const auto& callback : pThis->m_callbacks)
				{
					callback(details::CallbackAction::Update);
				}
			}
			catch (...)
			{
				// best-effort to update the caller with the data from this time-slice
			}

			auto lock = pThis->m_lockedData->m_lock.lock();
			if (pThis->m_lockedData->m_countersStarted)
			{
				FILETIME relativeTimeout = wil::filetime::from_int64(
					-1 * wil::filetime_duration::one_millisecond * pThis->m_timerIntervalMs);
				SetThreadpoolTimer(pThis->m_timer.get(), &relativeTimeout, 0, 0);
			}
		}
	};


	enum class ctWmiEnumClassType : std::uint8_t
	{
		Uninitialized,
		// created with ctMakeStaticPerfCounter
		Static,
		// created with ctMakeInstancePerfCounter
		Instance
	};

	enum class ctWmiEnumClassName : std::uint8_t
	{
		Uninitialized,
		Process,
		Processor,
		Memory,
		NetworkAdapter,
		NetworkInterface,
		TcpipDiagnostics,
		TcpipIpv4,
		TcpipIpv6,
		TcpipTcpv4,
		TcpipTcpv6,
		TcpipUdpv4,
		TcpipUdpv6,
		WinsockBsp,
		WfpFilter,
		WfpFilterCount,
	};

	struct ctWmiPerformanceCounterProperties
	{
		const ctWmiEnumClassType m_classType = ctWmiEnumClassType::Uninitialized;
		const ctWmiEnumClassName m_className = ctWmiEnumClassName::Uninitialized;
		const wchar_t* m_providerName = nullptr;

		const uint32_t m_ulongFieldNameCount = 0;
		const wchar_t** m_ulongFieldNames = nullptr;

		const uint32_t m_ulonglongFieldNameCount = 0;
		const wchar_t** m_ulonglongFieldNames = nullptr;

		const uint32_t m_stringFieldNameCount = 0;
		const wchar_t** m_stringFieldNames = nullptr;

		template <typename T>
		bool PropertyNameExists(_In_ PCWSTR name) const noexcept;
	};

	template <>
	inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<ULONG>(_In_ PCWSTR name) const noexcept
		// NOLINT(bugprone-exception-escape)
	{
		for (auto counter = 0ul; counter < m_ulongFieldNameCount; ++counter)
		{
			if (ctString::iordinal_equals(name, m_ulongFieldNames[counter]))
			{
				return true;
			}
		}

		return false;
	}

	template <>
	inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<ULONGLONG>(_In_ PCWSTR name) const noexcept
		// NOLINT(bugprone-exception-escape)
	{
		for (auto counter = 0ul; counter < m_ulonglongFieldNameCount; ++counter)
		{
			if (ctString::iordinal_equals(name, m_ulonglongFieldNames[counter]))
			{
				return true;
			}
		}

		return false;
	}

	template <>
	inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<std::wstring>(_In_ PCWSTR name) const noexcept
		// NOLINT(bugprone-exception-escape)
	{
		for (auto counter = 0ul; counter < m_stringFieldNameCount; ++counter)
		{
			if (ctString::iordinal_equals(name, m_stringFieldNames[counter]))
			{
				return true;
			}
		}

		return false;
	}

	template <>
	inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<wil::unique_bstr>(_In_ PCWSTR name) const noexcept
		// NOLINT(bugprone-exception-escape)
	{
		for (auto counter = 0ul; counter < m_stringFieldNameCount; ++counter)
		{
			if (ctString::iordinal_equals(name, m_stringFieldNames[counter]))
			{
				return true;
			}
		}

		return false;
	}

	namespace ctWmiPerformanceDetails
	{
		// ReSharper disable StringLiteralTypo
		inline const wchar_t* g_commonStringPropertyNames[]{
			L"Caption",
			L"Description",
			L"Name"
		};

		inline const wchar_t* g_memoryCounter = L"Win32_PerfFormattedData_PerfOS_Memory";
		inline const wchar_t* g_memoryUlongCounterNames[]{
			L"CacheFaultsPerSec",
			L"DemandZeroFaultsPerSec",
			L"FreeSystemPageTableEntries",
			L"PageFaultsPerSec",
			L"PageReadsPerSec",
			L"PagesInputPerSec",
			L"PagesOutputPerSec",
			L"PagesPerSec",
			L"PageWritesPerSec",
			L"PercentCommittedBytesInUse",
			L"PoolNonpagedAllocs",
			L"PoolPagedAllocs",
			L"TransitionFaultsPerSec",
			L"WriteCopiesPerSec"
		};
		inline const wchar_t* g_memoryUlonglongCounterNames[]{
			L"AvailableBytes",
			L"AvailableKBytes",
			L"AvailableMBytes",
			L"CacheBytes",
			L"CacheBytesPeak",
			L"CommitLimit",
			L"CommittedBytes",
			L"Frequency_Object",
			L"Frequency_PerfTime",
			L"Frequency_Sys100NS",
			L"PoolNonpagedBytes",
			L"PoolPagedBytes",
			L"PoolPagedResidentBytes",
			L"SystemCacheResidentBytes",
			L"SystemCodeResidentBytes",
			L"SystemCodeTotalBytes",
			L"SystemDriverResidentBytes",
			L"SystemDriverTotalBytes",
			L"Timestamp_Object",
			L"Timestamp_PerfTime",
			L"Timestamp_Sys100NS"
		};

		inline const wchar_t* g_processorInformationCounter = L"Win32_PerfFormattedData_Counters_ProcessorInformation";
		inline const wchar_t* g_processorInformationUlongCounterNames[]{
			L"ClockInterruptsPersec",
			L"DPCRate",
			L"DPCsQueuedPersec",
			L"InterruptsPersec",
			L"ParkingStatus",
			L"PercentofMaximumFrequency",
			L"PercentPerformanceLimit",
			L"PerformanceLimitFlags",
			L"ProcessorFrequency",
			L"ProcessorStateFlags"
		};
		inline const wchar_t* g_processorInformationUlonglongCounterNames[]{
			L"AverageIdleTime",
			L"C1TransitionsPerSec",
			L"C2TransitionsPerSec",
			L"C3TransitionsPerSec",
			L"IdleBreakEventsPersec",
			L"PercentC1Time",
			L"PercentC2Time",
			L"PercentC3Time",
			L"PercentDPCTime",
			L"PercentIdleTime",
			L"PercentInterruptTime",
			L"PercentPriorityTime",
			L"PercentPrivilegedTime",
			L"PercentPrivilegedUtility",
			L"PercentProcessorPerformance",
			L"PercentProcessorTime",
			L"PercentProcessorUtility",
			L"PercentUserTime",
			L"Timestamp_Object",
			L"Timestamp_PerfTime",
			L"Timestamp_Sys100NS"
		};

		inline const wchar_t* g_perfProcProcessCounter = L"Win32_PerfFormattedData_PerfProc_Process";
		inline const wchar_t* g_perfProcProcessUlongCounterNames[]{
			L"CreatingProcessID",
			L"HandleCount",
			L"IDProcess",
			L"PageFaultsPerSec",
			L"PoolNonpagedBytes",
			L"PoolPagedBytes",
			L"PriorityBase",
			L"ThreadCount"
		};
		inline const wchar_t* g_perfProcProcessUlonglongCounterNames[]{
			L"ElapsedTime",
			L"Frequency_Object",
			L"Frequency_PerfTime",
			L"Frequency_Sys100NS",
			L"IODataBytesPerSec",
			L"IODataOperationsPerSec",
			L"IOOtherBytesPerSec",
			L"IOOtherOperationsPerSec",
			L"IOReadBytesPerSec",
			L"IOReadOperationsPerSec",
			L"IOWriteBytesPerSec",
			L"IOWriteOperationsPerSec",
			L"PageFileBytes",
			L"PageFileBytesPeak",
			L"PercentPrivilegedTime",
			L"PercentProcessorTime",
			L"PercentUserTime",
			L"PrivateBytes",
			L"Timestamp_Object",
			L"Timestamp_PerfTime",
			L"Timestamp_Sys100NS",
			L"VirtualBytes",
			L"VirtualBytesPeak",
			L"WorkingSet",
			L"WorkingSetPeak"
		};

		inline const wchar_t* g_tcpipNetworkAdapterCounter = L"Win32_PerfFormattedData_Tcpip_NetworkAdapter";
		inline const wchar_t* g_tcpipNetworkAdapterULongLongCounterNames[]{
			L"BytesReceivedPersec",
			L"BytesSentPersec",
			L"BytesTotalPersec",
			L"CurrentBandwidth",
			L"OffloadedConnections",
			L"OutputQueueLength",
			L"PacketsOutboundDiscarded",
			L"PacketsOutboundErrors",
			L"PacketsReceivedDiscarded",
			L"PacketsReceivedErrors",
			L"PacketsReceivedNonUnicastPersec",
			L"PacketsReceivedUnicastPersec",
			L"PacketsReceivedUnknown",
			L"PacketsReceivedPersec",
			L"PacketsSentNonUnicastPersec",
			L"PacketsSentUnicastPersec",
			L"PacketsSentPersec",
			L"PacketsPersec",
			L"TCPActiveRSCConnections",
			L"TCPRSCAveragePacketSize",
			L"TCPRSCCoalescedPacketsPersec",
			L"TCPRSCExceptionsPersec",
			L"Timestamp_Object",
			L"Timestamp_PerfTime",
			L"Timestamp_Sys100NS"
		};

		inline const wchar_t* g_tcpipNetworkInterfaceCounter = L"Win32_PerfFormattedData_Tcpip_NetworkInterface";
		inline const wchar_t* g_tcpipNetworkInterfaceULongLongCounterNames[]{
			L"BytesReceivedPerSec",
			L"BytesSentPerSec",
			L"BytesTotalPerSec",
			L"CurrentBandwidth",
			L"Frequency_Object",
			L"Frequency_PerfTime",
			L"Frequency_Sys100NS",
			L"OffloadedConnections",
			L"OutputQueueLength",
			L"PacketsOutboundDiscarded",
			L"PacketsOutboundErrors",
			L"PacketsPerSec",
			L"PacketsReceivedDiscarded",
			L"PacketsReceivedErrors",
			L"PacketsReceivedNonUnicastPerSec",
			L"PacketsReceivedPerSec",
			L"PacketsReceivedUnicastPerSec",
			L"PacketsReceivedUnknown",
			L"PacketsSentNonUnicastPerSec",
			L"PacketsSentPerSec",
			L"PacketsSentUnicastPerSec",
			L"TCPActiveRSCConnections",
			L"TCPRSCAveragePacketSize",
			L"TCPRSCCoalescedPacketsPersec",
			L"TCPRSCExceptionsPersec",
			L"Timestamp_Object",
			L"Timestamp_PerfTime",
			L"Timestamp_Sys100NS"
		};

		inline const wchar_t* g_tcpipIpv4Counter = L"Win32_PerfFormattedData_Tcpip_IPv4";
		inline const wchar_t* g_tcpipIpv6Counter = L"Win32_PerfFormattedData_Tcpip_IPv6";
		inline const wchar_t* g_tcpipIpULongCounterNames[]{
			L"DatagramsForwardedPersec",
			L"DatagramsOutboundDiscarded",
			L"DatagramsOutboundNoRoute",
			L"DatagramsReceivedAddressErrors",
			L"DatagramsReceivedDeliveredPersec",
			L"DatagramsReceivedDiscarded",
			L"DatagramsReceivedHeaderErrors",
			L"DatagramsReceivedUnknownProtocol",
			L"DatagramsReceivedPersec",
			L"DatagramsSentPersec",
			L"DatagramsPersec",
			L"FragmentReassemblyFailures",
			L"FragmentationFailures",
			L"FragmentedDatagramsPersec",
			L"FragmentsCreatedPersec",
			L"FragmentsReassembledPersec",
			L"FragmentsReceivedPersec"
		};

		inline const wchar_t* g_tcpipTcpv4Counter = L"Win32_PerfFormattedData_Tcpip_TCPv4";
		inline const wchar_t* g_tcpipTcpv6Counter = L"Win32_PerfFormattedData_Tcpip_TCPv6";
		inline const wchar_t* g_tcpipTcpULongCounterNames[]{
			L"ConnectionFailures",
			L"ConnectionsActive",
			L"ConnectionsEstablished",
			L"ConnectionsPassive",
			L"ConnectionsReset",
			L"SegmentsReceivedPersec",
			L"SegmentsRetransmittedPersec",
			L"SegmentsSentPersec",
			L"SegmentsPersec"
		};

		inline const wchar_t* g_tcpipUdpv4Counter = L"Win32_PerfFormattedData_Tcpip_UDPv4";
		inline const wchar_t* g_tcpipUdpv6Counter = L"Win32_PerfFormattedData_Tcpip_UDPv6";
		inline const wchar_t* g_tcpipUdpULongCounterNames[]{
			L"DatagramsNoPortPersec",
			L"DatagramsReceivedErrors",
			L"DatagramsReceivedPersec",
			L"DatagramsSentPersec",
			L"DatagramsPersec"
		};

		inline const wchar_t* g_tcpipPerformanceDiagnosticsCounter =
			L"Win32_PerfFormattedData_TCPIPCounters_TCPIPPerformanceDiagnostics";
		inline const wchar_t* g_tcpipPerformanceDiagnosticsULongCounterNames[]{
			L"Deniedconnectorsendrequestsinlowpowermode",
			L"IPv4NBLsindicatedwithlowresourceflag",
			L"IPv4NBLsindicatedwithoutprevalidation",
			L"IPv4NBLstreatedasnonprevalidated",
			L"IPv4NBLsPersecindicatedwithlowresourceflag",
			L"IPv4NBLsPersecindicatedwithoutprevalidation",
			L"IPv4NBLsPersectreatedasnonprevalidated",
			L"IPv4outboundNBLsnotprocessedviafastpath",
			L"IPv4outboundNBLsPersecnotprocessedviafastpath",
			L"IPv6NBLsindicatedwithlowresourceflag",
			L"IPv6NBLsindicatedwithoutprevalidation",
			L"IPv6NBLstreatedasnonprevalidated",
			L"IPv6NBLsPersecindicatedwithlowresourceflag",
			L"IPv6NBLsPersecindicatedwithoutprevalidation",
			L"IPv6NBLsPersectreatedasnonprevalidated",
			L"IPv6outboundNBLsnotprocessedviafastpath",
			L"IPv6outboundNBLsPersecnotprocessedviafastpath",
			L"TCPconnectrequestsfallenoffloopbackfastpath",
			L"TCPconnectrequestsPersecfallenoffloopbackfastpath",
			L"TCPinboundsegmentsnotprocessedviafastpath",
			L"TCPinboundsegmentsPersecnotprocessedviafastpath"
		};

		inline const wchar_t* g_microsoftWinsockBspCounter = L"Win32_PerfFormattedData_AFDCounters_MicrosoftWinsockBSP";
		inline const wchar_t* g_microsoftWinsockBspULongCounterNames[]{
			L"DroppedDatagrams",
			L"DroppedDatagramsPersec",
			L"RejectedConnections",
			L"RejectedConnectionsPersec"
		};

		inline const wchar_t* g_wfpFilterCounter = L"Win32_PerfFormattedData_Counters_WFP";
		inline const wchar_t* g_wfpFilterULongCounterNames[]{
			L"ProviderCount"
		};

		inline const wchar_t* g_wfpFilterCountCounter = L"Win32_PerfFormattedData_Counters_WFPFilterCount";
		// ReSharper disable once IdentifierTypo
		inline const wchar_t* g_wfpFilterCountULongLongCounterNames[]{
			L"FWPM_LAYER_INBOUND_IPPACKET_V4",
			L"FWPM_LAYER_INBOUND_IPPACKET_V4_DISCARD",
			L"FWPM_LAYER_INBOUND_IPPACKET_V6",
			L"FWPM_LAYER_INBOUND_IPPACKET_V6_DISCARD",
			L"FWPM_LAYER_OUTBOUND_IPPACKET_V4",
			L"FWPM_LAYER_OUTBOUND_IPPACKET_V4_DISCARD",
			L"FWPM_LAYER_OUTBOUND_IPPACKET_V6",
			L"FWPM_LAYER_OUTBOUND_IPPACKET_V6_DISCARD",
			L"FWPM_LAYER_IPFORWARD_V4",
			L"FWPM_LAYER_IPFORWARD_V4_DISCARD",
			L"FWPM_LAYER_IPFORWARD_V6",
			L"FWPM_LAYER_IPFORWARD_V6_DISCARD",
			L"FWPM_LAYER_INBOUND_TRANSPORT_V4",
			L"FWPM_LAYER_INBOUND_TRANSPORT_V4_DISCARD",
			L"FWPM_LAYER_INBOUND_TRANSPORT_V6",
			L"FWPM_LAYER_INBOUND_TRANSPORT_V6_DISCARD",
			L"FWPM_LAYER_OUTBOUND_TRANSPORT_V4",
			L"FWPM_LAYER_OUTBOUND_TRANSPORT_V4_DISCARD",
			L"FWPM_LAYER_OUTBOUND_TRANSPORT_V6",
			L"FWPM_LAYER_OUTBOUND_TRANSPORT_V6_DISCARD",
			L"FWPM_LAYER_STREAM_V4",
			L"FWPM_LAYER_STREAM_V4_DISCARD",
			L"FWPM_LAYER_STREAM_V6",
			L"FWPM_LAYER_STREAM_V6_DISCARD",
			L"FWPM_LAYER_DATAGRAM_DATA_V4",
			L"FWPM_LAYER_DATAGRAM_DATA_V4_DISCARD",
			L"FWPM_LAYER_DATAGRAM_DATA_V6",
			L"FWPM_LAYER_DATAGRAM_DATA_V6_DISCARD",
			L"FWPM_LAYER_INBOUND_ICMP_ERROR_V4",
			L"FWPM_LAYER_INBOUND_ICMP_ERROR_V4_DISCARD",
			L"FWPM_LAYER_INBOUND_ICMP_ERROR_V6",
			L"FWPM_LAYER_INBOUND_ICMP_ERROR_V6_DISCARD",
			L"FWPM_LAYER_OUTBOUND_ICMP_ERROR_V4",
			L"FWPM_LAYER_OUTBOUND_ICMP_ERROR_V4_DISCARD",
			L"FWPM_LAYER_OUTBOUND_ICMP_ERROR_V6",
			L"FWPM_LAYER_OUTBOUND_ICMP_ERROR_V6_DISCARD",
			L"FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V4",
			L"FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V4_DISCARD",
			L"FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V6",
			L"FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V6_DISCARD",
			L"FWPM_LAYER_ALE_AUTH_LISTEN_V4",
			L"FWPM_LAYER_ALE_AUTH_LISTEN_V4_DISCARD",
			L"FWPM_LAYER_ALE_AUTH_LISTEN_V6",
			L"FWPM_LAYER_ALE_AUTH_LISTEN_V6_DISCARD",
			L"FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4",
			L"FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4_DISCARD",
			L"FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6",
			L"FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6_DISCARD",
			L"FWPM_LAYER_ALE_AUTH_CONNECT_V4",
			L"FWPM_LAYER_ALE_AUTH_CONNECT_V4_DISCARD",
			L"FWPM_LAYER_ALE_AUTH_CONNECT_V6",
			L"FWPM_LAYER_ALE_AUTH_CONNECT_V6_DISCARD",
			L"FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4",
			L"FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4_DISCARD",
			L"FWPM_LAYER_ALE_FLOW_ESTABLISHED_V6",
			L"FWPM_LAYER_ALE_FLOW_ESTABLISHED_V6_DISCARD",
			L"FWPM_LAYER_INBOUND_MAC_FRAME_ETHERNET",
			L"FWPM_LAYER_OUTBOUND_MAC_FRAME_ETHERNET",
			L"FWPM_LAYER_INBOUND_MAC_FRAME_NATIVE",
			L"FWPM_LAYER_OUTBOUND_MAC_FRAME_NATIVE",
			L"FWPM_LAYER_NAME_RESOLUTION_CACHE_V4",
			L"FWPM_LAYER_NAME_RESOLUTION_CACHE_V6",
			L"FWPM_LAYER_ALE_RESOURCE_RELEASE_V4",
			L"FWPM_LAYER_ALE_RESOURCE_RELEASE_V6",
			L"FWPM_LAYER_ALE_ENDPOINT_CLOSURE_V4",
			L"FWPM_LAYER_ALE_ENDPOINT_CLOSURE_V6",
			L"FWPM_LAYER_ALE_CONNECT_REDIRECT_V4",
			L"FWPM_LAYER_ALE_CONNECT_REDIRECT_V6",
			L"FWPM_LAYER_ALE_BIND_REDIRECT_V4",
			L"FWPM_LAYER_ALE_BIND_REDIRECT_V6",
			L"FWPM_LAYER_STREAM_PACKET_V4",
			L"FWPM_LAYER_STREAM_PACKET_V6",
			L"FWPM_LAYER_INGRESS_VSWITCH_ETHERNET",
			L"FWPM_LAYER_EGRESS_VSWITCH_ETHERNET",
			L"FWPM_LAYER_INGRESS_VSWITCH_TRANSPORT_V4",
			L"FWPM_LAYER_INGRESS_VSWITCH_TRANSPORT_V6",
			L"FWPM_LAYER_EGRESS_VSWITCH_TRANSPORT_V4",
			L"FWPM_LAYER_EGRESS_VSWITCH_TRANSPORT_V6",
			L"FWPM_LAYER_INBOUND_TRANSPORT_FAST",
			L"FWPM_LAYER_OUTBOUND_TRANSPORT_FAST",
			L"FWPM_LAYER_INBOUND_MAC_FRAME_NATIVE_FAST",
			L"FWPM_LAYER_OUTBOUND_MAC_FRAME_NATIVE_FAST",
			L"FWPM_LAYER_INBOUND_RESERVED2",
			L"FWPM_LAYER_ALE_ACCEPT_REDIRECT_V4",
			L"FWPM_LAYER_ALE_ACCEPT_REDIRECT_V6",
			L"FWPM_LAYER_OUTBOUND_NETWORK_CONNECTION_POLICY_V4",
			L"FWPM_LAYER_OUTBOUND_NETWORK_CONNECTION_POLICY_V6",
			L"FWPM_LAYER_IPSEC_KM_DEMUX_V4",
			L"FWPM_LAYER_IPSEC_KM_DEMUX_V6",
			L"FWPM_LAYER_IPSEC_V4",
			L"FWPM_LAYER_IPSEC_V6",
			L"FWPM_LAYER_IKEEXT_V4",
			L"FWPM_LAYER_IKEEXT_V6",
			L"FWPM_LAYER_RPC_UM",
			L"FWPM_LAYER_RPC_EPMAP",
			L"FWPM_LAYER_RPC_EP_ADD",
			L"FWPM_LAYER_RPC_PROXY_CONN",
			L"FWPM_LAYER_RPC_PROXY_IF",
			L"FWPM_LAYER_KM_AUTHORIZATION",
			L"Total",
		};
		// ReSharper restore StringLiteralTypo

		// this patterns (const array of wchar_t* pointers)
		// allows for compile-time construction of the array of properties
		inline const ctWmiPerformanceCounterProperties c_performanceCounterPropertiesArray[]{

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::Memory,
				.m_providerName = g_memoryCounter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_memoryUlongCounterNames),
				.m_ulongFieldNames = g_memoryUlongCounterNames,
				.m_ulonglongFieldNameCount = ARRAYSIZE(g_memoryUlonglongCounterNames),
				.m_ulonglongFieldNames = g_memoryUlonglongCounterNames,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Instance,
				.m_className = ctWmiEnumClassName::Processor,
				.m_providerName = g_processorInformationCounter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_processorInformationUlongCounterNames),
				.m_ulongFieldNames = g_processorInformationUlongCounterNames,
				.m_ulonglongFieldNameCount = ARRAYSIZE(g_processorInformationUlonglongCounterNames),
				.m_ulonglongFieldNames = g_processorInformationUlonglongCounterNames,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Instance,
				.m_className = ctWmiEnumClassName::Process,
				.m_providerName = g_perfProcProcessCounter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_perfProcProcessUlongCounterNames),
				.m_ulongFieldNames = g_perfProcProcessUlongCounterNames,
				.m_ulonglongFieldNameCount = ARRAYSIZE(g_perfProcProcessUlonglongCounterNames),
				.m_ulonglongFieldNames = g_perfProcProcessUlonglongCounterNames,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Instance,
				.m_className = ctWmiEnumClassName::NetworkAdapter,
				.m_providerName = g_tcpipNetworkAdapterCounter,
				.m_ulongFieldNameCount = 0,
				.m_ulongFieldNames = nullptr,
				.m_ulonglongFieldNameCount = ARRAYSIZE(g_tcpipNetworkAdapterULongLongCounterNames),
				.m_ulonglongFieldNames = g_tcpipNetworkAdapterULongLongCounterNames,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Instance,
				.m_className = ctWmiEnumClassName::NetworkInterface,
				.m_providerName = g_tcpipNetworkInterfaceCounter,
				.m_ulongFieldNameCount = 0,
				.m_ulongFieldNames = nullptr,
				.m_ulonglongFieldNameCount = ARRAYSIZE(g_tcpipNetworkInterfaceULongLongCounterNames),
				.m_ulonglongFieldNames = g_tcpipNetworkInterfaceULongLongCounterNames,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::TcpipIpv4,
				.m_providerName = g_tcpipIpv4Counter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_tcpipIpULongCounterNames),
				.m_ulongFieldNames = g_tcpipIpULongCounterNames,
				.m_ulonglongFieldNameCount = 0,
				.m_ulonglongFieldNames = nullptr,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::TcpipIpv6,
				.m_providerName = g_tcpipIpv6Counter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_tcpipIpULongCounterNames),
				.m_ulongFieldNames = g_tcpipIpULongCounterNames,
				.m_ulonglongFieldNameCount = 0,
				.m_ulonglongFieldNames = nullptr,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::TcpipTcpv4,
				.m_providerName = g_tcpipTcpv4Counter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_tcpipTcpULongCounterNames),
				.m_ulongFieldNames = g_tcpipTcpULongCounterNames,
				.m_ulonglongFieldNameCount = 0,
				.m_ulonglongFieldNames = nullptr,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::TcpipTcpv6,
				.m_providerName = g_tcpipTcpv6Counter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_tcpipTcpULongCounterNames),
				.m_ulongFieldNames = g_tcpipTcpULongCounterNames,
				.m_ulonglongFieldNameCount = 0,
				.m_ulonglongFieldNames = nullptr,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::TcpipUdpv4,
				.m_providerName = g_tcpipUdpv4Counter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_tcpipUdpULongCounterNames),
				.m_ulongFieldNames = g_tcpipUdpULongCounterNames,
				.m_ulonglongFieldNameCount = 0,
				.m_ulonglongFieldNames = nullptr,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::TcpipUdpv6,
				.m_providerName = g_tcpipUdpv6Counter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_tcpipUdpULongCounterNames),
				.m_ulongFieldNames = g_tcpipUdpULongCounterNames,
				.m_ulonglongFieldNameCount = 0,
				.m_ulonglongFieldNames = nullptr,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::TcpipDiagnostics,
				.m_providerName = g_tcpipPerformanceDiagnosticsCounter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_tcpipPerformanceDiagnosticsULongCounterNames),
				.m_ulongFieldNames = g_tcpipPerformanceDiagnosticsULongCounterNames,
				.m_ulonglongFieldNameCount = 0,
				.m_ulonglongFieldNames = nullptr,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::WinsockBsp,
				.m_providerName = g_microsoftWinsockBspCounter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_microsoftWinsockBspULongCounterNames),
				.m_ulongFieldNames = g_microsoftWinsockBspULongCounterNames,
				.m_ulonglongFieldNameCount = 0,
				.m_ulonglongFieldNames = nullptr,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::WfpFilter,
				.m_providerName = g_wfpFilterCounter,
				.m_ulongFieldNameCount = ARRAYSIZE(g_wfpFilterULongCounterNames),
				.m_ulongFieldNames = g_wfpFilterULongCounterNames,
				.m_ulonglongFieldNameCount = 0,
				.m_ulonglongFieldNames = nullptr,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},

			{
				.m_classType = ctWmiEnumClassType::Static,
				.m_className = ctWmiEnumClassName::WfpFilterCount,
				.m_providerName = g_wfpFilterCountCounter,
				.m_ulongFieldNameCount = 0,
				.m_ulongFieldNames = nullptr,
				.m_ulonglongFieldNameCount = ARRAYSIZE(g_wfpFilterCountULongLongCounterNames),
				.m_ulonglongFieldNames = g_wfpFilterCountULongLongCounterNames,
				.m_stringFieldNameCount = ARRAYSIZE(g_commonStringPropertyNames),
				.m_stringFieldNames = g_commonStringPropertyNames
			},
		};
	}

	template <typename T>
	std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeStaticPerfCounter(
		const ctWmiService& wmi,
		_In_ PCWSTR className,
		_In_ PCWSTR counterName,
		ctWmiPerformanceCollectionType collectionType = ctWmiPerformanceCollectionType::Detailed)
	{
		// 'static' WMI PerfCounters enumerate via IWbemClassObject and accessed/refreshed via IWbemClassObject
		return std::make_shared<ctWmiPerformanceCounterImpl<IWbemClassObject, IWbemClassObject, T>>(
			wmi, className, counterName, collectionType);
	}

	template <typename T>
	std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeStaticPerfCounter(
		PCWSTR className,
		PCWSTR counterName,
		const ctWmiPerformanceCollectionType collectionType = ctWmiPerformanceCollectionType::Detailed)
	{
		const ctWmiService wmi(L"root\\cimv2");
		return ctMakeStaticPerfCounter<T>(wmi, className, counterName, collectionType);
	}

	template <typename T>
	std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeInstancePerfCounter(
		const ctWmiService& wmi,
		_In_ PCWSTR className,
		_In_ PCWSTR counterName,
		ctWmiPerformanceCollectionType collectionType = ctWmiPerformanceCollectionType::Detailed)
	{
		// 'instance' WMI perf objects are enumerated through the IWbemHiPerfEnum interface and accessed/refreshed through the IWbemObjectAccess interface
		return std::make_shared<ctWmiPerformanceCounterImpl<IWbemHiPerfEnum, IWbemObjectAccess, T>>(
			wmi, className, counterName, collectionType);
	}

	template <typename T>
	std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeInstancePerfCounter(
		_In_ PCWSTR className,
		_In_ PCWSTR counterName,
		ctWmiPerformanceCollectionType collectionType = ctWmiPerformanceCollectionType::Detailed)
	{
		const ctWmiService wmi(L"root\\cimv2");
		return ctMakeInstancePerfCounter<T>(wmi, className, counterName, collectionType);
	}

	template <typename T>
	std::shared_ptr<ctWmiPerformanceCounter<T>> ctCreatePerfCounter(
		const ctWmiService& wmi,
		ctWmiEnumClassName className,
		_In_ PCWSTR counterName,
		ctWmiPerformanceCollectionType collectionType = ctWmiPerformanceCollectionType::Detailed)
	{
		const ctWmiPerformanceCounterProperties* foundProperty = nullptr;
		for (const auto& counterProperty : ctWmiPerformanceDetails::c_performanceCounterPropertiesArray)
		{
			if (className == counterProperty.m_className)
			{
				foundProperty = &counterProperty;
				break;
			}
		}

		THROW_HR_IF_MSG(
			HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
			!foundProperty,
			"Unknown WMI Performance Counter Class");

		THROW_HR_IF_MSG(
			HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
			!foundProperty->PropertyNameExists<T>(counterName),
			"CounterName (%ws) does not exist in the requested class (%u)",
			counterName, static_cast<unsigned>(className));

		if (foundProperty->m_classType == ctWmiEnumClassType::Static)
		{
			return ctMakeStaticPerfCounter<T>(wmi, foundProperty->m_providerName, counterName, collectionType);
		}

		FAIL_FAST_IF(foundProperty->m_classType != ctWmiEnumClassType::Instance);
		return ctMakeInstancePerfCounter<T>(wmi, foundProperty->m_providerName, counterName, collectionType);
	}

	template <typename T>
	std::shared_ptr<ctWmiPerformanceCounter<T>> ctCreatePerfCounter(
		ctWmiEnumClassName className,
		_In_ PCWSTR counterName,
		ctWmiPerformanceCollectionType collectionType = ctWmiPerformanceCollectionType::Detailed)
	{
		const ctWmiService wmi(L"root\\cimv2");
		return ctCreatePerfCounter<T>(wmi, className, counterName, collectionType);
	}
} // namespace ctl
