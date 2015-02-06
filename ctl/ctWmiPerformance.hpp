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
#include <vector>
#include <string>
#include <tuple>
#include <map>
#include <algorithm>

#include <Windows.h>

#include <ctThreadTimer.hpp>
#include <ctException.hpp>
#include <ctWmiInitialize.hpp>
#include <ctScopeGuard.hpp>
#include <cttime.hpp>
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
/// ctWmiPerformanceCounter is exposed to the user through factory functions defined per-counter class (ctShared<class>PerfCounter) e.g. ctSharedNetworkInterfacePerfCounter
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
/// - calculate_size(): returns to the caller the number of data-points matching that instance name (instance name is always the string matching the 'Name' property)
/// - clear_data(): clears all data points captured but still maintains all filters previously added
/// - get_counterName(): returns the name of the property of the WMI class that is being recorded
/// - get_filter(): takes a counter name parameter and returns the variant value by which that name is being filtered
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
/// - exposes match() taking both types of Access objects to return if it matches the instance it contains
/// - exposes match() taking a string to check if it matches the instance it contains
/// - exposes add() taking both types of Access objects + a ULONGLONG time parameter to retrieve the data and add it to the internal map
/// 
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace ctl {
    namespace {
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Function to return the performance data of the specified property from the input instance
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        inline
        ctComVariant ctReadIWbemObjectAccess(_In_ IWbemObjectAccess* _instance, _In_ LPCWSTR _counter_name)
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
                    long value_size = static_cast<long>(value.size() * sizeof(WCHAR));
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
                            L"ctWmiPerformance only supports data of type INT32, INT64, and BSTR: counter %s is of type %u",
                            _counter_name, property_type).c_str(),
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

            ctWmiPerformanceDataAccessor(_In_ ctComPtr<IWbemConfigureRefresher> _config, _In_ LPCWSTR _classname);

            ~ctWmiPerformanceDataAccessor() throw()
            {
                this->clear();
            }

            ///
            /// refreshes internal data with the latest performance data
            ///
            void refresh();

            access_iterator begin() const throw()
            {
                return this->accessor_objects.begin();
            }
            access_iterator end() const throw()
            {
                return this->accessor_objects.end();
            }

        private:
            // non-copyable
            ctWmiPerformanceDataAccessor(const ctWmiPerformanceDataAccessor&);
            ctWmiPerformanceDataAccessor& operator=(const ctWmiPerformanceDataAccessor&);

            // members
            ctComPtr<TEnum> enumeration_object;
            // TAccess pointers are returned through enumeration_object::GetObjects, reusing the same vector for each refresh call
            std::vector<TAccess*> accessor_objects;
            access_iterator current_iterator;

            void clear() throw();
        };

        inline
        ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::ctWmiPerformanceDataAccessor(_In_ ctComPtr<IWbemConfigureRefresher> _config, _In_ LPCWSTR _classname)
        : enumeration_object(),
          accessor_objects(),
          current_iterator(accessor_objects.end())
        {
            // must load COM and WMI locally, though both are still required globally
            ctComInitialize com;
            ctWmiService wmi(L"root\\cimv2");

            LONG lid;
            HRESULT hr = _config->AddEnum(wmi.get(), _classname, 0, NULL, enumeration_object.get_addr_of(), &lid);
            if (FAILED(hr)) {
                throw ctException(
                    hr,
                    L"IWbemConfigureRefresher::AddEnum",
                    L"ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::ctWmiPerformanceDataAccessor",
                    false);
            }
        }

        inline
        ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::ctWmiPerformanceDataAccessor(_In_ ctComPtr<IWbemConfigureRefresher> _config, _In_ LPCWSTR _classname)
        : enumeration_object(),
          accessor_objects(),
          current_iterator(accessor_objects.end())
        {
            // must load COM and WMI locally, though both are still required globally
            ctComInitialize com;
            ctWmiService wmi(L"root\\cimv2");

            ctWmiEnumerate enum_instances(wmi);
            enum_instances.query(ctString::format_string(L"SELECT * FROM %s", _classname).c_str());
            if (enum_instances.begin() == enum_instances.end()) {
                throw ctException(
                    ERROR_NOT_FOUND,
                    ctString::format_string(
                        L"Failed to refresh a static instances of the WMI class %s",
                        _classname).c_str(),
                    L"ctWmiPerformanceDataAccessor",
                    true);
            }

            auto instance = *enum_instances.begin();
            LONG lid;
            HRESULT hr = _config->AddObjectByTemplate(
                wmi.get(),
                instance.get_instance().get(),
                0,
                NULL,
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
            this->clear();

            ULONG objects_returned = 0;
            HRESULT hr = this->enumeration_object->GetObjects(
                0,
                static_cast<ULONG>(this->accessor_objects.size()),
                (0 == this->accessor_objects.size()) ? nullptr : &this->accessor_objects[0],
                &objects_returned);

            if (WBEM_E_BUFFER_TOO_SMALL == hr) {
                this->accessor_objects.resize(objects_returned);
                hr = this->enumeration_object->GetObjects(
                    0,
                    static_cast<ULONG>(this->accessor_objects.size()),
                    &this->accessor_objects[0],
                    &objects_returned);
            }
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemObjectAccess::GetObjects", L"ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::refresh", false);
            }

            this->accessor_objects.resize(objects_returned);
            this->current_iterator = this->accessor_objects.begin();
        }

        template <>
        inline void ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::refresh()
        {
            // the underlying IWbemClassObject is already refreshed
            // accessor_objects will only ever have a singe instance
            ctFatalCondition(
                this->accessor_objects.size() != 1,
                L"ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>: for IWbemClassObject performance classes there can only ever have the single instance being tracked - instead has %Iu",
                this->accessor_objects.size());

            this->current_iterator = this->accessor_objects.begin();
        }

        template <>
        inline void ctWmiPerformanceDataAccessor<IWbemHiPerfEnum, IWbemObjectAccess>::clear() throw()
        {
            for (IWbemObjectAccess* _object : this->accessor_objects) {
                _object->Release();
            }
            this->accessor_objects.clear();
            this->current_iterator = this->accessor_objects.end();
        }

        template <>
        inline void ctWmiPerformanceDataAccessor<IWbemClassObject, IWbemClassObject>::clear() throw()
        {
            this->current_iterator = this->accessor_objects.end();
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
            CRITICAL_SECTION guard_data;
            std::wstring instance_name;
            std::wstring counter_name;
            std::map<ULONGLONG, T> counter_data;

        public:
            ctWmiPeformanceCounterData(_In_ IWbemObjectAccess* _instance, _In_ LPCWSTR _counter)
            : guard_data(),
              instance_name(ctReadIWbemObjectAccess(_instance, L"Name")->bstrVal),
              counter_name(_counter),
              counter_data()
            {
                if (!::InitializeCriticalSectionEx(&guard_data, 4000, 0)) {
                    auto gle = ::GetLastError();
                    ctAlwaysFatalCondition(
                        ctString::format_string(
                            L"InitializeCriticalSectionEx failed with error %ul",
                            gle).c_str());
                }
            }
            ctWmiPeformanceCounterData(_In_ IWbemClassObject* _instance, _In_ LPCWSTR _counter)
            : guard_data(),
              instance_name(),
              counter_name(_counter),
              counter_data()
            {
                if (!::InitializeCriticalSectionEx(&guard_data, 4000, 0)) {
                    auto gle = ::GetLastError();
                    ctAlwaysFatalCondition(
                        ctString::format_string(
                            L"InitializeCriticalSectionEx failed with error %ul",
                            gle).c_str());
                }

                ctComVariant value;
                HRESULT hr = _instance->Get(L"Name", 0, value.get(), nullptr, nullptr);
                if (FAILED(hr)) {
                    throw ctException(hr, L"IWbemClassObject::Get(Name)", L"ctWmiPerformanceCounterData", false);
                }
                // Name is expected to be NULL in this case
                // - since IWbemClassObject is expected to be a single instance
                if (!value.is_null()) {
                    throw ctException(
                        ERROR_INVALID_DATA,
                        ctString::format_string(
                            L"ctWmiPeformanceCounterData was given an IWbemClassObject to track that had a non-null 'Name' key field ['%s']. Expected to be a NULL key field as to only support single-instances",
                            value->bstrVal).c_str(),
                        L"ctWmiPeformanceCounterData",
                        true);
                }
            }
            ~ctWmiPeformanceCounterData() throw()
            {
                ::DeleteCriticalSection(&guard_data);
            }

            /// _instance_name == nullptr means match everything
            /// - allows for the caller to not have to pass Name filters multiple times
            bool match(_In_opt_ LPCWSTR _instance_name) throw()
            {
                if (nullptr == _instance_name) {
                    return true;

                } else if (this->instance_name.empty()) {
                    return (nullptr == _instance_name);

                } else {
                    return ctString::iordinal_equals(
                        this->instance_name,
                        _instance_name);
                }
            }
            bool match(_In_ IWbemObjectAccess* _instance)
            {
                return ctString::iordinal_equals(
                    this->instance_name,
                    ctReadIWbemObjectAccess(_instance, L"Name")->bstrVal);
            }
            bool match(_In_ IWbemClassObject* _instance)
            {
                ctComVariant value;
                HRESULT hr = _instance->Get(L"Name", 0, value.get(), nullptr, nullptr);
                if (FAILED(hr)) {
                    throw ctException(hr, L"IWbemClassObject::Get(Name)", L"ctWmiPerformanceCounterData::match", false);
                }
                return this->match(value->bstrVal);
            }

            void add(_In_ IWbemObjectAccess* _instance, _In_ ULONGLONG _time)
            {
                T instance_data;
                ctReadIWbemObjectAccess(_instance, counter_name.c_str()).retrieve(&instance_data);
                ctAutoReleaseCriticalSection auto_guard(&this->guard_data);
                this->counter_data[_time] = instance_data;
            }
            void add(_In_ IWbemClassObject* _instance, _In_ ULONGLONG _time)
            {
                ctComVariant value;
                HRESULT hr = _instance->Get(this->counter_name.c_str(), 0, value.get(), nullptr, nullptr);
                if (FAILED(hr)) {
                    throw ctException(
                        hr,
                        ctString::format_string(
                            L"IWbemClassObject::Get(%s)",
                            this->counter_name.c_str()).c_str(),
                        L"ctWmiPeformanceCounterData<T>::add",
                        true);
                }

                ctAutoReleaseCriticalSection auto_guard(&this->guard_data);
                T instance_data;
                this->counter_data[_time] = value.retrieve(&instance_data);
            }

            typename std::map<ULONGLONG, T>::iterator begin()
            {
                ctAutoReleaseCriticalSection auto_guard(&this->guard_data);
                return this->counter_data.begin();
            }
            typename std::map<ULONGLONG, T>::iterator end()
            {
                ctAutoReleaseCriticalSection auto_guard(&this->guard_data);
                return this->counter_data.end();
            }

            size_t count()
            {
                ctAutoReleaseCriticalSection auto_guard(&this->guard_data);
                return this->counter_data.size();
            }

            void clear()
            {
                ctAutoReleaseCriticalSection auto_guard(&this->guard_data);
                this->counter_data.clear();
            }

        private:
            // no default c'tor
            // blocking copying
            ctWmiPeformanceCounterData();
            ctWmiPeformanceCounterData(const ctWmiPeformanceCounterData&);
            ctWmiPeformanceCounterData& operator= (const ctWmiPeformanceCounterData&);
        };

        ///
        /// WMI passes around 64-bit integers as BSTR's, so must specialize reading those values to do the proper conversion
        ///
        template <>
        inline void ctWmiPeformanceCounterData<ULONGLONG>::add(_In_ IWbemClassObject* _instance, _In_ ULONGLONG _time)
        {
            ctComVariant value;
            HRESULT hr = _instance->Get(this->counter_name.c_str(), 0, value.get(), nullptr, nullptr);
            if (FAILED(hr)) {
                throw ctException(
                    hr,
                    ctString::format_string(
                        L"IWbemClassObject::Get(%s)",
                        this->counter_name.c_str()).c_str(),
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

            ctAutoReleaseCriticalSection auto_guard(&this->guard_data);
            this->counter_data[_time] = ::_wcstoui64(value->bstrVal, NULL, 10);
        }

        template <>
        inline void ctWmiPeformanceCounterData<LONGLONG>::add(_In_ IWbemClassObject* _instance, _In_ ULONGLONG _time)
        {
            ctComVariant value;
            HRESULT hr = _instance->Get(this->counter_name.c_str(), 0, value.get(), nullptr, nullptr);
            if (FAILED(hr)) {
                throw ctException(
                    hr,
                    ctString::format_string(
                        L"IWbemClassObject::Get(%s)",
                        this->counter_name.c_str()).c_str(),
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

            ctAutoReleaseCriticalSection auto_guard(&this->guard_data);
            this->counter_data[_time] = ::_wcstoi64(value->bstrVal, NULL, 10);
        }


        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// type for the callback implemented in all ctWmiPerformanceCounter classes
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        typedef std::function<void(const bool _update_data, const ULONGLONG _time)> ctWmiPerformanceCallback;

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
            typename std::map<ULONGLONG, T>::iterator current;
            bool is_empty;

        public:
            iterator(typename std::map<ULONGLONG, T>::iterator _instance)
            : current(_instance),
              is_empty(false)
            {
            }
            iterator() : current(), is_empty(true)
            {
            }

            ~iterator() throw()
            {
            }

            ////////////////////////////////////////////////////////////////////////////////
            ///
            /// copy c'tor and copy assignment
            /// move c'tor and move assignment
            ///
            ////////////////////////////////////////////////////////////////////////////////
            iterator(_In_ iterator&& _i) throw()
            : current(std::move(_i.current)),
              is_empty(std::move(_i.is_empty))
            {
            }
            iterator& operator =(_In_ iterator&& _i) throw()
            {
                this->current = std::move(_i.current);
                this->is_empty = std::move(_i.is_empty);
                return *this;
            }

            T& operator* ()
            {
                if (this->is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : dereferencing an iterator referencing an empty container");
                }
                return this->current->second;
            }

            T* operator-> ()
            {
                if (this->is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : dereferencing an iterator referencing an empty container");
                }
                &(this->current->second);
            }

            bool operator==(_In_ const iterator& _iter) const throw()
            {
                if (this->is_empty || _iter.is_empty) {
                    return (this->is_empty == _iter.is_empty);
                } else {
                    return (this->current == _iter.current);
                }
            }
            bool operator!=(_In_ const iterator& _iter) const throw()
            {
                return !(*this == _iter);
            }

            // preincrement
            iterator& operator++()
            {
                if (this->is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : preincrementing an iterator referencing an empty container");
                }
                ++this->current;
                return *this;
            }
            // postincrement
            iterator operator++(_In_ int)
            {
                if (this->is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : postincrementing an iterator referencing an empty container");
                }
                iterator temp(*this);
                ++this->current;
                return temp;
            }
            // increment by integer
            iterator& operator+=(_In_ size_t _inc)
            {
                if (this->is_empty) {
                    throw std::runtime_error("ctWmiPerformanceCounter::iterator : postincrementing an iterator referencing an empty container");
                }
                for (size_t loop = 0; loop < _inc; ++loop) {
                    ++this->current;
                }
                return *this;
            }
        };

    public:
        ctWmiPerformanceCounter(_In_ LPCWSTR _counter_name)
        : counter_guard(),
          filter_guard(),
          counter_name(_counter_name),
          refresher(),
          configure_refresher(),
          counter_data()
        {
            refresher = ctComPtr<IWbemRefresher>::createInstance(CLSID_WbemRefresher, IID_IWbemRefresher);
            HRESULT hr = refresher->QueryInterface(IID_IWbemConfigureRefresher, reinterpret_cast<void**>(configure_refresher.get_addr_of()));
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemRefresher::QueryInterface", L"ctWmiPerformanceCounter", false);
            }

            if (!::InitializeCriticalSectionEx(&counter_guard, 4000, 0)) {
                auto gle = ::GetLastError();
                ctAlwaysFatalCondition(
                    ctString::format_string(
                        L"InitializeCriticalSectionEx failed with error %ul",
                        gle).c_str());
            }
            if (!::InitializeCriticalSectionEx(&filter_guard, 4000, 0)) {
                auto gle = ::GetLastError();
                ctAlwaysFatalCondition(
                    ctString::format_string(
                        L"InitializeCriticalSectionEx failed with error %ul",
                        gle).c_str());
            }
        }

        virtual ~ctWmiPerformanceCounter() throw()
        {
            ::DeleteCriticalSection(&counter_guard);
            ::DeleteCriticalSection(&filter_guard);
        }

        const std::wstring& get_counterName() const
        {
            return this->counter_name;
        }

        const ctComVariant& get_filter(_In_ LPCWSTR _counter_name) const
        {
            ctAutoReleaseCriticalSection guard(&this->filter_guard);
            auto found_filter = std::find_if(
                std::begin(this->instance_filter),
                std::end(this->instance_filter),
                [_counter_name] (const ctWmiPerformanceInstanceFilter& _filter) -> bool {
                return ctString::iordinal_equals(_counter_name, _filter.counter_name);
            });
            if (std::end(this->instance_filter) == found_filter) {
                throw ctException(
                    ERROR_NOT_FOUND,
                    ctString::format_string(
                        L"Filter with the counter_name %s was not found",
                        _counter_name).c_str(),
                    L"ctWmiPerformanceCounter::get_filter",
                    true);
            }
            return found_filter->property_value;
        }

        ///
        /// *not* thread-safe: caller must guarantee sequential access to add_filter()
        ///
        template <typename T>
        void add_filter(_In_ LPCWSTR _counter_name, _In_ T _property_value)
        {
            ctAutoReleaseCriticalSection guard(&this->filter_guard);
            this->instance_filter.emplace_back(_counter_name, ctWmiMakeVariant<T>(_property_value));
        }

        ///
        /// returns a begin/end pair of interator that exposes data for each time-slice
        /// - static classes will have a null instance name
        ///
        std::pair<iterator, iterator> reference_range(_In_ LPCWSTR _instance_name = nullptr)
        {
            ctAutoReleaseCriticalSection guard(&this->counter_guard);
            auto found_instance = std::find_if(
                this->counter_data.begin(),
                this->counter_data.end(),
                [&] (const std::unique_ptr<ctWmiPeformanceCounterData<T>>& _instance) {
                return _instance->match(_instance_name);
            });
            if (this->counter_data.end() == found_instance) {
                // nothing matching that instance name
                // return the end iterator (default c'tor == end)
                return std::pair<iterator, iterator>(iterator(), iterator());
            }

            const std::unique_ptr<ctWmiPeformanceCounterData<T>>& instance_reference = *found_instance;
            return std::pair<iterator, iterator>(instance_reference->begin(), instance_reference->end());
        }

        ///
        /// returns only the number of counters matching that instance name
        /// - which could be smaller than the total instances captured across that time-period
        ///
        size_t calculate_size(_In_ LPCWSTR _instance_name = nullptr)
        {
            ctAutoReleaseCriticalSection guard(&this->counter_guard);
            auto found = std::find_if(
                this->counter_data.rbegin(),
                this->counter_data.rend(),
                [_instance_name] (const std::unique_ptr<ctWmiPeformanceCounterData<T>>& _instance) {
                return _instance->match(_instance_name);
            });
            if (this->counter_data.rend() == found) {
                return 0;
            } else {
                const std::unique_ptr<ctWmiPeformanceCounterData<T>>& found_data = *found;
                return found_data->count();
            }
        }

        ///
        /// clears all counter data, but leaves all filters in-place
        ///
        void clear_data()
        {
            ctAutoReleaseCriticalSection guard(&this->counter_guard);
            for (auto& _counter_data : this->counter_data) {
                _counter_data->clear();
            }
        }

    private:
        //
        // private stucture to track the 'filter' which instances to track
        //
        struct ctWmiPerformanceInstanceFilter {
            std::wstring counter_name;
            ctComVariant property_value;

            ctWmiPerformanceInstanceFilter(_In_ LPCWSTR _counter_name, _In_ const ctComVariant& _property_value)
            : counter_name(_counter_name),
              property_value(_property_value)
            {
            }
            ~ctWmiPerformanceInstanceFilter() throw()
            {
            }

            bool operator==(_In_ IWbemObjectAccess* _instance)
            {
                return (this->property_value == ctReadIWbemObjectAccess(_instance, counter_name.c_str()));
            }
            bool operator!=(_In_ IWbemObjectAccess* _instance)
            {
                return !(*this == _instance);
            }

            bool operator==(_In_ IWbemClassObject* _instance)
            {
                ctComVariant value;
                HRESULT hr = _instance->Get(counter_name.c_str(), 0, value.get(), nullptr, nullptr);
                if (FAILED(hr)) {
                    throw ctException(hr, L"IWbemClassObject::Get(counter_name)", L"ctWmiPerformanceCounterData", false);
                }
                ctFatalCondition(
                    value->vt != property_value->vt,
                    L"VARIANT types do not match to make a comparison");
                return (this->property_value == value);
            }
            bool operator!=(_In_ IWbemClassObject* _instance)
            {
                return !(*this == _instance);
            }
        };

    private:
        // non-copyable
        ctWmiPerformanceCounter(const ctWmiPerformanceCounter&);
        ctWmiPerformanceCounter& operator=(const ctWmiPerformanceCounter&);
        // CS's must be mutable to allow internal locks to be taken in const methods
        mutable CRITICAL_SECTION counter_guard;
        mutable CRITICAL_SECTION filter_guard;

        std::wstring counter_name;
        ctComPtr<IWbemRefresher> refresher;
        ctComPtr<IWbemConfigureRefresher> configure_refresher;
        std::vector<ctWmiPerformanceInstanceFilter> instance_filter;
        std::vector<std::unique_ptr<ctWmiPeformanceCounterData<T>>> counter_data;

    protected:

        virtual void update_counter_data(ULONGLONG _time) = 0;

        // ctWmiPerformance needs private access to invoke register_callback in the derived type
        friend class ctWmiPerformance;
        ctWmiPerformanceCallback register_callback()
        {
            return [this] (const bool _update_data, const ULONGLONG _time) {
                ctAutoReleaseCriticalSection guard(&this->counter_guard);
                if (_update_data) {
                    // only the derived class has appropriate the accessor class to update the data
                    this->update_counter_data(_time);
                } else {
                    // else clear the data
                    for (auto& _counter_data : this->counter_data) {
                        _counter_data->clear();
                    }
                }
            };
        }

        //
        // the derived classes need to use the same refresher object
        //
        ctComPtr<IWbemConfigureRefresher> access_refresher()
        {
            return this->configure_refresher;
        }

        //
        // this function is how one looks to see if the data machines requires knowing how to access the data from that specific WMI class
        // - it's also how to access the data is captured with the TAccess template type
        // - ctWmiPerformanceCounterData overrides match() for each possible TAccess* types
        //
        template <typename TAccess>
        void add_instance(_In_ TAccess* _instance, ULONGLONG _time)
        {
            bool fAddData = false;
            // scope for the instance gaurd
            {
                ctAutoReleaseCriticalSection guard(&this->filter_guard);
                fAddData = this->instance_filter.empty();
                if (!fAddData) {
                    fAddData = (std::end(this->instance_filter) != std::find(
                        std::begin(this->instance_filter),
                        std::end(this->instance_filter),
                        _instance));
                }
            }
            // add the counter data for this instance if:
            // - have no filters [not filtering instances at all]
            // - matches at least one filter
            if (fAddData) {
                ctAutoReleaseCriticalSection guard(&this->counter_guard);
                auto tracked_instance = std::find_if(
                    std::begin(this->counter_data),
                    std::end(this->counter_data),
                    [&] (std::unique_ptr<ctWmiPeformanceCounterData<T>>& _counter_data) {
                    return _counter_data->match(_instance);
                });

                // if this instance of this counter is new [new unique instance for this counter]
                // - we must add a new ctWmiPeformanceCounterData to the parent's counter_data vector
                // else
                // - we just add this counter value to the already-tracked instance
                if (tracked_instance == std::end(this->counter_data)) {
                    this->counter_data.push_back(
                        std::unique_ptr<ctWmiPeformanceCounterData<T>>
                            (new ctWmiPeformanceCounterData<T>(_instance, this->counter_name.c_str())));
                    (*this->counter_data.rbegin())->add(_instance, _time);
                } else {
                    (*tracked_instance)->add(_instance, _time);
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
        ctWmiPerformanceCounterImpl(_In_ LPCWSTR _class_name, _In_ LPCWSTR _counter_name)
        : ctWmiPerformanceCounter<TData>(_counter_name),
          accessor(this->access_refresher(), _class_name)
        {
        }

        ~ctWmiPerformanceCounterImpl() throw()
        {
        }

    private:
        ///
        /// no default c'tor
        /// non-copyable
        ///
        ctWmiPerformanceCounterImpl();
        ctWmiPerformanceCounterImpl(const ctWmiPerformanceCounterImpl&);
        ctWmiPerformanceCounterImpl& operator=(const ctWmiPerformanceCounterImpl&);

        ///
        /// this concrete template class serves to capture the Enum and Access template types
        /// - so can instantiate the appropriate accessor object
        ctWmiPerformanceDataAccessor<TEnum, TAccess> accessor;

        ///
        /// invoked from the parent class to add data matching any/all filters
        ///
        /// private function required to be implemented from the abstract base class
        /// - concrete classe must pass back a function callback for addeding data points for the specified counter
        ///
        void update_counter_data(ULONGLONG _time)
        {
            // refresh this hi-perf object to get the current values
            // requires the invoker serializes all calls
            this->accessor.refresh();

            // the accessor object exposes begin() / end() to allow access to instances
            // - of the specified hi-performance counter
            for (auto& _instance : this->accessor) {
                this->add_instance(_instance, _time);
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
        ctWmiPerformance()
        : com_init(),
          wmi_service(L"root\\cimv2"),
          refresher(),
          timer(),
          starttime(),
          timeslots(),
          callbacks()
        {
            refresher = ctComPtr<IWbemRefresher>::createInstance(CLSID_WbemRefresher, IID_IWbemRefresher);
            HRESULT hr = refresher->QueryInterface(
                IID_IWbemConfigureRefresher,
                reinterpret_cast<void**>(this->config_refresher.get_addr_of()));
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemRefresher::QueryInterface(IID_IWbemConfigureRefresher)", L"ctWmiPerformance", false);
            }
        }

        virtual ~ctWmiPerformance() throw()
        {
            // must stop all callbacks before tearing everything down
            timer.stop();
        }

        template <typename T>
        void add_counter(std::shared_ptr<ctWmiPerformanceCounter<T>> _wmi_perf)
        {
            this->callbacks.push_back(_wmi_perf->register_callback());
            ctlScopeGuard(revert_callback, { this->callbacks.pop_back(); });

            HRESULT hr = this->config_refresher->AddRefresher(_wmi_perf->refresher.get(), 0, nullptr);
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemConfigureRefresher::AddRefresher", L"ctWmiPerformance<T>::add", false);
            }

            // dismiss scope-guard - successfully added refresher
            revert_callback.dismiss();
        }

        void start_all_counters(unsigned _interval)
        {
            std::function<void(void*)> callback = [this] (void*) {
                try {
                    // must guarantee COM is initialized on this thread
                    ctComInitialize com;
                    this->refresher->Refresh(0);
                    this->starttime.setCurrentSystemTime();
                    ULONGLONG now = this->starttime.getMilliSeconds();

                    this->timeslots.push_back(now);
                    for (auto& _callback : this->callbacks) {
                        _callback(true, now);
                    }
                }
                catch (const std::exception& e) {
                    ctAlwaysFatalCondition(L"%s", ctString::format_exception(e).c_str());
                }
            };

            // non-reentrant: so if overlapping times occur, will ignore the latter perf data
            this->timer.stop();
            this->timer.start_nonreentrant(
                callback,
                static_cast<void*>(nullptr),
                _interval);
        }

        // no-throw / no-fail
        void stop_all_counters() throw()
        {
            this->timer.stop();
        }

        // no-throw / no-fail
        void clear_counter_data() throw()
        {
            this->timeslots.clear();
            for (auto& _callback : this->callbacks) {
                _callback(false, 0ULL);
            }
        }

        void reset_counters()
        {
            this->timeslots.clear();
            this->callbacks.clear();

            // release this Refresher and ConfigRefresher, so future counters will be added cleanly
            this->refresher = ctComPtr<IWbemRefresher>::createInstance(CLSID_WbemRefresher, IID_IWbemRefresher);
            HRESULT hr = refresher->QueryInterface(
                IID_IWbemConfigureRefresher,
                reinterpret_cast<void**>(this->config_refresher.get_addr_of()));
            if (FAILED(hr)) {
                throw ctException(hr, L"IWbemRefresher::QueryInterface(IID_IWbemConfigureRefresher)", L"ctWmiPerformance", false);
            }
        }

    private:
        ctWmiPerformance(const ctWmiPerformance&);
        ctWmiPerformance& operator=(const ctWmiPerformance&);

        ctComInitialize com_init;
        ctWmiService wmi_service;
        ctComPtr<IWbemRefresher> refresher;
        ctComPtr<IWbemConfigureRefresher> config_refresher;
        // timer to fire to indicate when to Refresh the data
        ctThreadTimer timer;
        // track when the time started
        ctTime starttime;
        // track the timeslots for each interval
        std::vector<ULONGLONG> timeslots;
        // for each interval, callback each of the registered aggregators
        std::vector<ctWmiPerformanceCallback> callbacks;
    };


    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeStaticPerfCounter(_In_ LPCWSTR _class_name, _In_ LPCWSTR _counter_name)
    {
        // 'static' WMI PerfCounters enumerate via IWbemClassObject and accessed/refreshed via IWbemClassObject
        return std::make_shared<ctWmiPerformanceCounterImpl<IWbemClassObject, IWbemClassObject, T>>(_class_name, _counter_name);
    }

    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctMakeInstancePerfCounter(_In_ LPCWSTR _class_name, _In_ LPCWSTR _counter_name)
    {
        // 'instance' WMI perf objects are enumerated through the IWbemHiPerfEnum interface and accessed/refreshed through the IWbemObjectAccess interface
        return std::make_shared<ctWmiPerformanceCounterImpl<IWbemHiPerfEnum, IWbemObjectAccess, T>>(_class_name, _counter_name);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Factory for a Win32_PerfFormattedData_PerfProc_Process ctWmiPerformanceCounter instance
    /// - specialized only for ULONGLONG, ULONG, and string
    /// - each matching their respective counter
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctSharedProcessPerfCounter(_In_ LPCWSTR _counter_name);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Factory for a Win32_PerfFormattedData_Counters_ProcessorInformation ctWmiPerformanceCounter instance
    ///   *Not* using the older Win32_PerfFormattedData_PerfOS_Processor which is now considered less accurate
    /// - specialized only for ULONGLONG, ULONG, and string
    /// - each matching their respective counter
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctSharedProcessorPerfCounter(_In_ LPCWSTR _counter_name);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Factory for a Win32_PerfFormattedData_PerfOS_Memory ctWmiPerformanceCounter instance
    /// - specialized only for ULONGLONG, ULONG, and string
    /// - each matching their respective counter
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctSharedMemoryPerfCounter(_In_ LPCWSTR _counter_name);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Factory for a Win32_PerfFormattedData_Tcpip_NetworkInterface ctWmiPerformanceCounter instance
    /// - specialized only for ULONGLONG, ULONG, and string
    /// - each matching their respective counter
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    std::shared_ptr<ctWmiPerformanceCounter<T>> ctSharedNetworkInterfacePerfCounter(_In_ LPCWSTR _counter_name);


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctSharedMemoryPerfCounter specializations
    /// - colleecting from the static hi-perf WMI class Win32_PerfFormattedData_PerfOS_Memory
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> ctSharedMemoryPerfCounter<ULONGLONG>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_Memory = L"Win32_PerfFormattedData_PerfOS_Memory";

        if (ctString::iordinal_equals(_counter_name, L"AvailableBytes") ||
            ctString::iordinal_equals(_counter_name, L"AvailableKBytes") ||
            ctString::iordinal_equals(_counter_name, L"AvailableMBytes") ||
            ctString::iordinal_equals(_counter_name, L"CacheBytes") ||
            ctString::iordinal_equals(_counter_name, L"CacheBytesPeak") ||
            ctString::iordinal_equals(_counter_name, L"CommitLimit") ||
            ctString::iordinal_equals(_counter_name, L"CommittedBytes") ||
            ctString::iordinal_equals(_counter_name, L"Frequency_Object") ||
            ctString::iordinal_equals(_counter_name, L"Frequency_PerfTime") ||
            ctString::iordinal_equals(_counter_name, L"Frequency_Sys100NS") ||
            ctString::iordinal_equals(_counter_name, L"PoolNonpagedBytes") ||
            ctString::iordinal_equals(_counter_name, L"PoolPagedBytes") ||
            ctString::iordinal_equals(_counter_name, L"PoolPagedResidentBytes") ||
            ctString::iordinal_equals(_counter_name, L"SystemCacheResidentBytes") ||
            ctString::iordinal_equals(_counter_name, L"SystemCodeResidentBytes") ||
            ctString::iordinal_equals(_counter_name, L"SystemCodeTotalBytes") ||
            ctString::iordinal_equals(_counter_name, L"SystemDriverResidentBytes") ||
            ctString::iordinal_equals(_counter_name, L"SystemDriverTotalBytes") ||
            ctString::iordinal_equals(_counter_name, L"Timestamp_Object") ||
            ctString::iordinal_equals(_counter_name, L"Timestamp_PerfTime") ||
            ctString::iordinal_equals(_counter_name, L"Timestamp_Sys100NS")) {
            // a static perf counter
            return ctMakeStaticPerfCounter<ULONGLONG>(ctWmiPerformanceClass_Memory, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedMemoryPerfCounter counter name %s [from class Win32_PerfFormattedData_PerfOS_Memory] does not have a ULONGLONG counter type",
                _counter_name).c_str(),
            L"ctSharedMemoryPerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ULONG>> ctSharedMemoryPerfCounter<ULONG>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_Memory = L"Win32_PerfFormattedData_PerfOS_Memory";

        if (ctString::iordinal_equals(_counter_name, L"CacheFaultsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"DemandZeroFaultsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"FreeSystemPageTableEntries") ||
            ctString::iordinal_equals(_counter_name, L"PageFaultsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PageReadsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PagesInputPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PagesOutputPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PagesPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PageWritesPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PercentCommittedBytesInUse") ||
            ctString::iordinal_equals(_counter_name, L"PoolNonpagedAllocs") ||
            ctString::iordinal_equals(_counter_name, L"PoolPagedAllocs") ||
            ctString::iordinal_equals(_counter_name, L"TransitionFaultsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"WriteCopiesPerSec")) {
            // a static perf counter
            return ctMakeStaticPerfCounter<ULONG>(ctWmiPerformanceClass_Memory, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedMemoryPerfCounter counter name %s [from class Win32_PerfFormattedData_PerfOS_Memory] does not have a ULONG counter type",
                _counter_name).c_str(),
            L"ctSharedMemoryPerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ctComBstr>> ctSharedMemoryPerfCounter<ctComBstr>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_Memory = L"Win32_PerfFormattedData_PerfOS_Memory";

        if (ctString::iordinal_equals(_counter_name, L"Caption") ||
            ctString::iordinal_equals(_counter_name, L"Description") ||
            ctString::iordinal_equals(_counter_name, L"Name")) {
            // a static perf counter
            return ctMakeStaticPerfCounter<ctComBstr>(ctWmiPerformanceClass_Memory, _counter_name);
        }
        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedMemoryPerfCounter counter name %s [from class Win32_PerfFormattedData_PerfOS_Memory] does not have a string counter type",
                _counter_name).c_str(),
            L"ctSharedMemoryPerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<std::wstring>> ctSharedMemoryPerfCounter<std::wstring>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_Memory = L"Win32_PerfFormattedData_PerfOS_Memory";

        if (ctString::iordinal_equals(_counter_name, L"Caption") ||
            ctString::iordinal_equals(_counter_name, L"Description") ||
            ctString::iordinal_equals(_counter_name, L"Name")) {
            // a static perf counter
            return ctMakeStaticPerfCounter<std::wstring>(ctWmiPerformanceClass_Memory, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedMemoryPerfCounter counter name %s [from class Win32_PerfFormattedData_PerfOS_Memory] does not have a string counter type",
                _counter_name).c_str(),
            L"ctSharedMemoryPerfCounter",
            true);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctSharedProcessorPerfCounter specializations
    /// - colleecting from the instance hi-perf WMI class Win32_PerfFormattedData_Counters_ProcessorInformation
    ///   *Not* using the older Win32_PerfFormattedData_PerfOS_Processor which is now considered less accurate
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> ctSharedProcessorPerfCounter<ULONGLONG>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_ProcessorInformation = L"Win32_PerfFormattedData_Counters_ProcessorInformation";

        if (ctString::iordinal_equals(_counter_name, L"AverageIdleTime") ||
            ctString::iordinal_equals(_counter_name, L"C1TransitionsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"C2TransitionsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"C3TransitionsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"IdleBreakEventsPersec") ||
            ctString::iordinal_equals(_counter_name, L"PercentC1Time") ||
            ctString::iordinal_equals(_counter_name, L"PercentC2Time") ||
            ctString::iordinal_equals(_counter_name, L"PercentC3Time") ||
            ctString::iordinal_equals(_counter_name, L"PercentDPCTime") ||
            ctString::iordinal_equals(_counter_name, L"PercentIdleTime") ||
            ctString::iordinal_equals(_counter_name, L"PercentInterruptTime") ||
            ctString::iordinal_equals(_counter_name, L"PercentPriorityTime") ||
            ctString::iordinal_equals(_counter_name, L"PercentPrivilegedTime") ||
            ctString::iordinal_equals(_counter_name, L"PercentPrivilegedUtility") ||
            ctString::iordinal_equals(_counter_name, L"PercentProcessorPerformance") ||
            ctString::iordinal_equals(_counter_name, L"PercentProcessorTime") ||
            ctString::iordinal_equals(_counter_name, L"PercentProcessorUtility") ||
            ctString::iordinal_equals(_counter_name, L"PercentUserTime")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<ULONGLONG>(ctWmiPerformanceClass_ProcessorInformation, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedProcessorPerfCounter counter name %s [from class Win32_PerfFormattedData_Counters_ProcessorInformation] does not have a ULONGLONG counter type",
                _counter_name).c_str(),
            L"ctSharedProcessorPerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ULONG>> ctSharedProcessorPerfCounter<ULONG>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_ProcessorInformation = L"Win32_PerfFormattedData_Counters_ProcessorInformation";

        if (ctString::iordinal_equals(_counter_name, L"ClockInterruptsPersec") ||
            ctString::iordinal_equals(_counter_name, L"DPCRate") ||
            ctString::iordinal_equals(_counter_name, L"DPCsQueuedPersec") ||
            ctString::iordinal_equals(_counter_name, L"InterruptsPersec") ||
            ctString::iordinal_equals(_counter_name, L"ParkingStatus") ||
            ctString::iordinal_equals(_counter_name, L"PercentofMaximumFrequency") ||
            ctString::iordinal_equals(_counter_name, L"PercentPerformanceLimit") ||
            ctString::iordinal_equals(_counter_name, L"PerformanceLimitFlags") ||
            ctString::iordinal_equals(_counter_name, L"ProcessorFrequency") ||
            ctString::iordinal_equals(_counter_name, L"ProcessorStateFlags")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<ULONG>(ctWmiPerformanceClass_ProcessorInformation, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedProcessorPerfCounter counter name %s [from class Win32_PerfFormattedData_Counters_ProcessorInformation] does not have a ULONG counter type",
                _counter_name).c_str(),
            L"ctSharedProcessorPerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ctComBstr>> ctSharedProcessorPerfCounter<ctComBstr>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_ProcessorInformation = L"Win32_PerfFormattedData_Counters_ProcessorInformation";

        if (ctString::iordinal_equals(_counter_name, L"Caption") ||
            ctString::iordinal_equals(_counter_name, L"Description") ||
            ctString::iordinal_equals(_counter_name, L"Name")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<ctComBstr>(ctWmiPerformanceClass_ProcessorInformation, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedProcessorPerfCounter counter name %s [from class Win32_PerfFormattedData_Counters_ProcessorInformation] does not have a string counter type",
                _counter_name).c_str(),
            L"ctSharedProcessorPerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<std::wstring>> ctSharedProcessorPerfCounter<std::wstring>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_ProcessorInformation = L"Win32_PerfFormattedData_Counters_ProcessorInformation";

        if (ctString::iordinal_equals(_counter_name, L"Caption") ||
            ctString::iordinal_equals(_counter_name, L"Description") ||
            ctString::iordinal_equals(_counter_name, L"Name")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<std::wstring>(ctWmiPerformanceClass_ProcessorInformation, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedProcessorPerfCounter counter name %s [from class Win32_PerfFormattedData_Counters_ProcessorInformation] does not have a string counter type",
                _counter_name).c_str(),
            L"ctSharedProcessorPerfCounter",
            true);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctSharedProcessPerfCounter specializations
    /// - colleecting from the instance hi-perf WMI class Win32_PerfFormattedData_PerfProc_Process
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> ctSharedProcessPerfCounter<ULONGLONG>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_Process = L"Win32_PerfFormattedData_PerfProc_Process";

        if (ctString::iordinal_equals(_counter_name, L"ElapsedTime") ||
            ctString::iordinal_equals(_counter_name, L"Frequency_Object") ||
            ctString::iordinal_equals(_counter_name, L"Frequency_PerfTime") ||
            ctString::iordinal_equals(_counter_name, L"Frequency_Sys100NS") ||
            ctString::iordinal_equals(_counter_name, L"IODataBytesPerSec") ||
            ctString::iordinal_equals(_counter_name, L"IODataOperationsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"IOOtherBytesPerSec") ||
            ctString::iordinal_equals(_counter_name, L"IOOtherOperationsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"IOReadBytesPerSec") ||
            ctString::iordinal_equals(_counter_name, L"IOReadOperationsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"IOWriteBytesPerSec") ||
            ctString::iordinal_equals(_counter_name, L"IOWriteOperationsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PageFileBytes") ||
            ctString::iordinal_equals(_counter_name, L"PageFileBytesPeak") ||
            ctString::iordinal_equals(_counter_name, L"PercentPrivilegedTime") ||
            ctString::iordinal_equals(_counter_name, L"PercentProcessorTime") ||
            ctString::iordinal_equals(_counter_name, L"PercentUserTime") ||
            ctString::iordinal_equals(_counter_name, L"PrivateBytes") ||
            ctString::iordinal_equals(_counter_name, L"Timestamp_Object") ||
            ctString::iordinal_equals(_counter_name, L"Timestamp_PerfTime") ||
            ctString::iordinal_equals(_counter_name, L"Timestamp_Sys100NS") ||
            ctString::iordinal_equals(_counter_name, L"VirtualBytes") ||
            ctString::iordinal_equals(_counter_name, L"VirtualBytesPeak") ||
            ctString::iordinal_equals(_counter_name, L"WorkingSet") ||
            ctString::iordinal_equals(_counter_name, L"WorkingSetPeak")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<ULONGLONG>(ctWmiPerformanceClass_Process, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedProcessPerfCounter counter name %s [from class Win32_PerfFormattedData_PerfProc_Process] does not have a ULONGLONG counter type",
                _counter_name).c_str(),
            L"ctSharedProcessorPerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ULONG>> ctSharedProcessPerfCounter<ULONG>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_Process = L"Win32_PerfFormattedData_PerfProc_Process";

        if (ctString::iordinal_equals(_counter_name, L"CreatingProcessID") ||
            ctString::iordinal_equals(_counter_name, L"HandleCount") ||
            ctString::iordinal_equals(_counter_name, L"IDProcess") ||
            ctString::iordinal_equals(_counter_name, L"PageFaultsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PoolNonpagedBytes") ||
            ctString::iordinal_equals(_counter_name, L"PoolPagedBytes") ||
            ctString::iordinal_equals(_counter_name, L"PriorityBase") ||
            ctString::iordinal_equals(_counter_name, L"ThreadCount")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<ULONG>(ctWmiPerformanceClass_Process, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedProcessPerfCounter counter name %s [from class Win32_PerfFormattedData_PerfProc_Process] does not have a ULONG counter type",
                _counter_name).c_str(),
            L"ctSharedProcessorPerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ctComBstr>> ctSharedProcessPerfCounter<ctComBstr>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_Process = L"Win32_PerfFormattedData_PerfProc_Process";

        if (ctString::iordinal_equals(_counter_name, L"Caption") ||
            ctString::iordinal_equals(_counter_name, L"Description") ||
            ctString::iordinal_equals(_counter_name, L"Name")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<ctComBstr>(ctWmiPerformanceClass_Process, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedProcessPerfCounter counter name %s [from class Win32_PerfFormattedData_PerfProc_Process] does not have a string counter type",
                _counter_name).c_str(),
            L"ctSharedProcessorPerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<std::wstring>> ctSharedProcessPerfCounter<std::wstring>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_Process = L"Win32_PerfFormattedData_PerfProc_Process";

        if (ctString::iordinal_equals(_counter_name, L"Caption") ||
            ctString::iordinal_equals(_counter_name, L"Description") ||
            ctString::iordinal_equals(_counter_name, L"Name")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<std::wstring>(ctWmiPerformanceClass_Process, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedProcessPerfCounter counter name %s [from class Win32_PerfFormattedData_PerfProc_Process] does not have a string counter type",
                _counter_name).c_str(),
            L"ctSharedProcessorPerfCounter",
            true);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctSharedMemoryPerfCounter specializations
    /// - colleecting from the instance hi-perf WMI class Win32_PerfFormattedData_Tcpip_NetworkInterface
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> ctSharedNetworkInterfacePerfCounter<ULONGLONG>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_NetworkInterface = L"Win32_PerfFormattedData_Tcpip_NetworkInterface";

        if (ctString::iordinal_equals(_counter_name, L"BytesTotalPerSec") ||
            ctString::iordinal_equals(_counter_name, L"Frequency_Object") ||
            ctString::iordinal_equals(_counter_name, L"Frequency_PerfTime") ||
            ctString::iordinal_equals(_counter_name, L"Frequency_Sys100NS") ||
            ctString::iordinal_equals(_counter_name, L"Timestamp_Object") ||
            ctString::iordinal_equals(_counter_name, L"Timestamp_PerfTime") ||
            ctString::iordinal_equals(_counter_name, L"Timestamp_Sys100NS")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<ULONGLONG>(ctWmiPerformanceClass_NetworkInterface, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedNetworkInterfacePerfCounter counter name %s [from class Win32_PerfFormattedData_Tcpip_NetworkInterface] does not have a ULONGLONG counter type",
                _counter_name).c_str(),
            L"ctSharedNetworkInterfacePerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ULONG>> ctSharedNetworkInterfacePerfCounter<ULONG>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_NetworkInterface = L"Win32_PerfFormattedData_Tcpip_NetworkInterface";

        if (ctString::iordinal_equals(_counter_name, L"BytesReceivedPerSec") ||
            ctString::iordinal_equals(_counter_name, L"BytesSentPerSec") ||
            ctString::iordinal_equals(_counter_name, L"CurrentBandwidth") ||
            ctString::iordinal_equals(_counter_name, L"OutputQueueLength") ||
            ctString::iordinal_equals(_counter_name, L"PacketsOutboundDiscarded") ||
            ctString::iordinal_equals(_counter_name, L"PacketsOutboundErrors") ||
            ctString::iordinal_equals(_counter_name, L"PacketsPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PacketsReceivedDiscarded") ||
            ctString::iordinal_equals(_counter_name, L"PacketsReceivedErrors") ||
            ctString::iordinal_equals(_counter_name, L"PacketsReceivedNonUnicastPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PacketsReceivedPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PacketsReceivedUnicastPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PacketsReceivedUnknown") ||
            ctString::iordinal_equals(_counter_name, L"PacketsSentNonUnicastPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PacketsSentPerSec") ||
            ctString::iordinal_equals(_counter_name, L"PacketsSentUnicastPerSec")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<ULONG>(ctWmiPerformanceClass_NetworkInterface, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedNetworkInterfacePerfCounter counter name %s [from class Win32_PerfFormattedData_Tcpip_NetworkInterface] does not have a ULONG counter type",
                _counter_name).c_str(),
            L"ctSharedNetworkInterfacePerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<ctComBstr>> ctSharedNetworkInterfacePerfCounter<ctComBstr>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_NetworkInterface = L"Win32_PerfFormattedData_Tcpip_NetworkInterface";

        if (ctString::iordinal_equals(_counter_name, L"Caption") ||
            ctString::iordinal_equals(_counter_name, L"Description") ||
            ctString::iordinal_equals(_counter_name, L"Name")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<ctComBstr>(ctWmiPerformanceClass_NetworkInterface, _counter_name);
        }
        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedNetworkInterfacePerfCounter counter name %s [from class Win32_PerfFormattedData_Tcpip_NetworkInterface] does not have a string counter type",
                _counter_name).c_str(),
            L"ctSharedNetworkInterfacePerfCounter",
            true);
    }

    template <>
    inline std::shared_ptr<ctWmiPerformanceCounter<std::wstring>> ctSharedNetworkInterfacePerfCounter<std::wstring>(_In_ LPCWSTR _counter_name)
    {
        static LPCWSTR ctWmiPerformanceClass_NetworkInterface = L"Win32_PerfFormattedData_Tcpip_NetworkInterface";

        if (ctString::iordinal_equals(_counter_name, L"Caption") ||
            ctString::iordinal_equals(_counter_name, L"Description") ||
            ctString::iordinal_equals(_counter_name, L"Name")) {
            // an instance perf counter
            return ctMakeInstancePerfCounter<std::wstring>(ctWmiPerformanceClass_NetworkInterface, _counter_name);
        }

        throw ctException(
            ERROR_INVALID_NAME,
            ctString::format_string(
                L"ctSharedNetworkInterfacePerfCounter counter name %s [from class Win32_PerfFormattedData_Tcpip_NetworkInterface] does not have a string counter type",
                _counter_name).c_str(),
            L"ctSharedNetworkInterfacePerfCounter",
            true);
    }

} // ctl namespace

