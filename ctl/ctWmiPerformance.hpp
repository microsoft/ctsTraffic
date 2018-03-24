/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

#include <iterator>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <string>
#include <tuple>
#include <algorithm>

#include <Windows.h>

#include <ctThreadPoolTimer.hpp>
#include <ctException.hpp>
#include <ctWmiInitialize.hpp>
#include <ctScopeGuard.hpp>
#include <ctString.hpp>
#include <ctLocks.hpp>


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

namespace ctl {
    enum class ctWmiPerformanceCollectionType
    {
        Detailed,
        MeanOnly,
        FirstLast
    };

    namespace details {
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Function to return the performance data of the specified property from the input instance
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        inline
        ctComVariant ctReadIWbemObjectAccess(IWbemObjectAccess* _instance, LPCWSTR _counter_name)
        {
            LONG property_handle;
            CIMTYPE property_type;
            HRESULT hr = _instance->GetPropertyHandle(_counter_name, &property_type, &property_handle);
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemObjectAccess::GetPropertyHandle", L"ctWmiPerformance::ctReadIWbemObjectAccess", false);
            }

            ctComVariant current_value;
            switch (property_type) {
                case CIM_SINT32:
                case CIM_UINT32: {
                    ULONG value;
                    hr = _instance->ReadDWORD(property_handle, &value);
                    if (FAILED(hr)) {
                        throw ctException(hr, L"IWbemObjectAccess::ReadDWORD", L"ctWmiPerformance::ctReadIWbemObjectAccess", false);
                    }
                    current_value.assign<VT_UI4>(value);
                    break;
                }

                case CIM_SINT64:
                case CIM_UINT64: {
                    ULONGLONG value;
                    hr = _instance->ReadQWORD(property_handle, &value);
                    if (FAILED(hr)) {
                        throw ctException(hr, L"IWbemObjectAccess::ReadQWORD", L"ctWmiPerformance::ctReadIWbemObjectAccess", false);
                    }
                    current_value.assign<VT_UI8>(value);
                    break;
                }

                case CIM_STRING: {
                    ctComBstr value;
                    value.resize(64);
                    long value_size = static_cast<long>(value.size() * sizeof(WCHAR));  // NOLINT
                    long returned_size;
                    hr = _instance->ReadPropertyValue(property_handle, value_size, &returned_size, reinterpret_cast<BYTE*>(value.get()));
                    if (WBEM_E_BUFFER_TOO_SMALL == hr) {
                        value_size = returned_size;
                        value.resize(value_size);
                        hr = _instance->ReadPropertyValue(property_handle, value_size, &returned_size, reinterpret_cast<BYTE*>(value.get()));
                    }
                    if (FAILED(hr)) {
                        throw ctException(hr, L"IWbemObjectAccess::ReadPropertyValue", L"ctWmiPerformance::ctReadIWbemObjectAccess", false);
                    }
                    current_value.assign<VT_BSTR>(value.get());
                    break;
                }

                default:
                    throw ctException(
                        ERROR_INVALID_DATA,
                        ctString::format_string(
                            L"ctWmiPerformance only supports data of type INT32, INT64, and BSTR: counter %ws is of type %u",
                            _counter_name, static_cast<unsigned>(property_type)).c_str(),
                        L"ctWmiPerformance::ctReadIWbemObjectAccess",
                        true);
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
        class ctWmiPerformanceDataAccessor {
        public:
            typedef typename std::vector<TAccess*>::const_iterator access_iterator;

            ctWmiPerformanceDataAccessor(ctComPtr<IWbemConfigureRefresher> _config, LPCWSTR _classname);

            ~ctWmiPerformanceDataAccessor() NOEXCEPT
            {
                clear();
            }

            ///
            /// refreshes internal data with the latest performance data
            ///
            void refresh();

            access_iterator begin() const NOEXCEPT
            {
                return accessor_objects.cbegin();
            }
            access_iterator end() const NOEXCEPT
            {
                return accessor_objects.cend();
            }

            // non-copyable
            ctWmiPerformanceDataAccessor(const ctWmiPerformanceDataAccessor&) = delete;
            ctWmiPerformanceDataAccessor& operator=(const ctWmiPerformanceDataAccessor&) = delete;

            // movable
            ctWmiPerformanceDataAccessor(ctWmiPerformanceDataAccessor&& rhs) NOEXCEPT :
                enumeration_object(std::move(rhs.enumeration_object)),
                accessor_objects(std::move(rhs.accessor_objects)),
                current_iterator(std::move(rhs.current_iterator))
            {
                // since accessor_objects is storing raw pointers, manually clear out the rhs object
                // so they won't be double-deleted
                rhs.accessor_objects.clear();
                rhs.current_iterator = rhs.accessor_objects.end();
            }
            ctWmiPerformanceDataAccessor& operator=(ctWmiPerformanceDataAccessor&& rhs) NOEXCEPT
            {
                enumeration_object = std::move(rhs.enumeration_object);
                accessor_objects = std::move(rhs.accessor_objects);
                current_iterator = std::move(rhs.current_iterator);
                // since accessor_objects is storing raw pointers, manually clear out the rhs object
                // so they won't be double-deleted
                rhs.accessor_objects.clear();
                rhs.current_iterator = rhs.accessor_objects.end();
                return *this;
            }

        private:
            // members
            ctComPtr<TEnum> enumeration_object;
            // TAccess pointers are returned through enumeration_object::GetObjects, reusing the same vector for each refresh call
            std::vector<TAccess*> accessor_objects;
            access_iterator current_iterator;

            void clear() NOEXCEPT;
        };

        inline
        ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::ctWmiPerformanceDataAccessor(ctComPtr<IWbemConfigureRefresher> _config, LPCWSTR _classname)
        : enumeration_object(),
          accessor_objects(),
          current_iterator(accessor_objects.end())
        {
            // must load COM and WMI locally, though both are still required globally
            ctComInitialize com;
            ctWmiService wmi(L"root\\cimv2");

            LONG lid;
            const auto hr = _config->AddEnum(wmi.get(), _classname, 0, nullptr, enumeration_object.get_addr_of(), &lid);
            if (FAILED(hr)) {
                throw ctException(
                    hr,
                    L"IWbemConfigureRefresher::AddEnum",
                    L"ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::ctWmiPerformanceDataAccessor",
                    false);
            }
        }

        inline
        ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::ctWmiPerformanceDataAccessor(ctComPtr<IWbemConfigureRefresher> _config, LPCWSTR _classname)
        : enumeration_object(),
          accessor_objects(),
          current_iterator(accessor_objects.end())
        {
            // must load COM and WMI locally, though both are still required globally
            ctComInitialize com;
            ctWmiService wmi(L"root\\cimv2");

            ctWmiEnumerate enum_instances(wmi);
            enum_instances.query(ctString::format_string(L"SELECT * FROM %ws", _classname).c_str());
            if (enum_instances.begin() == enum_instances.end()) {
                throw ctException(
                    ERROR_NOT_FOUND,
                    ctString::format_string(
                        L"Failed to refresh a static instances of the WMI class %ws",
                        _classname).c_str(),
                    L"ctWmiPerformanceDataAccessor",
                    true);
            }

            auto instance = *enum_instances.begin();
            LONG lid;
            const auto hr = _config->AddObjectByTemplate(
                wmi.get(),
                instance.get_instance().get(),
                0,
                nullptr,
                enumeration_object.get_addr_of(),
                &lid);
            if (FAILED(hr)) {
                throw ctException(
                    hr,
                    L"IWbemConfigureRefresher::AddObjectByTemplate",
                    L"ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::ctWmiPerformanceDataAccessor",
                    false);
            }

            // setting the raw pointer in the access vector to behave with the iterator
            accessor_objects.push_back(enumeration_object.get());
        }

        template <>
        inline void ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::refresh()
        {
            clear();

            ULONG objects_returned = 0;
            HRESULT hr = enumeration_object->GetObjects(
                0,
                static_cast<ULONG>(accessor_objects.size()),
                accessor_objects.empty() ? nullptr : &accessor_objects[0],
                &objects_returned);

            if (WBEM_E_BUFFER_TOO_SMALL == hr) {
                accessor_objects.resize(objects_returned);
                hr = enumeration_object->GetObjects(
                    0,
                    static_cast<ULONG>(accessor_objects.size()),
                    &accessor_objects[0],
                    &objects_returned);
            }
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemObjectAccess::GetObjects", L"ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::refresh", false);
            }

            accessor_objects.resize(objects_returned);
            current_iterator = accessor_objects.begin();
        }

        template <>
        inline void ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::refresh()
        {
            // the underlying IWbemClassObject is already refreshed
            // accessor_objects will only ever have a singe instance
            ctFatalCondition(
                accessor_objects.size() != 1,
                L"ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>: for IWbemClassObject performance classes there can only ever have the single instance being tracked - instead has %Iu",
                accessor_objects.size());

            current_iterator = accessor_objects.begin();
        }

        template <>
        inline void ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::clear() NOEXCEPT
        {
            for (IWbemObjectAccess* _object : accessor_objects) {
                _object->Release();
            }
            accessor_objects.clear();
            current_iterator = accessor_objects.end();
        }

        template <>
        inline void ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::clear() NOEXCEPT
        {
            current_iterator = accessor_objects.end();
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
        class ctWmiPeformanceCounterData {
        private:
            mutable CRITICAL_SECTION guard_data{};
            const ctWmiPerformanceCollectionType collection_type = ctWmiPerformanceCollectionType::Detailed;
            const std::wstring instance_name;
            const std::wstring counter_name;
            std::vector<T> counter_data;
            ULONGLONG counter_sum = 0;

            void add_data(const T& instance_data)
            {
                ctAutoReleaseCriticalSection auto_guard(&guard_data);
                switch (collection_type) {
                    case ctWmiPerformanceCollectionType::Detailed:
                        counter_data.push_back(instance_data);
                        break;

                    case ctWmiPerformanceCollectionType::MeanOnly:
                        // vector is formatted as:
                        // [0] == count
                        // [1] == min
                        // [2] == max
                        // [3] == mean
                        if (counter_data.empty()) {
                            counter_data.push_back(1);
                            counter_data.push_back(instance_data);
                            counter_data.push_back(instance_data);
                            counter_data.push_back(0);
                        } else {
                            ++counter_data[0];

                            if (instance_data < counter_data[1]) {
                                counter_data[1] = instance_data;
                            }
                            if (instance_data > counter_data[2]) {
                                counter_data[2] = instance_data;
                            }
                        }

                        counter_sum += instance_data;
                        break;

                    case ctWmiPerformanceCollectionType::FirstLast:
                        // the first data point write both min and max
                        // [0] == count
                        // [1] == first
                        // [2] == last
                        if (counter_data.empty()) {
                            counter_data.push_back(1);
                            counter_data.push_back(instance_data);
                            counter_data.push_back(instance_data);
                        } else {
                            ++counter_data[0];
                            counter_data[2] = instance_data;
                        }
                        break;

                    default:
                        ctAlwaysFatalCondition(
                            L"Unknown ctWmiPerformanceCollectionType (%u)",
                            static_cast<unsigned>(collection_type));
                }
            }

            typename std::vector<T>::const_iterator access_begin() NOEXCEPT
            {
                ctAutoReleaseCriticalSection auto_guard(&guard_data);
                // when accessing data, calculate the mean
                if (ctWmiPerformanceCollectionType::MeanOnly == collection_type) {
                    counter_data[3] = static_cast<T>(counter_sum / counter_data[0]);
                }
                return counter_data.cbegin();
            }

            typename std::vector<T>::const_iterator access_end() const NOEXCEPT
            {
                ctAutoReleaseCriticalSection auto_guard(&guard_data);
                return counter_data.cend();
            }

        public:
            ctWmiPeformanceCounterData(
                const ctWmiPerformanceCollectionType _collection_type,
                IWbemObjectAccess* _instance,
                LPCWSTR _counter)
            : collection_type(_collection_type),
              instance_name(ctReadIWbemObjectAccess(_instance, L"Name")->bstrVal),
              counter_name(_counter)
            {
                if (!::InitializeCriticalSectionEx(&guard_data, 4000, 0)) {
                    const auto gle = ::GetLastError();
                    ctAlwaysFatalCondition(
                        ctString::format_string(
                            L"InitializeCriticalSectionEx failed with error %ul",
                            gle).c_str());
                }
            }
            ctWmiPeformanceCounterData(
                const ctWmiPerformanceCollectionType _collection_type,
                IWbemClassObject* _instance,
                LPCWSTR _counter)
            : collection_type(_collection_type),
              counter_name(_counter)
            {
                if (!::InitializeCriticalSectionEx(&guard_data, 4000, 0)) {
                    const auto gle = ::GetLastError();
                    ctAlwaysFatalCondition(
                        ctString::format_string(
                            L"InitializeCriticalSectionEx failed with error %ul",
                            gle).c_str());
                }

                ctComVariant value;
                const auto hr = _instance->Get(L"Name", 0, value.get(), nullptr, nullptr);
                if (FAILED(hr)) {
                    throw ctException(hr, L"IWbemClassObject::Get(Name)", L"ctWmiPerformanceCounterData", false);
                }
                // Name is expected to be NULL in this case
                // - since IWbemClassObject is expected to be a single instance
                if (!value.is_null()) {
                    throw ctException(
                        ERROR_INVALID_DATA,
                        ctString::format_string(
                            L"ctWmiPeformanceCounterData was given an IWbemClassObject to track that had a non-null 'Name' key field ['%ws']. Expected to be a NULL key field as to only support single-instances",
                            value->bstrVal).c_str(),
                        L"ctWmiPeformanceCounterData",
                        true);
                }
            }
            ~ctWmiPeformanceCounterData() NOEXCEPT
            {
                ::DeleteCriticalSection(&guard_data);
            }

            /// _instance_name == nullptr means match everything
            /// - allows for the caller to not have to pass Name filters multiple times
            bool match(_In_opt_ LPCWSTR _instance_name) const NOEXCEPT
            {
                if (nullptr == _instance_name) {
                    return true;
                }
                if (instance_name.empty()) {
                    return nullptr == _instance_name;

                }
                return ctString::iordinal_equals(instance_name, _instance_name);
            }

            void add(IWbemObjectAccess* _instance)
            {
                T instance_data;
                ctReadIWbemObjectAccess(_instance, counter_name.c_str()).retrieve(&instance_data);
                add_data(instance_data);
            }
            void add(IWbemClassObject* _instance)
            {
                ctComVariant value;
                const HRESULT hr = _instance->Get(counter_name.c_str(), 0, value.get(), nullptr, nullptr);
                if (FAILED(hr)) {
                    throw ctException(
                        hr,
                        ctString::format_string(
                            L"IWbemClassObject::Get(%ws)",
                            counter_name.c_str()).c_str(),
                        L"ctWmiPeformanceCounterData<T>::add",
                        true);
                }

                T instance_data;
                add_data(value.retrieve(&instance_data));
            }

            typename std::vector<T>::const_iterator begin() NOEXCEPT
            {
                ctAutoReleaseCriticalSection auto_guard(&guard_data);
                return access_begin();
            }
            typename std::vector<T>::const_iterator end() const NOEXCEPT
            {
                ctAutoReleaseCriticalSection auto_guard(&guard_data);
                return access_end();
            }

            size_t count() NOEXCEPT
            {
                ctAutoReleaseCriticalSection auto_guard(&guard_data);
                return access_end() - access_begin();
            }

            void clear() NOEXCEPT
            {
                ctAutoReleaseCriticalSection auto_guard(&guard_data);
                counter_data.clear();
                counter_sum = 0;
            }

            // non-copyable
            ctWmiPeformanceCounterData(const ctWmiPeformanceCounterData&) = delete;
            ctWmiPeformanceCounterData& operator= (const ctWmiPeformanceCounterData&) = delete;

            // not movable
            ctWmiPeformanceCounterData(ctWmiPeformanceCounterData&&) = delete;
            ctWmiPeformanceCounterData& operator=(ctWmiPeformanceCounterData&&) = delete;
        };

        ///
        /// WMI passes around 64-bit integers as BSTR's, so must specialize reading those values to do the proper conversion
        ///
        template <>
        inline void ctWmiPeformanceCounterData<ULONGLONG>::add(IWbemClassObject* _instance)
        {
            ctComVariant value;
            const auto hr = _instance->Get(counter_name.c_str(), 0, value.get(), nullptr, nullptr);
            if (FAILED(hr)) {
                throw ctException(
                    hr,
                    ctString::format_string(
                        L"IWbemClassObject::Get(%ws)",
                        counter_name.c_str()).c_str(),
                    L"ctWmiPeformanceCounterData<ULONGLONG>::add",
                    true);
            }
            if (value->vt != VT_BSTR) {
                throw ctException(
                    value->vt,
                    L"Expected a BSTR type to read a ULONGLONG from the IWbemClassObject - unexpected variant type",
                    L"ctWmiPeformanceCounterData<ULONGLONG>::add",
                    false);
            }

            add_data(::_wcstoui64(value->bstrVal, nullptr, 10));
        }

        template <>
        inline void ctWmiPeformanceCounterData<LONGLONG>::add(IWbemClassObject* _instance)
        {
            ctComVariant value;
            const auto hr = _instance->Get(counter_name.c_str(), 0, value.get(), nullptr, nullptr);
            if (FAILED(hr)) {
                throw ctException(
                    hr,
                    ctString::format_string(
                        L"IWbemClassObject::Get(%ws)",
                        counter_name.c_str()).c_str(),
                    L"ctWmiPeformanceCounterData<LONGLONG>::add",
                    true);
            }
            if (value->vt != VT_BSTR) {
                throw ctException(
                    value->vt,
                    L"Expected a BSTR type to read a ULONGLONG from the IWbemClassObject - unexpected variant type",
                    L"ctWmiPeformanceCounterData<LONGLONG>::add",
                    false);
            }

            add_data(::_wcstoi64(value->bstrVal, nullptr, 10));
        }

        inline ctComVariant ctQueryInstanceName(IWbemObjectAccess* _instance)
        {
            return ctReadIWbemObjectAccess(_instance, L"Name");
        }

        inline ctComVariant ctQueryInstanceName(IWbemClassObject* _instance)
        {
            ctComVariant value;
            const auto hr = _instance->Get(L"Name", 0, value.get(), nullptr, nullptr);
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemClassObject::Get(Name)", L"ctQueryInstanceName", false);
            }
            return value;
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// type for the callback implemented in all ctWmiPerformanceCounter classes
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        enum class CallbackAction {
            Start,
            Stop,
            Update,
            Clear
        };
        typedef std::function<void(CallbackAction _action)> ctWmiPerformanceCallback;
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

    ///
    /// forward-declaration to reference ctWmiPerformance
    ///
    class ctWmiPerformance;
    template <typename T>
    class ctWmiPerformanceCounter {
    public:
        ///
        /// iterator
        ///
        /// iterates across *time-slices* captured over from ctWmiPerformance
        ///
        class iterator : public std::iterator<std::forward_iterator_tag, T> {
        private:
            typename std::vector<T>::const_iterator current;
            bool is_empty;

        public:
            explicit iterator(typename std::vector<T>::const_iterator && _instance) NOEXCEPT
            : current(std::move(_instance)),
              is_empty(false)
            {
            }
            iterator() NOEXCEPT : current(), is_empty(true)
            {
            }
            ~iterator() = default;

            ////////////////////////////////////////////////////////////////////////////////
            ///
            /// copy c'tor and copy assignment
            /// move c'tor and move assignment
            ///
            ////////////////////////////////////////////////////////////////////////////////
            iterator(iterator&& _i) NOEXCEPT
            : current(std::move(_i.current)),
              is_empty(std::move(_i.is_empty))
            {
            }
            iterator& operator =(iterator&& _i) NOEXCEPT
            {
                current = std::move(_i.current);
                is_empty = std::move(_i.is_empty);
                return *this;
            }

            iterator(const iterator& _i) NOEXCEPT
            : current(_i.current),
              is_empty(_i.is_empty)
            {
            }
            iterator& operator =(const iterator& i) NOEXCEPT
            {
                iterator local_copy(i);
                *this = std::move(local_copy);
                return *this;
            }

            const T& operator* () const
            {
                if (is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : dereferencing an iterator referencing an empty container");
                }
                return *current;
            }

            /*
            T* operator-> ()
            {
                if (is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : dereferencing an iterator referencing an empty container");
                }
                &(current);
            }
            */

            bool operator==(const iterator& _iter) const NOEXCEPT
            {
                if (is_empty || _iter.is_empty) {
                    return (is_empty == _iter.is_empty);
                }
                return (current == _iter.current);
            }
            bool operator!=(const iterator& _iter) const NOEXCEPT
            {
                return !(*this == _iter);
            }

            // preincrement
            iterator& operator++()
            {
                if (is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : preincrementing an iterator referencing an empty container");
                }
                ++current;
                return *this;
            }
            // postincrement
            iterator operator++(int)
            {
                if (is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : postincrementing an iterator referencing an empty container");
                }
                iterator temp(*this);
                ++current;
                return temp;
            }
            // increment by integer
            iterator& operator+=(size_t _inc)
            {
                if (is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : postincrementing an iterator referencing an empty container");
                }
                for (size_t loop = 0; loop < _inc; ++loop) {
                    ++current;
                }
                return *this;
            }
        };

        ctWmiPerformanceCounter(LPCWSTR _counter_name, const ctWmiPerformanceCollectionType _collection_type)
        : collection_type(_collection_type),
          counter_name(_counter_name)
        {
            refresher = ctComPtr<IWbemRefresher>::createInstance(CLSID_WbemRefresher, IID_IWbemRefresher);
            const auto hr = refresher->QueryInterface(IID_IWbemConfigureRefresher, reinterpret_cast<void**>(configure_refresher.get_addr_of()));
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemRefresher::QueryInterface", L"ctWmiPerformanceCounter", false);
            }
        }

        virtual ~ctWmiPerformanceCounter() = default;

        ctWmiPerformanceCounter(const ctWmiPerformanceCounter&) = delete;
        ctWmiPerformanceCounter& operator=(const ctWmiPerformanceCounter&) = delete;
        ctWmiPerformanceCounter(ctWmiPerformanceCounter&&) = delete;
        ctWmiPerformanceCounter& operator=(ctWmiPerformanceCounter&&) = delete;

        ///
        /// *not* thread-safe: caller must guarantee sequential access to add_filter()
        ///
        template <typename V>
        void add_filter(LPCWSTR _counter_name, V _property_value)
        {
            ctFatalCondition(
                !data_stopped,
                L"ctWmiPerformanceCounter: must call stop_all_counters on the ctWmiPerformance class containing this counter");
            instance_filter.emplace_back(_counter_name, ctWmiMakeVariant(_property_value));
        }

        ///
        /// returns a begin/end pair of interator that exposes data for each time-slice
        /// - static classes will have a null instance name
        ///
        std::pair<iterator, iterator> reference_range(_In_opt_ LPCWSTR _instance_name = nullptr)
        {
            ctFatalCondition(
                !data_stopped,
                L"ctWmiPerformanceCounter: must call stop_all_counters on the ctWmiPerformance class containing this counter");

            auto found_instance = std::find_if(
                std::begin(counter_data),
                std::end(counter_data),
                [&] (const std::unique_ptr<details::ctWmiPeformanceCounterData<T>>& _instance) {
                return _instance->match(_instance_name);
            });
            if (std::end(counter_data) == found_instance) {
                // nothing matching that instance name
                // return the end iterator (default c'tor == end)
                return std::pair<iterator, iterator>(iterator(), iterator());
            }

            const std::unique_ptr<details::ctWmiPeformanceCounterData<T>>& instance_reference = *found_instance;
            return std::pair<iterator, iterator>(instance_reference->begin(), instance_reference->end());
        }

    private:
        //
        // private stucture to track the 'filter' which instances to track
        //
        struct ctWmiPerformanceInstanceFilter {
            const std::wstring counter_name;
            const ctComVariant property_value;

            ctWmiPerformanceInstanceFilter(LPCWSTR _counter_name, ctComVariant _property_value)
            : counter_name(_counter_name),
              property_value(std::move(_property_value))
            {
            }
            ~ctWmiPerformanceInstanceFilter() = default;
            ctWmiPerformanceInstanceFilter(const ctWmiPerformanceInstanceFilter&) = default;
            ctWmiPerformanceInstanceFilter& operator=(const ctWmiPerformanceInstanceFilter&) = default;
            ctWmiPerformanceInstanceFilter(ctWmiPerformanceInstanceFilter&&) = default;
            ctWmiPerformanceInstanceFilter& operator=(ctWmiPerformanceInstanceFilter&&) = default;

            bool operator==(IWbemObjectAccess* _instance) const
            {
                return property_value == details::ctReadIWbemObjectAccess(_instance, counter_name.c_str());
            }
            bool operator!=(IWbemObjectAccess* _instance) const
            {
                return !(*this == _instance);
            }

            bool operator==(IWbemClassObject* _instance) const
            {
                ctComVariant value;
                const auto hr = _instance->Get(counter_name.c_str(), 0, value.get(), nullptr, nullptr);
                if (FAILED(hr)) {
                    throw ctException(hr, L"IWbemClassObject::Get(counter_name)", L"ctWmiPerformanceCounterData", false);
                }
                // if the filter currently doesn't match anything we have, return not equal
                if (value->vt == VT_NULL)
                {
                    return false;
                }
                ctFatalCondition(
                    value->vt != property_value->vt,
                    L"VARIANT types do not match to make a comparison : Counter name '%ws', retrieved type '%u', expected type '%u'",
                    counter_name.c_str(), value->vt, property_value->vt);

                return property_value == value;
            }
            bool operator!=(IWbemClassObject* _instance) const
            {
                return !(*this == _instance);
            }
        };

        const ctWmiPerformanceCollectionType collection_type;
        const std::wstring counter_name;
        ctComPtr<IWbemRefresher> refresher;
        ctComPtr<IWbemConfigureRefresher> configure_refresher;
        std::vector<ctWmiPerformanceInstanceFilter> instance_filter;
        std::vector<std::unique_ptr<details::ctWmiPeformanceCounterData<T>>> counter_data;
        bool data_stopped = true;

    protected:

        virtual void update_counter_data() = 0;

        // ctWmiPerformance needs private access to invoke register_callback in the derived type
        friend class ctWmiPerformance;

        details::ctWmiPerformanceCallback register_callback()
        {
            return [this] (const details::CallbackAction _update_data) {
                switch (_update_data)
                {
                case details::CallbackAction::Start:
                    data_stopped = false;
                    break;

                case details::CallbackAction::Stop:
                    data_stopped = true;
                    break;

                case details::CallbackAction::Update:
                    // only the derived class has appropriate the accessor class to update the data
                    update_counter_data();
                    break;

                case details::CallbackAction::Clear:
                    ctFatalCondition(
                        !data_stopped,
                        L"ctWmiPerformanceCounter: must call stop_all_counters on the ctWmiPerformance class containing this counter");

                    for (auto& _counter_data : counter_data) {
                        _counter_data->clear();
                    }
                    break;
                }
            };
        }

        //
        // the derived classes need to use the same refresher object
        //
        ctComPtr<IWbemConfigureRefresher> access_refresher() const NOEXCEPT
        {
            return configure_refresher;
        }

        //
        // this function is how one looks to see if the data machines requires knowing how to access the data from that specific WMI class
        // - it's also how to access the data is captured with the TAccess template type
        //
        template <typename TAccess>
        void add_instance(TAccess* _instance)
        {
            bool fAddData = instance_filter.empty();
            if (!fAddData) {
                fAddData = (std::end(instance_filter) != std::find(
                    std::begin(instance_filter),
                    std::end(instance_filter),
                    _instance));
            }

            // add the counter data for this instance if:
            // - have no filters [not filtering instances at all]
            // - matches at least one filter
            if (fAddData) {
                ctComVariant instance_name = details::ctQueryInstanceName(_instance);

                auto tracked_instance = std::find_if(
                    std::begin(counter_data),
                    std::end(counter_data),
                    [&] (std::unique_ptr<details::ctWmiPeformanceCounterData<T>>& _counter_data) {
                    return _counter_data->match(instance_name->bstrVal);
                });

                // if this instance of this counter is new [new unique instance for this counter]
                // - we must add a new ctWmiPeformanceCounterData to the parent's counter_data vector
                // else
                // - we just add this counter value to the already-tracked instance
                if (tracked_instance == std::end(counter_data)) {
                    counter_data.push_back(
                        std::unique_ptr<details::ctWmiPeformanceCounterData<T>>
                            (new details::ctWmiPeformanceCounterData<T>(collection_type, _instance, counter_name.c_str())));
                    (*counter_data.rbegin())->add(_instance);
                } else {
                    (*tracked_instance)->add(_instance);
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
    class ctWmiPerformanceCounterImpl : public ctWmiPerformanceCounter<TData> {
    public:
        ctWmiPerformanceCounterImpl(LPCWSTR _class_name, LPCWSTR _counter_name, const ctWmiPerformanceCollectionType _collection_type)
        : ctWmiPerformanceCounter<TData>(_counter_name, _collection_type),
          accessor(this->access_refresher(), _class_name) // must qualify this name lookup to access access_refresher since it's in the base class
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
        details::ctWmiPerformanceDataAccessor<TEnum, TAccess> accessor;

        ///
        /// invoked from the parent class to add data matching any/all filters
        ///
        /// private function required to be implemented from the abstract base class
        /// - concrete classe must pass back a function callback for adding data points for the specified counter
        ///
        void update_counter_data() override
        {
            // refresh this hi-perf object to get the current values
            // requires the invoker serializes all calls
            accessor.refresh();

            // the accessor object exposes begin() / end() to allow access to instances
            // - of the specified hi-performance counter
            for (const auto& _instance : accessor) {
                // must qualify this name lookup to access add_instance since it's in the base class
                this->add_instance(_instance);
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
    class ctWmiPerformance {
    public:
        ctWmiPerformance() : wmi_service(L"root\\cimv2")
        {
            refresher = ctComPtr<IWbemRefresher>::createInstance(CLSID_WbemRefresher, IID_IWbemRefresher);
            const auto hr = refresher->QueryInterface(
                IID_IWbemConfigureRefresher,
                reinterpret_cast<void**>(config_refresher.get_addr_of()));
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemRefresher::QueryInterface(IID_IWbemConfigureRefresher)", L"ctWmiPerformance", false);
            }
        }

        virtual ~ctWmiPerformance() NOEXCEPT
        {
            stop_all_counters();
        }

        template <typename T>
        void add_counter(const std::shared_ptr<ctWmiPerformanceCounter<T>>& _wmi_perf)
        {
            callbacks.push_back(_wmi_perf->register_callback());
            ctlScopeGuard(revert_callback, { callbacks.pop_back(); });

            const HRESULT hr = config_refresher->AddRefresher(_wmi_perf->refresher.get(), 0, nullptr);
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemConfigureRefresher::AddRefresher", L"ctWmiPerformance<T>::add", false);
            }

            // dismiss scope-guard - successfully added refresher
            revert_callback.dismiss();
        }

        void start_all_counters(unsigned _interval)
        {
            for (auto& _callback : callbacks) {
                _callback(details::CallbackAction::Start);
            }
            timer.reset(new ctThreadpoolTimer);
            timer->schedule_singleton(
                [this, _interval] () { TimerCallback(this, _interval); },
                _interval);
        }
        // no-throw / no-fail
        void stop_all_counters() NOEXCEPT
        {
            if (timer) {
                timer->stop_all_timers();
            }
            for (auto& _callback : callbacks) {
                _callback(details::CallbackAction::Stop);
            }
        }

        // no-throw / no-fail
        void clear_counter_data() NOEXCEPT
        {
            for (auto& _callback : callbacks) {
                _callback(details::CallbackAction::Clear);
            }
        }

        void reset_counters()
        {
            callbacks.clear();

            // release this Refresher and ConfigRefresher, so future counters will be added cleanly
            refresher = ctComPtr<IWbemRefresher>::createInstance(CLSID_WbemRefresher, IID_IWbemRefresher);
            const auto hr = refresher->QueryInterface(
                IID_IWbemConfigureRefresher,
                reinterpret_cast<void**>(config_refresher.get_addr_of()));
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemRefresher::QueryInterface(IID_IWbemConfigureRefresher)", L"ctWmiPerformance", false);
            }
        }

        // non-copyable
        ctWmiPerformance(const ctWmiPerformance&) = delete;
        ctWmiPerformance& operator=(const ctWmiPerformance&) = delete;

        // movable
        ctWmiPerformance(ctWmiPerformance&& rhs) NOEXCEPT :
            wmi_service(std::move(rhs.wmi_service)),
            refresher(std::move(rhs.refresher)),
            config_refresher(std::move(rhs.config_refresher)),
            callbacks(std::move(rhs.callbacks)),
            timer(std::move(rhs.timer))
        {
        }
        ctWmiPerformance& operator=(ctWmiPerformance&& rhs) NOEXCEPT
        {
            wmi_service = std::move(rhs.wmi_service);
            refresher = std::move(rhs.refresher);
            config_refresher = std::move(rhs.config_refresher);
            callbacks = std::move(rhs.callbacks);
            timer = std::move(rhs.timer);
            return *this;
        }

    private:
        ctComInitialize com_init;
        ctWmiService wmi_service;
        ctComPtr<IWbemRefresher> refresher;
        ctComPtr<IWbemConfigureRefresher> config_refresher;
        // for each interval, callback each of the registered aggregators
        std::vector<details::ctWmiPerformanceCallback> callbacks;
        // timer to fire to indicate when to Refresh the data
        // declare last to guarantee will be destroyed first
        std::unique_ptr<ctThreadpoolTimer> timer;

        static void TimerCallback(ctWmiPerformance* this_ptr, unsigned long _interval) NOEXCEPT
        {
            try {
                // must guarantee COM is initialized on this thread
                ctComInitialize com;
                this_ptr->refresher->Refresh(0);

                for (const auto& _callback : this_ptr->callbacks) {
                    _callback(details::CallbackAction::Update);
                }

                this_ptr->timer->schedule_singleton(
                    [this_ptr, _interval] () { TimerCallback(this_ptr, _interval); }, 
                    _interval);
            }
            catch (const std::exception& e) {
                ctAlwaysFatalCondition(L"Failed to schedule the next Performance Counter read [%ws]",
                    ctString::format_exception(e).c_str());
            }
        }
    };


    enum class ctWmiClassType {
        Uninitialized,
        Static, // ctMakeStaticPerfCounter
        Instance // created with ctMakeInstancePerfCounter
    };
    
    enum class ctWmiClassName {
        Uninitialized,
        Process,
        Processor,
        Memory,
        NetworkAdapter,
        NetworkInterface,
        Tcpip_Diagnostics,
        Tcpip_Ipv4,
        Tcpip_Ipv6,
        Tcpip_TCPv4,
        Tcpip_TCPv6,
        Tcpip_UDPv4,
        Tcpip_UDPv6,
        WinsockBSP
    };

    struct ctWmiPerformanceCounterProperties {
        const ctWmiClassType classType = ctWmiClassType::Uninitialized;
        const ctWmiClassName className = ctWmiClassName::Uninitialized;
        const wchar_t* providerName = nullptr;

        const unsigned long ulongFieldNameCount = 0;
        const wchar_t** ulongFieldNames = nullptr;

        const unsigned long ulonglongFieldNameCount = 0;
        const wchar_t** ulonglongFieldNames = nullptr;

        const unsigned long stringFieldNameCount = 0;
        const wchar_t** stringFieldNames = nullptr;

        template <typename T> bool PropertyNameExists(LPCWSTR name) const NOEXCEPT;
    };

    template <> 
    inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<ULONG>(LPCWSTR name) const NOEXCEPT
    {
        for (unsigned counter = 0; counter < this->ulongFieldNameCount; ++counter) {
            if (ctString::iordinal_equals(name, this->ulongFieldNames[counter])) {
                return true;
            }
        }

        return false;
    }
    template <>
    inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<ULONGLONG>(LPCWSTR name) const NOEXCEPT
    {
        for (unsigned counter = 0; counter < this->ulonglongFieldNameCount; ++counter) {
            if (ctString::iordinal_equals(name, this->ulonglongFieldNames[counter])) {
                return true;
            }
        }

        return false;
    }
    template <>
    inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<std::wstring>(LPCWSTR name) const NOEXCEPT
    {
        for (unsigned counter = 0; counter < this->stringFieldNameCount; ++counter) {
            if (ctString::iordinal_equals(name, this->stringFieldNames[counter])) {
                return true;
            }
        }

        return false;
    }
    template <>
    inline bool ctWmiPerformanceCounterProperties::PropertyNameExists<ctComBstr>(LPCWSTR name) const NOEXCEPT
    {
        for (unsigned counter = 0; counter < this->stringFieldNameCount; ++counter) {
            if (ctString::iordinal_equals(name, this->stringFieldNames[counter])) {
                return true;
            }
        }

        return false;
    }

    namespace ctWmiPerformanceDetails {
        const wchar_t* CommonStringPropertyNames[] = {
            L"Caption",
            L"Description",
            L"Name" };

        const wchar_t* Memory_Counter = L"Win32_PerfFormattedData_PerfOS_Memory";
        const wchar_t* Memory_UlongCounterNames[] = {
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
        const wchar_t* Memory_UlonglongCounterNames[] = {
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

        const wchar_t* ProcessorInformation_Counter = L"Win32_PerfFormattedData_Counters_ProcessorInformation";
        const wchar_t* ProcessorInformation_UlongCounterNames[] = {
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
        const wchar_t* ProcessorInformation_UlonglongCounterNames[] = {
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

        const wchar_t* PerfProc_Process_Counter = L"Win32_PerfFormattedData_PerfProc_Process";
        const wchar_t* PerfProc_Process_UlongCounterNames[] = {
            L"CreatingProcessID",
            L"HandleCount",
            L"IDProcess",
            L"PageFaultsPerSec",
            L"PoolNonpagedBytes",
            L"PoolPagedBytes",
            L"PriorityBase",
            L"ThreadCount"
        };
        const wchar_t* PerfProc_Process_UlonglongCounterNames[] = {
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

        const wchar_t* Tcpip_NetworkAdapter_Counter = L"Win32_PerfFormattedData_Tcpip_NetworkAdapter";
        const wchar_t* Tcpip_NetworkAdapter_ULongLongCounterNames[] = {
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

        const wchar_t* Tcpip_NetworkInterface_Counter = L"Win32_PerfFormattedData_Tcpip_NetworkInterface";
        const wchar_t* Tcpip_NetworkInterface_ULongLongCounterNames[] = {
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

        const wchar_t* Tcpip_IPv4_Counter = L"Win32_PerfFormattedData_Tcpip_IPv4";
        const wchar_t* Tcpip_IPv6_Counter = L"Win32_PerfFormattedData_Tcpip_IPv6";
        const wchar_t* Tcpip_IP_ULongCounterNames[] = {
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

        const wchar_t* Tcpip_TCPv4_Counter = L"Win32_PerfFormattedData_Tcpip_TCPv4";
        const wchar_t* Tcpip_TCPv6_Counter = L"Win32_PerfFormattedData_Tcpip_TCPv6";
        const wchar_t* Tcpip_TCP_ULongCounterNames[] = {
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

        const wchar_t* Tcpip_UDPv4_Counter = L"Win32_PerfFormattedData_Tcpip_UDPv4";
        const wchar_t* Tcpip_UDPv6_Counter = L"Win32_PerfFormattedData_Tcpip_UDPv6";
        const wchar_t* Tcpip_UDP_ULongCounterNames[] = {
            L"DatagramsNoPortPersec",
            L"DatagramsReceivedErrors",
            L"DatagramsReceivedPersec",
            L"DatagramsSentPersec",
            L"DatagramsPersec"
        };

        const wchar_t* TCPIPPerformanceDiagnostics_Counter = L"Win32_PerfFormattedData_TCPIPCounters_TCPIPPerformanceDiagnostics";
        const wchar_t* TCPIPPerformanceDiagnostics_ULongCounterNames[] = {
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

        const wchar_t* MicrosoftWinsockBSP_Counter = L"Win32_PerfFormattedData_AFDCounters_MicrosoftWinsockBSP";
        const wchar_t* MicrosoftWinsockBSP_ULongCounterNames[] = {
            L"DroppedDatagrams",
            L"DroppedDatagramsPersec",
            L"RejectedConnections",
            L"RejectedConnectionsPersec"
        };

        // this patterns (const array of wchar_t* pointers) 
        // allows for compile-time construction of the array of properties
        const ctWmiPerformanceCounterProperties PerformanceCounterPropertiesArray[] = {

        {
            ctWmiClassType::Static,
            ctWmiClassName::Memory,
            Memory_Counter,
            ARRAYSIZE(Memory_UlongCounterNames),
            Memory_UlongCounterNames,
            ARRAYSIZE(Memory_UlonglongCounterNames),
            Memory_UlonglongCounterNames,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Instance,
            ctWmiClassName::Processor,
            ProcessorInformation_Counter,
            ARRAYSIZE(ProcessorInformation_UlongCounterNames),
            ProcessorInformation_UlongCounterNames,
            ARRAYSIZE(ProcessorInformation_UlonglongCounterNames),
            ProcessorInformation_UlonglongCounterNames,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Instance,
            ctWmiClassName::Process,
            PerfProc_Process_Counter,
            ARRAYSIZE(PerfProc_Process_UlongCounterNames),
            PerfProc_Process_UlongCounterNames,
            ARRAYSIZE(PerfProc_Process_UlonglongCounterNames),
            PerfProc_Process_UlonglongCounterNames,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Instance,
            ctWmiClassName::NetworkAdapter,
            Tcpip_NetworkAdapter_Counter,
            0,
            nullptr,
            ARRAYSIZE(Tcpip_NetworkAdapter_ULongLongCounterNames),
            Tcpip_NetworkAdapter_ULongLongCounterNames,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Instance,
            ctWmiClassName::NetworkInterface,
            Tcpip_NetworkInterface_Counter,
            0,
            nullptr,
            ARRAYSIZE(Tcpip_NetworkInterface_ULongLongCounterNames),
            Tcpip_NetworkInterface_ULongLongCounterNames,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Static,
            ctWmiClassName::Tcpip_Ipv4,
            Tcpip_IPv4_Counter,
            ARRAYSIZE(Tcpip_IP_ULongCounterNames),
            Tcpip_IP_ULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Static,
            ctWmiClassName::Tcpip_Ipv6,
            Tcpip_IPv6_Counter,
            ARRAYSIZE(Tcpip_IP_ULongCounterNames),
            Tcpip_IP_ULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Static,
            ctWmiClassName::Tcpip_TCPv4,
            Tcpip_TCPv4_Counter,
            ARRAYSIZE(Tcpip_TCP_ULongCounterNames),
            Tcpip_TCP_ULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Static,
            ctWmiClassName::Tcpip_TCPv6,
            Tcpip_TCPv6_Counter,
            ARRAYSIZE(Tcpip_TCP_ULongCounterNames),
            Tcpip_TCP_ULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Static,
            ctWmiClassName::Tcpip_UDPv4,
            Tcpip_UDPv4_Counter,
            ARRAYSIZE(Tcpip_UDP_ULongCounterNames),
            Tcpip_UDP_ULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Static,
            ctWmiClassName::Tcpip_UDPv6,
            Tcpip_UDPv6_Counter,
            ARRAYSIZE(Tcpip_UDP_ULongCounterNames),
            Tcpip_UDP_ULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Static,
            ctWmiClassName::Tcpip_Diagnostics,
            TCPIPPerformanceDiagnostics_Counter,
            ARRAYSIZE(TCPIPPerformanceDiagnostics_ULongCounterNames),
            TCPIPPerformanceDiagnostics_ULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        },

        {
            ctWmiClassType::Static,
            ctWmiClassName::WinsockBSP,
            MicrosoftWinsockBSP_Counter,
            ARRAYSIZE(MicrosoftWinsockBSP_ULongCounterNames),
            MicrosoftWinsockBSP_ULongCounterNames,
            0,
            nullptr,
            ARRAYSIZE(CommonStringPropertyNames),
            CommonStringPropertyNames
        }

        };
    }

    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeStaticPerfCounter(LPCWSTR _class_name, LPCWSTR _counter_name, const ctWmiPerformanceCollectionType _collection_type = ctWmiPerformanceCollectionType::Detailed)
    {
        // 'static' WMI PerfCounters enumerate via IWbemClassObject and accessed/refreshed via IWbemClassObject
        return std::make_shared<ctWmiPerformanceCounterImpl<IWbemClassObject, IWbemClassObject, T>>(_class_name, _counter_name, _collection_type);
    }

    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeInstancePerfCounter(LPCWSTR _class_name, LPCWSTR _counter_name, const ctWmiPerformanceCollectionType _collection_type = ctWmiPerformanceCollectionType::Detailed)
    {
        // 'instance' WMI perf objects are enumerated through the IWbemHiPerfEnum interface and accessed/refreshed through the IWbemObjectAccess interface
        return std::make_shared<ctWmiPerformanceCounterImpl<IWbemHiPerfEnum, IWbemObjectAccess, T>>(_class_name, _counter_name, _collection_type);
    }

    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctCreatePerfCounter(const ctWmiClassName& _class, LPCWSTR _counter_name, const ctWmiPerformanceCollectionType _collection_type = ctWmiPerformanceCollectionType::Detailed)
    {
        const ctWmiPerformanceCounterProperties* foundProperty = nullptr;
        for (const auto& counterProperty : ctWmiPerformanceDetails::PerformanceCounterPropertiesArray) {
            if (_class == counterProperty.className) {
                foundProperty = &counterProperty;
                break;
            }
        }

        if (!foundProperty) {
            throw std::invalid_argument("Unknown WMI Performance Counter Class");
        }

        if (!foundProperty->PropertyNameExists<T>(_counter_name)) {
            throw ctException(
                ERROR_INVALID_DATA,
                ctString::format_string(
                    L"CounterName (%ws) does not exist in the requested class (%u)",
                    _counter_name, static_cast<unsigned>(_class)).c_str(),
                L"ctCreatePerfCounter",
                true);
        }

        if (foundProperty->classType == ctWmiClassType::Static) {
            return ctMakeStaticPerfCounter<T>(foundProperty->providerName, _counter_name, _collection_type);
        }
        ctFatalCondition(
            foundProperty->classType != ctWmiClassType::Instance,
            L"The ctWmiClassType is invalid");
        return ctMakeInstancePerfCounter<T>(foundProperty->providerName, _counter_name, _collection_type);
    }
} // ctl namespace
