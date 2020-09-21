/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

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
#include <OleAuto.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>
// ctl headers
#include <ctWmiInitialize.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// Concepts for this class:
/// - WMI Classes expose performance counters through hi-performance WMI interfaces
/// - ctWmiPerformanceCounter exposes one counter within one WMI performance counter class
/// - Every performance counter object contains a 'Name' key field, uniquely identifying a 'set' of data points for that counter
/// - Counters are 'snapped' every one second, with the timeslot tracked with the data
///
/// ctWmiPerformanceCounter is exposed to the user through factory functions defined per-counter class (ctShared<class>PerfCounter)
/// - the factory functions takes in the counter name that the user wants to capture data for
/// - the factory function has a template type matching the data type of the counter data for that counter name
///
/// Internally, the factory function instantiates a ctWmiPerformanceCounterImpl:
/// - has 3 template arguments:
/// 1. The IWbem* interface used to enumerate instances of this performance class (either IWbemHiPerfEnum or IWbemClassObject)
/// 2. The IWbem* interface used to access data in perf instances of this performance class (either IWbemObjectAccess or IWbemClassObject)
/// 3. The data type of the values for the counter name being recorded
/// - has 2 function arguments
/// 1. The string value of the WMI class to be used
/// 2. The string value of the counter name to be recorded
///
/// Methods exposed publicly off of ctWmiPerformanceCounter:
/// - add_filter(): allows the caller to only capture instances which match the parameter/value combination for that object
/// - reference_range() : takes an Instance Name by which to return values
/// -- returns begin/end iterators to reference the data
///
/// ctWmiPerformanceCounter populates data by invoking a pure virtual function (update_counter_data) every one second.
/// - update_counter_data takes a boolean parameter: true will invoke the virtual function to update the data, false will clear the data.
/// That pure virtual function (ctWmiPerformanceCounterImpl::update_counter_data) refreshes its counter (through its template accessor interface)
/// - then iterates through each instance recorded for that counter and invokes add_instance() in the base class.
///
/// add_instance() takes a WMI* of teh Accessor type: if that instance wasn't explicitly filtered out (through an instance_filter object),
/// - it looks to see if this is a new instance or if we have already been tracking that instance
/// - if new, we create a new ctWmiPerformanceCounterData object and add it to our counter_data
/// - if not new, we just add this object to the counter_data object that we already created
///
/// There are 2 possible sets of WMI interfaces to access and enumerate performance data, these are defined in ctWmiPerformanceDataAccessor
/// - this is instantiated in ctWmiPerformanceCounterImpl as it knows the Access and Enum template types
///
/// ctWmiPerformanceCounterData encapsulates the data points for one instance of one counter.
/// - exposes match() taking a string to check if it matches the instance it contains
/// - exposes add() taking both types of Access objects + a ULONGLONG time parameter to retrieve the data and add it to the internal map
///
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace ctl
{
    enum class ctWmiPerformanceCollectionType
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
                return rhs.date == lhs.date;
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
        unsigned rhs_integer = 0;
        switch (rhs.vt)
        {
            case VT_I1:
                rhs_integer += rhs.cVal;
                break;
            case VT_UI1:
                rhs_integer += rhs.bVal;
                break;
            case VT_I2:
                rhs_integer += rhs.iVal;
                break;
            case VT_UI2:
                rhs_integer += rhs.uiVal;
                break;
            case VT_I4:
                rhs_integer += rhs.lVal;
                break;
            case VT_UI4:
                rhs_integer += rhs.ulVal;
                break;
            case VT_INT:
                rhs_integer += rhs.intVal;
                break;
            case VT_UINT:
                rhs_integer += rhs.uintVal;
                break;
            default:
                return false;
        }
        unsigned lhs_integer = 0;
        switch (lhs.vt)
        {
            case VT_I1:
                lhs_integer += lhs.cVal;
                break;
            case VT_UI1:
                lhs_integer += lhs.bVal;
                break;
            case VT_I2:
                lhs_integer += lhs.iVal;
                break;
            case VT_UI2:
                lhs_integer += lhs.uiVal;
                break;
            case VT_I4:
                lhs_integer += lhs.lVal;
                break;
            case VT_UI4:
                lhs_integer += lhs.ulVal;
                break;
            case VT_INT:
                lhs_integer += lhs.intVal;
                break;
            case VT_UINT:
                lhs_integer += lhs.uintVal;
                break;
            default:
                return false;
        }

        return lhs_integer == rhs_integer;
    }

    inline bool operator !=(const wil::unique_variant& rhs, const wil::unique_variant& lhs) noexcept
    {
        return !(rhs == lhs);
    }

    namespace details
    {
        inline wil::unique_variant ReadCounterFromWbemObjectAccess(_In_ IWbemObjectAccess* instance, _In_ PCWSTR counter_name)
        {
            LONG property_handle{};
            CIMTYPE property_type{};
            THROW_IF_FAILED(instance->GetPropertyHandle(counter_name, &property_type, &property_handle));

            wil::unique_variant current_value;
            switch (property_type)
            {
                case CIM_SINT32:
                case CIM_UINT32:
                {
                    ULONG value{};
                    THROW_IF_FAILED(instance->ReadDWORD(property_handle, &value));
                    current_value = ctWmiMakeVariant(value);
                    break;
                }

                case CIM_SINT64:
                case CIM_UINT64:
                {
                    ULONGLONG value{};
                    THROW_IF_FAILED(instance->ReadQWORD(property_handle, &value));
                    current_value = ctWmiMakeVariant(value);
                    break;
                }

                case CIM_STRING:
                {
                    constexpr long CimStringDefaultSize = 64;
                    std::wstring value(CimStringDefaultSize, L'\0');
                    long value_size = CimStringDefaultSize * sizeof(WCHAR);
                    long returned_size{};
                    auto hr = instance->ReadPropertyValue(property_handle, value_size, &returned_size, reinterpret_cast<BYTE*>(&value[0]));
                    if (WBEM_E_BUFFER_TOO_SMALL == hr)
                    {
                        value_size = returned_size;
                        value.resize(value_size / sizeof(WCHAR));
                        hr = instance->ReadPropertyValue(property_handle, value_size, &returned_size, reinterpret_cast<BYTE*>(&value[0]));
                    }
                    THROW_IF_FAILED(hr);
                    current_value = ctWmiMakeVariant(value.c_str());
                    break;
                }

                default:
                    THROW_HR_MSG(HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                        "ctWmiPerformance only supports data of type INT32, INT64, and BSTR: counter %ws is of type %u",
                        counter_name, static_cast<unsigned>(property_type));
            }

            return current_value;
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// template class ctWmiPerformanceDataAccessor
        ///
        /// Refreshes performance data for the target specified based off the classname
        /// - and the template types specified [the below are the only types supported]:
        ///
        /// Note: caller *MUST* provide thread safety
        ///       this class is not providing locking at this level
        ///
        /// Note: callers *MUST* guarantee connections with the WMI service stay connected
        ///       for the lifetime of this object [e.g. guarnated ctWmiService is instanitated]
        ///
        /// Note: callers *MUST* guarantee that COM is CoInitialized on this thread before calling
        ///
        /// Note: the ctWmiPerformance class *will* retain WMI service instance
        ///       it's recommended to guarantee it stays alive
        ///
        /// Template typename options:
        ///
        /// typename TEnum == IWbemHiPerfEnum
        /// - encapsulates the processing of IWbemHiPerfEnum instances of type _classname
        ///
        /// typename TEnum == IWbemClassObject
        /// - encapsulates the processing of a single refreshable IWbemClassObject of type _classname
        ///
        /// typename TAccess == IWbemObjectAccess
        /// - begin/end return an iterator to a vector<IWbemObjectAccess> of refreshed perf data
        /// - Note: could be N number of instances
        ///
        /// typename TAccess == IWbemClassObject
        /// - begin/end return an iterator to a vector<IWbemClassObject> of refreshed perf data
        /// - Note: will only ever be a single instance
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        template <typename TEnum, typename TAccess>
        class ctWmiPerformanceDataAccessor
        {
        public:
            typedef typename std::vector<TAccess*>::const_iterator access_iterator;

            ctWmiPerformanceDataAccessor(ctWmiService wmi, const wil::com_ptr<IWbemConfigureRefresher>& config, _In_ PCWSTR classname);

            ~ctWmiPerformanceDataAccessor() noexcept
            {
                clear();
            }

            ///
            /// refreshes internal data with the latest performance data
            ///
            void refresh();

            [[nodiscard]] access_iterator begin() const noexcept
            {
                return m_accessorObjects.cbegin();
            }
            [[nodiscard]] access_iterator end() const noexcept
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
                // since accessor_objects is storing raw pointers, manually clear out the rhs object
                // so they won't be double-deleted
                rhs.m_accessorObjects.clear();
                rhs.m_currentIterator = rhs.m_accessorObjects.end();
            }
            ctWmiPerformanceDataAccessor& operator=(ctWmiPerformanceDataAccessor&& rhs) noexcept
            {
                m_enumerationObject = std::move(rhs.m_enumerationObject);
                m_accessorObjects = std::move(rhs.m_accessorObjects);
                m_currentIterator = std::move(rhs.m_currentIterator);
                // since accessor_objects is storing raw pointers, manually clear out the rhs object
                // so they won't be double-deleted
                rhs.m_accessorObjects.clear();
                rhs.m_currentIterator = rhs.m_accessorObjects.end();
                return *this;
            }

        private:
            wil::com_ptr<TEnum> m_enumerationObject;
            // TAccess pointers are returned through enumeration_object::GetObjects, reusing the same vector for each refresh call
            std::vector<TAccess*> m_accessorObjects;
            access_iterator m_currentIterator;

            void clear() noexcept;
        };

        inline ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::ctWmiPerformanceDataAccessor(ctWmiService wmi, const wil::com_ptr<IWbemConfigureRefresher>& config, _In_ PCWSTR classname)
            : m_currentIterator(m_accessorObjects.end())
        {
            LONG lid{};
            THROW_IF_FAILED(config->AddEnum(wmi.get(), classname, 0, nullptr, m_enumerationObject.addressof(), &lid));
        }

        inline ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::ctWmiPerformanceDataAccessor(ctWmiService wmi, const wil::com_ptr<IWbemConfigureRefresher>& config, _In_ PCWSTR classname)
            : m_currentIterator(m_accessorObjects.end())
        {
            ctWmiEnumerate enum_instances(wmi);
            enum_instances.query(wil::str_printf<std::wstring>(L"SELECT * FROM %ws", classname).c_str());
            if (enum_instances.begin() == enum_instances.end())
            {
                THROW_HR_MSG(HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
                    "Failed to refresh a static instances of the WMI class %ws", classname);
            }

            const auto instance = *enum_instances.begin();
            LONG lid{};
            THROW_IF_FAILED(config->AddObjectByTemplate(
                wmi.get(),
                instance.get_instance_object().get(),
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

            ULONG objects_returned = 0;
            auto hr = m_enumerationObject->GetObjects(
                0,
                static_cast<ULONG>(m_accessorObjects.size()),
                m_accessorObjects.empty() ? nullptr : &m_accessorObjects[0],
                &objects_returned);

            if (WBEM_E_BUFFER_TOO_SMALL == hr)
            {
                m_accessorObjects.resize(objects_returned);
                hr = m_enumerationObject->GetObjects(
                    0,
                    static_cast<ULONG>(m_accessorObjects.size()),
                    &m_accessorObjects[0],
                    &objects_returned);
            }
            THROW_IF_FAILED(hr);

            m_accessorObjects.resize(objects_returned);
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

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Stucture to track the performance data for each property desired for the instance being tracked
        ///
        /// typename T : the data type of the counter to be stored
        ///
        /// Note: callers *MUST* guarantee connections with the WMI service stay connected
        ///       for the lifetime of this object [e.g. guarnated ctWmiService is instanitated]
        /// Note: callers *MUST* guarantee that COM is CoInitialized on this thread before calling
        /// Note: the ctWmiPerformance class *will* retain WMI service instance
        ///       it's recommended to guarantee it stays alive
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        template <typename T>
        class ctWmiPerformanceCounterData
        {
        private:
            mutable wil::critical_section m_guardData;
            const ctWmiPerformanceCollectionType m_collectionType = ctWmiPerformanceCollectionType::Detailed;
            const std::wstring m_instanceName;
            const std::wstring m_counterName;
            std::vector<T> m_counterData;
            ULONGLONG m_counterSum = 0;

            void add_data(const T& instance_data)
            {
                const auto auto_guard = m_guardData.lock();
                switch (m_collectionType)
                {
                    case ctWmiPerformanceCollectionType::Detailed:
                        m_counterData.push_back(instance_data);
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
                            m_counterData.push_back(instance_data);
                            m_counterData.push_back(instance_data);
                            m_counterData.push_back(0);
                        }
                        else
                        {
                            ++m_counterData[0];
                            if (instance_data < m_counterData[1])
                            {
                                m_counterData[1] = instance_data;
                            }
                            if (instance_data > m_counterData[2])
                            {
                                m_counterData[2] = instance_data;
                            }
                        }

                        m_counterSum += instance_data;
                        break;

                    case ctWmiPerformanceCollectionType::FirstLast:
                        // the first data point write both min and max
                        // [0] == count
                        // [1] == first
                        // [2] == last
                        if (m_counterData.empty())
                        {
                            m_counterData.push_back(1);
                            m_counterData.push_back(instance_data);
                            m_counterData.push_back(instance_data);
                        }
                        else
                        {
                            ++m_counterData[0];
                            m_counterData[2] = instance_data;
                        }
                        break;

                    default:
                        FAIL_FAST_MSG(
                            "Unknown ctWmiPerformanceCollectionType (%u)",
                            static_cast<unsigned>(m_collectionType));
                }
            }

            typename std::vector<T>::const_iterator access_begin() noexcept
            {
                const auto auto_guard = m_guardData.lock();
                // when accessing data, calculate the mean
                if (ctWmiPerformanceCollectionType::MeanOnly == m_collectionType)
                {
                    m_counterData[3] = static_cast<T>(m_counterSum / m_counterData[0]);
                }
                return m_counterData.cbegin();
            }

            typename std::vector<T>::const_iterator access_end() const noexcept
            {
                const auto auto_guard = m_guardData.lock();
                return m_counterData.cend();
            }

        public:
            ctWmiPerformanceCounterData(
                const ctWmiPerformanceCollectionType collectionType,
                _In_ IWbemObjectAccess* instance,
                _In_ PCWSTR counter)
                : m_collectionType(collectionType),
                m_instanceName(V_BSTR(ReadCounterFromWbemObjectAccess(instance, L"Name").addressof())),
                m_counterName(counter)
            {
            }
            ctWmiPerformanceCounterData(
                const ctWmiPerformanceCollectionType collection_type,
                _In_ IWbemClassObject* instance,
                _In_ PCWSTR counter)
                : m_collectionType(collection_type),
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

            // instance_name == nullptr means match everything
            bool match(_In_opt_ PCWSTR instanceName) const
            {
                if (!instanceName)
                {
                    return true;
                }
                return ctString::ctOrdinalEqualsCaseInsensative(instanceName, m_instanceName);
            }

            void add(_In_ IWbemObjectAccess* instance)
            {
                T instance_data{};
                if (ctWmiReadFromVariant(
                    ReadCounterFromWbemObjectAccess(instance, m_counterName.c_str()).addressof(),
                    &instance_data))
                {
                    add_data(instance_data);
                }
            }
            void add(_In_ IWbemClassObject* instance)
            {
                wil::unique_variant value;
                THROW_IF_FAILED(instance->Get(m_counterName.c_str(), 0, value.addressof(), nullptr, nullptr));
                // the instance could return null if there's no data
                if (value.vt != VT_NULL)
                {
                    T instance_data;
                    if (ctWmiReadFromVariant(value.addressof(), &instance_data))
                    {
                        add_data(instance_data);
                    }
                }
            }

            typename std::vector<T>::const_iterator begin() noexcept
            {
                const auto auto_guard = m_guardData.lock();
                return access_begin();
            }
            typename std::vector<T>::const_iterator end() const noexcept
            {
                const auto auto_guard = m_guardData.lock();
                return access_end();
            }

            size_t count() noexcept
            {
                const auto auto_guard = m_guardData.lock();
                return access_end() - access_begin();
            }

            void clear() noexcept
            {
                const auto auto_guard = m_guardData.lock();
                m_counterData.clear();
                m_counterSum = 0;
            }

            // non-copyable
            ctWmiPerformanceCounterData(const ctWmiPerformanceCounterData&) = delete;
            ctWmiPerformanceCounterData& operator= (const ctWmiPerformanceCounterData&) = delete;

            // not movable
            ctWmiPerformanceCounterData(ctWmiPerformanceCounterData&&) = delete;
            ctWmiPerformanceCounterData& operator=(ctWmiPerformanceCounterData&&) = delete;
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

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// type for the callback implemented in all ctWmiPerformanceCounter classes
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        enum class CallbackAction
        {
            Start,
            Stop,
            Update,
            Clear
        };
        typedef std::function<void(CallbackAction action)> ctWmiPerformanceCallback;
    } // unnamed namespace


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// class ctWmiPerformanceCounter
    /// - The abstract base class contains the WMI-specific code which all templated instances will derive from
    /// - Using public inheritance + protected members over composition as we need a common type which we can pass to
    ///   ctWmiPerformance
    /// - Exposes the iterator class for users to traverse the data points gathered
    ///
    /// Note: callers *MUST* guarantee connections with the WMI service stay connected
    ///       for the lifetime of this object [e.g. guarnated ctWmiService is instanitated]
    /// Note: callers *MUST* guarantee that COM is CoInitialized on this thread before calling
    /// Note: the ctWmiPerformance class *will* retain WMI service instance
    ///       it's recommended to guarantee it stays alive
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // forward-declaration to reference ctWmiPerformance
    class ctWmiPerformance;
    template <typename T>
    class ctWmiPerformanceCounter
    {
    public:
        // iterates across *time-slices* captured over from ctWmiPerformance
        class iterator
        {
        private:
            typename std::vector<T>::const_iterator m_current;
            bool m_isEmpty = true;

        public:
            // iterator_traits - allows <algorithm> functions to be used
            typedef std::forward_iterator_tag iterator_category;
            typedef T value_type;
            typedef size_t difference_type;
            typedef T* pointer;
            typedef T& reference;

            explicit iterator(typename std::vector<T>::const_iterator&& instance) noexcept :
                m_current(std::move(instance)), m_isEmpty(false)
            {
            }
            iterator() = default;
            ~iterator() noexcept = default;

            ////////////////////////////////////////////////////////////////////////////////
            ///
            /// copy c'tor and copy assignment
            /// move c'tor and move assignment
            ///
            ////////////////////////////////////////////////////////////////////////////////
            iterator(iterator&& i) noexcept :
                m_current(std::move(i.m_current)),
                m_isEmpty(std::move(i.m_isEmpty))
            {
            }
            iterator& operator =(iterator&& i) noexcept
            {
                m_current = std::move(i.m_current);
                m_isEmpty = std::move(i.m_isEmpty);
                return *this;
            }

            iterator(const iterator& i) noexcept :
                m_current(i.m_current),
                m_isEmpty(i.m_isEmpty)
            {
            }
            iterator& operator =(const iterator& i) noexcept  // NOLINT(cert-oop54-cpp)
            {
                iterator local_copy(i);
                *this = std::move(local_copy);
                return *this;
            }

            const T& operator* () const
            {
                if (m_isEmpty)
                {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : dereferencing an iterator referencing an empty container");
                }
                return *m_current;
            }

            bool operator==(const iterator& iter) const noexcept
            {
                if (m_isEmpty || iter.m_isEmpty)
                {
                    return (m_isEmpty == iter.m_isEmpty);
                }
                return (m_current == iter.m_current);
            }
            bool operator!=(const iterator& iter) const noexcept
            {
                return !(*this == iter);
            }

            // preincrement
            iterator& operator++()
            {
                if (m_isEmpty)
                {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : preincrementing an iterator referencing an empty container");
                }
                ++m_current;
                return *this;
            }
            // postincrement
            iterator operator++(int)
            {
                if (m_isEmpty)
                {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : postincrementing an iterator referencing an empty container");
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
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : postincrementing an iterator referencing an empty container");
                }
                for (size_t loop = 0; loop < inc; ++loop)
                {
                    ++m_current;
                }
                return *this;
            }
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

        ///
        /// *not* thread-safe: caller must guarantee sequential access to add_filter()
        ///
        template <typename V>
        void add_filter(_In_ PCWSTR counterName, V propertyValue)
        {
            FAIL_FAST_IF_MSG(
                !m_dataStopped,
                "ctWmiPerformanceCounter: must call stop_all_counters on the ctWmiPerformance class containing this counter");
            m_instanceFilter.emplace_back(counterName, std::move(ctWmiMakeVariant(propertyValue)));
        }

        ///
        /// returns a begin/end pair of interator that exposes data for each time-slice
        /// - static classes will have a null instance name
        ///
        std::pair<iterator, iterator> reference_range(_In_opt_ PCWSTR instanceName = nullptr)
        {
            FAIL_FAST_IF_MSG(
                !m_dataStopped,
                "ctWmiPerformanceCounter: must call stop_all_counters on the ctWmiPerformance class containing this counter");

            const auto lock = m_guardCounterData.lock();
            auto found_instance = std::find_if(
                std::begin(m_counterData),
                std::end(m_counterData),
                [&](const auto& instance) { return instance->match(instanceName); });
            if (std::end(m_counterData) == found_instance)
            {
                // nothing matching that instance name
                // return the end iterator (default c'tor == end)
                return std::pair<iterator, iterator>(iterator(), iterator());
            }

            const std::unique_ptr<details::ctWmiPerformanceCounterData<T>>& instanceReference = *found_instance;
            return std::pair<iterator, iterator>(instanceReference->begin(), instanceReference->end());
        }

    private:
        //
        // private stucture to track the 'filter' which instances to track
        //
        struct ctWmiPerformanceInstanceFilter
        {
            const std::wstring CounterName;
            const wil::unique_variant PropertyValue;

            ctWmiPerformanceInstanceFilter(_In_ PCWSTR counterName, wil::unique_variant&& propertyValue) :
                CounterName(counterName),
                PropertyValue(std::move(propertyValue))
            {
            }
            ~ctWmiPerformanceInstanceFilter() = default;

            ctWmiPerformanceInstanceFilter(const ctWmiPerformanceInstanceFilter& rhs) = delete;
            ctWmiPerformanceInstanceFilter& operator=(const ctWmiPerformanceInstanceFilter& rhs) = delete;

            // must const-cast to allow the const objects to be movable
            // this is safe as these are const only for the accessors of this object
            ctWmiPerformanceInstanceFilter(ctWmiPerformanceInstanceFilter&& rhs) noexcept :
                CounterName(std::move(*const_cast<std::wstring*>(&rhs.CounterName))),
                PropertyValue(std::move(*const_cast<wil::unique_variant*>(&rhs.PropertyValue)))
            {
            }
            ctWmiPerformanceInstanceFilter& operator=(ctWmiPerformanceInstanceFilter&&) noexcept = delete;

            bool operator==(_In_ IWbemObjectAccess* instance) const
            {
                return PropertyValue == details::ReadCounterFromWbemObjectAccess(instance, CounterName.c_str());
            }
            bool operator!=(IWbemObjectAccess* instance) const
            {
                return !(*this == instance);
            }

            bool operator==(_In_ IWbemClassObject* instance) const
            {
                wil::unique_variant value;
                THROW_IF_FAILED(instance->Get(CounterName.c_str(), 0, value.addressof(), nullptr, nullptr));
                // if the filter currently doesn't match anything we have, return not equal
                if (value.vt == VT_NULL)
                {
                    return false;
                }

                FAIL_FAST_IF_MSG(
                    value.vt != PropertyValue.vt,
                    "VARIANT types do not match to make a comparison : Counter name '%ws', retrieved type '%u', expected type '%u'",
                    CounterName.c_str(), value.vt, PropertyValue.vt);

                return PropertyValue == value;
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
        mutable wil::critical_section m_guardCounterData;
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
            return [this](const details::CallbackAction update_data) noexcept {
                try
                {
                    switch (update_data)
                    {
                        case details::CallbackAction::Start:
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
                            for (auto& counter_data : m_counterData)
                            {
                                counter_data->clear();
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
        template <typename TAccess>
        void add_instance(_In_ TAccess* instance)
        {
            bool fAddData = m_instanceFilter.empty();
            if (!fAddData)
            {
                fAddData = (std::end(m_instanceFilter) != std::find(
                    std::begin(m_instanceFilter),
                    std::end(m_instanceFilter),
                    instance));
            }

            // add the counter data for this instance if:
            // - have no filters [not filtering instances at all]
            // - matches at least one filter
            if (fAddData)
            {
                wil::unique_variant instance_name = details::ctQueryInstanceName(instance);
                // this would be odd but technically possible - if this isn't a static class and this instance currently doesn't have a name
                if (instance_name.vt == VT_NULL)
                {
                    return;
                }

                const auto lock = m_guardCounterData.lock();
                auto tracked_instance = std::find_if(
                    std::begin(m_counterData),
                    std::end(m_counterData),
                    [&](const auto& counter_data) { return counter_data->match(instance_name.bstrVal); });

                // if this instance of this counter is new [new unique instance for this counter]
                // - we must add a new ctWmiPerformanceCounterData to the parent's counter_data vector
                // else
                // - we just add this counter value to the already-tracked instance
                if (tracked_instance == std::end(m_counterData))
                {
                    m_counterData.push_back(
                        std::unique_ptr<details::ctWmiPerformanceCounterData<T>>
                        (std::make_unique<details::ctWmiPerformanceCounterData<T>>(m_collectionType, instance, m_counterName.c_str())));
                    (*m_counterData.rbegin())->add(instance);
                }
                else
                {
                    (*tracked_instance)->add(instance);
                }
            }
        }
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctWmiPerformanceCounterImpl
    /// - derived from the pure-virtual ctWmiPerformanceCounter class
    ///   shares the same data type template typename with the parent class
    ///
    /// Template typename details:
    ///
    /// - TEnum: the IWbem* type to refresh the performance data
    /// - TAccess: the IWbem* type used to access the performance data once refreshed
    /// - TData: the data type of the counter of the class specified in the c'tor
    ///
    /// Only 2 combinations currently supported:
    /// : ctWmiPerformanceCounter<IWbemHiPerfEnum, IWbemObjectAccess, TData>
    ///   - refreshes N of instances of a counter
    /// : ctWmiPerformanceCounter<IWbemClassObject, IWbemClassObject, TData>
    ///   - refreshes a single instance of a counter
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename TEnum, typename TAccess, typename TData>
    class ctWmiPerformanceCounterImpl final : public ctWmiPerformanceCounter<TData>
    {
    public:
        ctWmiPerformanceCounterImpl(const ctWmiService& wmi, _In_ PCWSTR className, _In_ PCWSTR counterName, const ctWmiPerformanceCollectionType collectionType) :
            ctWmiPerformanceCounter<TData>(counterName, collectionType),
            m_accessor(wmi, this->access_refresher(), className) // must qualify 'this' name lookup to access access_refresher since it's in the base class
        {
        }

        ~ctWmiPerformanceCounterImpl() = default;

        // non-copyable
        ctWmiPerformanceCounterImpl(const ctWmiPerformanceCounterImpl&) = delete;
        ctWmiPerformanceCounterImpl& operator=(const ctWmiPerformanceCounterImpl&) = delete;
        ctWmiPerformanceCounterImpl(ctWmiPerformanceCounterImpl&&) = delete;
        ctWmiPerformanceCounterImpl& operator=(ctWmiPerformanceCounterImpl&&) = delete;

    private:
        ///
        /// this concrete template class serves to capture the Enum and Access template types
        /// - so can instantiate the appropriate accessor object
        details::ctWmiPerformanceDataAccessor<TEnum, TAccess> m_accessor;

        ///
        /// invoked from the parent class to add data matching any/all filters
        ///
        /// private function required to be implemented from the abstract base class
        /// - concrete classe must pass back a function callback for adding data points for the specified counter
        ///
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

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctWmiPerformance
    ///
    /// class to register for and collect performance counters
    /// - captures counter data into the ctWmiPerformanceCounter objects passed through add()
    ///
    /// CAUTION:
    /// - do not access the ctWmiPerformanceCounter instances while between calling start() and stop()
    /// - any iterators returned can be invalidated when more data is added on the next cycle
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctWmiPerformance final
    {
    public:
        ctWmiPerformance(ctWmiService wmiService) : m_wmiService(std::move(wmiService))
        {
            m_lockedData = std::make_unique<LockedData>();
            m_refresher = wil::CoCreateInstance<WbemRefresher, IWbemRefresher>();
            m_configRefresher = m_refresher.query<IWbemConfigureRefresher>();
        }

        ~ctWmiPerformance() noexcept
        {
            // when being destroyed, don't invoke callbacks
            // the counters might be destroyed and we don't hold a ref on them
            if (m_lockedData)
            {
                auto lock = m_lockedData->m_lock.lock();
                m_lockedData->countersStarted = false;
            }
            m_timer.reset();
        }

        template <typename T>
        void add_counter(const std::shared_ptr<ctWmiPerformanceCounter<T>>& perfCounterObject)
        {
            m_callbacks.push_back(perfCounterObject->register_callback());
            auto revert_callback = wil::scope_exit([&]() noexcept { m_callbacks.pop_back(); });

            THROW_IF_FAILED(m_configRefresher->AddRefresher(perfCounterObject->m_refresher.get(), 0, nullptr));
            // dismiss scope-guard - successfully added refresher
            revert_callback.release();
        }

        void start_all_counters(unsigned interval)
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
                m_lockedData->countersStarted = true;
                m_timerInterval = interval;
                FILETIME relativeTimeout = wil::filetime::from_int64(-1 * wil::filetime_duration::one_millisecond * m_timerInterval);
                SetThreadpoolTimer(m_timer.get(), &relativeTimeout, 0, 0);
            }
        }
        // no-throw / no-fail
        void stop_all_counters() noexcept
        {
            {
                auto lock = m_lockedData->m_lock.lock();
                m_lockedData->countersStarted = false;
            }

            m_timer.reset();

            for (auto& callback : m_callbacks)
            {
                callback(details::CallbackAction::Stop);
            }
        }

        // no-throw / no-fail
        void clear_counter_data() noexcept
        {
            for (auto& callback : m_callbacks)
            {
                callback(details::CallbackAction::Clear);
            }
        }

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
        // timer to fire to indicate when to Refresh the data
        // declare last to guarantee will be destroyed first
        unsigned long m_timerInterval{};
        wil::unique_threadpool_timer m_timer;

        // must dynamically allocate this as the critical_section isn't movable
        // and the ctWmiPerformance objects must be movable
        struct LockedData
        {
            wil::critical_section m_lock;
            bool countersStarted = false;
        };
        std::unique_ptr<LockedData> m_lockedData;

        static void NTAPI TimerCallback(PTP_CALLBACK_INSTANCE, PVOID pContext, PTP_TIMER) noexcept
        {
            auto* pThis = static_cast<ctWmiPerformance*>(pContext);
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
            if (pThis->m_lockedData->countersStarted)
            {
                FILETIME relativeTimeout = wil::filetime::from_int64(-1 * wil::filetime_duration::one_millisecond * pThis->m_timerInterval);
                SetThreadpoolTimer(pThis->m_timer.get(), &relativeTimeout, 0, 0);
            }
        }
    };


    enum class ctWmiEnumClassType
    {
        Uninitialized,
        Static, // ctMakeStaticPerfCounter
        Instance // created with ctMakeInstancePerfCounter
    };

    enum class ctWmiEnumClassName
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
        WinsockBsp
    };

    struct ctWmiPerformanceCounterProperties
    {
        const ctWmiEnumClassType classType = ctWmiEnumClassType::Uninitialized;
        const ctWmiEnumClassName className = ctWmiEnumClassName::Uninitialized;
        const wchar_t* providerName = nullptr;

        const unsigned long ulongFieldNameCount = 0;
        const wchar_t** ulongFieldNames = nullptr;

        const unsigned long ulonglongFieldNameCount = 0;
        const wchar_t** ulonglongFieldNames = nullptr;

        const unsigned long stringFieldNameCount = 0;
        const wchar_t** stringFieldNames = nullptr;

        template <typename T> bool PropertyNameExists(_In_ PCWSTR name) const noexcept;
    };

    template <>
    inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<ULONG>(_In_ PCWSTR name) const noexcept  // NOLINT(bugprone-exception-escape)
    {
        for (unsigned counter = 0; counter < this->ulongFieldNameCount; ++counter)
        {
            if (ctString::ctOrdinalEqualsCaseInsensative(name, this->ulongFieldNames[counter]))
            {
                return true;
            }
        }

        return false;
    }
    template <>
    inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<ULONGLONG>(_In_ PCWSTR name) const noexcept  // NOLINT(bugprone-exception-escape)
    {
        for (unsigned counter = 0; counter < this->ulonglongFieldNameCount; ++counter)
        {
            if (ctString::ctOrdinalEqualsCaseInsensative(name, this->ulonglongFieldNames[counter]))
            {
                return true;
            }
        }

        return false;
    }
    template <>
    inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<std::wstring>(_In_ PCWSTR name) const noexcept // NOLINT(bugprone-exception-escape)
    {
        for (unsigned counter = 0; counter < this->stringFieldNameCount; ++counter)
        {
            if (ctString::ctOrdinalEqualsCaseInsensative(name, this->stringFieldNames[counter]))
            {
                return true;
            }
        }

        return false;
    }
    template <>
    inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<wil::unique_bstr>(_In_ PCWSTR name) const noexcept  // NOLINT(bugprone-exception-escape)
    {
        for (unsigned counter = 0; counter < this->stringFieldNameCount; ++counter)
        {
            if (ctString::ctOrdinalEqualsCaseInsensative(name, this->stringFieldNames[counter]))
            {
                return true;
            }
        }

        return false;
    }

    namespace ctWmiPerformanceDetails
    {
        inline const wchar_t* g_CommonStringPropertyNames[]{
            L"Caption",
            L"Description",
            L"Name" };

        inline const wchar_t* g_MemoryCounter = L"Win32_PerfFormattedData_PerfOS_Memory";
        inline const wchar_t* g_MemoryUlongCounterNames[]{
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
        inline const wchar_t* g_MemoryUlonglongCounterNames[]{
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

        inline const wchar_t* g_ProcessorInformationCounter = L"Win32_PerfFormattedData_Counters_ProcessorInformation";
        inline const wchar_t* g_ProcessorInformationUlongCounterNames[]{
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
        inline const wchar_t* g_ProcessorInformationUlonglongCounterNames[]{
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

        inline const wchar_t* g_PerfProcProcessCounter = L"Win32_PerfFormattedData_PerfProc_Process";
        inline const wchar_t* g_PerfProcProcessUlongCounterNames[]{
            L"CreatingProcessID",
            L"HandleCount",
            L"IDProcess",
            L"PageFaultsPerSec",
            L"PoolNonpagedBytes",
            L"PoolPagedBytes",
            L"PriorityBase",
            L"ThreadCount"
        };
        inline const wchar_t* g_PerfProcProcessUlonglongCounterNames[]{
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

        inline const wchar_t* g_TcpipNetworkAdapterCounter = L"Win32_PerfFormattedData_Tcpip_NetworkAdapter";
        inline const wchar_t* g_TcpipNetworkAdapterULongLongCounterNames[]{
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

        inline const wchar_t* g_TcpipNetworkInterfaceCounter = L"Win32_PerfFormattedData_Tcpip_NetworkInterface";
        inline const wchar_t* g_TcpipNetworkInterfaceULongLongCounterNames[]{
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

        inline const wchar_t* g_TcpipIpv4Counter = L"Win32_PerfFormattedData_Tcpip_IPv4";
        inline const wchar_t* g_TcpipIpv6Counter = L"Win32_PerfFormattedData_Tcpip_IPv6";
        inline const wchar_t* g_TcpipIpULongCounterNames[]{
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

        inline const wchar_t* g_TcpipTcpv4Counter = L"Win32_PerfFormattedData_Tcpip_TCPv4";
        inline const wchar_t* g_TcpipTcpv6Counter = L"Win32_PerfFormattedData_Tcpip_TCPv6";
        inline const wchar_t* g_TcpipTcpULongCounterNames[]{
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

        inline const wchar_t* g_TcpipUdpv4Counter = L"Win32_PerfFormattedData_Tcpip_UDPv4";
        inline const wchar_t* g_TcpipUdpv6Counter = L"Win32_PerfFormattedData_Tcpip_UDPv6";
        inline const wchar_t* g_TcpipUdpULongCounterNames[]{
            L"DatagramsNoPortPersec",
            L"DatagramsReceivedErrors",
            L"DatagramsReceivedPersec",
            L"DatagramsSentPersec",
            L"DatagramsPersec"
        };

        inline const wchar_t* g_TcpipPerformanceDiagnosticsCounter = L"Win32_PerfFormattedData_TCPIPCounters_TCPIPPerformanceDiagnostics";
        inline const wchar_t* g_TcpipPerformanceDiagnosticsULongCounterNames[]{
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

        inline const wchar_t* g_MicrosoftWinsockBspCounter = L"Win32_PerfFormattedData_AFDCounters_MicrosoftWinsockBSP";
        inline const wchar_t* g_MicrosoftWinsockBspuLongCounterNames[]{
            L"DroppedDatagrams",
            L"DroppedDatagramsPersec",
            L"RejectedConnections",
            L"RejectedConnectionsPersec"
        };

        // this patterns (const array of wchar_t* pointers)
        // allows for compile-time construction of the array of properties
        inline const ctWmiPerformanceCounterProperties c_PerformanceCounterPropertiesArray[]{

        {
            ctWmiEnumClassType::Static,
            ctWmiEnumClassName::Memory,
            g_MemoryCounter,
            ARRAYSIZE(g_MemoryUlongCounterNames),
            g_MemoryUlongCounterNames,
            ARRAYSIZE(g_MemoryUlonglongCounterNames),
            g_MemoryUlonglongCounterNames,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Instance,
            ctWmiEnumClassName::Processor,
            g_ProcessorInformationCounter,
            ARRAYSIZE(g_ProcessorInformationUlongCounterNames),
            g_ProcessorInformationUlongCounterNames,
            ARRAYSIZE(g_ProcessorInformationUlonglongCounterNames),
            g_ProcessorInformationUlonglongCounterNames,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Instance,
            ctWmiEnumClassName::Process,
            g_PerfProcProcessCounter,
            ARRAYSIZE(g_PerfProcProcessUlongCounterNames),
            g_PerfProcProcessUlongCounterNames,
            ARRAYSIZE(g_PerfProcProcessUlonglongCounterNames),
            g_PerfProcProcessUlonglongCounterNames,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Instance,
            ctWmiEnumClassName::NetworkAdapter,
            g_TcpipNetworkAdapterCounter,
            0,
            nullptr,
            ARRAYSIZE(g_TcpipNetworkAdapterULongLongCounterNames),
            g_TcpipNetworkAdapterULongLongCounterNames,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Instance,
            ctWmiEnumClassName::NetworkInterface,
            g_TcpipNetworkInterfaceCounter,
            0,
            nullptr,
            ARRAYSIZE(g_TcpipNetworkInterfaceULongLongCounterNames),
            g_TcpipNetworkInterfaceULongLongCounterNames,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Static,
            ctWmiEnumClassName::TcpipIpv4,
            g_TcpipIpv4Counter,
            ARRAYSIZE(g_TcpipIpULongCounterNames),
            g_TcpipIpULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Static,
            ctWmiEnumClassName::TcpipIpv6,
            g_TcpipIpv6Counter,
            ARRAYSIZE(g_TcpipIpULongCounterNames),
            g_TcpipIpULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Static,
            ctWmiEnumClassName::TcpipTcpv4,
            g_TcpipTcpv4Counter,
            ARRAYSIZE(g_TcpipTcpULongCounterNames),
            g_TcpipTcpULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Static,
            ctWmiEnumClassName::TcpipTcpv6,
            g_TcpipTcpv6Counter,
            ARRAYSIZE(g_TcpipTcpULongCounterNames),
            g_TcpipTcpULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Static,
            ctWmiEnumClassName::TcpipUdpv4,
            g_TcpipUdpv4Counter,
            ARRAYSIZE(g_TcpipUdpULongCounterNames),
            g_TcpipUdpULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Static,
            ctWmiEnumClassName::TcpipUdpv6,
            g_TcpipUdpv6Counter,
            ARRAYSIZE(g_TcpipUdpULongCounterNames),
            g_TcpipUdpULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Static,
            ctWmiEnumClassName::TcpipDiagnostics,
            g_TcpipPerformanceDiagnosticsCounter,
            ARRAYSIZE(g_TcpipPerformanceDiagnosticsULongCounterNames),
            g_TcpipPerformanceDiagnosticsULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        },

        {
            ctWmiEnumClassType::Static,
            ctWmiEnumClassName::WinsockBsp,
            g_MicrosoftWinsockBspCounter,
            ARRAYSIZE(g_MicrosoftWinsockBspuLongCounterNames),
            g_MicrosoftWinsockBspuLongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(g_CommonStringPropertyNames),
            g_CommonStringPropertyNames
        }

        };
    }

    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeStaticPerfCounter(
        ctWmiService wmi, _In_ PCWSTR className, _In_ PCWSTR counterName, ctWmiPerformanceCollectionType collectionType = ctWmiPerformanceCollectionType::Detailed)
    {
        // 'static' WMI PerfCounters enumerate via IWbemClassObject and accessed/refreshed via IWbemClassObject
        return std::make_shared<ctWmiPerformanceCounterImpl<IWbemClassObject, IWbemClassObject, T>>(wmi, className, counterName, collectionType);
    }

    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeInstancePerfCounter(
        ctWmiService wmi, _In_ PCWSTR className, _In_ PCWSTR counterName, ctWmiPerformanceCollectionType collectionType = ctWmiPerformanceCollectionType::Detailed)
    {
        // 'instance' WMI perf objects are enumerated through the IWbemHiPerfEnum interface and accessed/refreshed through the IWbemObjectAccess interface
        return std::make_shared<ctWmiPerformanceCounterImpl<IWbemHiPerfEnum, IWbemObjectAccess, T>>(wmi, className, counterName, collectionType);
    }

    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctCreatePerfCounter(
        ctWmiService wmi, ctWmiEnumClassName className, _In_ PCWSTR counterName, ctWmiPerformanceCollectionType collectionType = ctWmiPerformanceCollectionType::Detailed)
    {
        const ctWmiPerformanceCounterProperties* foundProperty = nullptr;
        for (const auto& counterProperty : ctWmiPerformanceDetails::c_PerformanceCounterPropertiesArray)
        {
            if (className == counterProperty.className)
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

        if (foundProperty->classType == ctWmiEnumClassType::Static)
        {
            return ctMakeStaticPerfCounter<T>(wmi, foundProperty->providerName, counterName, collectionType);
        }

        FAIL_FAST_IF(foundProperty->classType != ctWmiEnumClassType::Instance);
        return ctMakeInstancePerfCounter<T>(wmi, foundProperty->providerName, counterName, collectionType);
    }
} // ctl namespace
