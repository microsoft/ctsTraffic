/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <cwchar>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include <utility>
// os headers
#include <windows.h>
#include <OleAuto.h>
#include <Wbemidl.h>
// wil headers
#include <wil/resource.h>
#include <wil/com.h>
// local headers
#include "ctWmiService.hpp"
#include "ctWmiClassObject.hpp"

namespace ctl
{
    namespace details
    {
        inline wil::unique_variant create_variant_array(const std::vector<std::wstring>& data)
        {
            const auto temp_safe_array = ::SafeArrayCreateVector(VT_BSTR, 0, static_cast<ULONG>(data.size()));
            THROW_IF_NULL_ALLOC(temp_safe_array);
            auto guard_array = wil::scope_exit([&]() {::SafeArrayDestroy(temp_safe_array); });

            for (size_t loop = 0; loop < data.size(); ++loop)
            {
                // SafeArrayPutElement requires an array of indexes for each dimension of the array
                // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
                long index[1] = { static_cast<long>(loop) };

                const auto bstr = ::SysAllocString(data[loop].c_str());
                THROW_IF_NULL_ALLOC(bstr);
                THROW_IF_FAILED(::SafeArrayPutElement(temp_safe_array, index, bstr));
            }

            wil::unique_variant variant;
            variant.parray = temp_safe_array;
            variant.vt = VT_BSTR | VT_ARRAY;

            // don't free the SAFEARRAY on success - its lifetime is transferred to variant
            guard_array.release();
            return variant;
        }

        inline wil::unique_variant create_variant_array(const std::vector<unsigned long>& data)
        {
            const auto temp_safe_array = ::SafeArrayCreateVector(VT_UI4, 0, static_cast<ULONG>(data.size()));
            THROW_IF_NULL_ALLOC(temp_safe_array);
            auto guard_array = wil::scope_exit([&]() {::SafeArrayDestroy(temp_safe_array); });

            for (size_t loop = 0; loop < data.size(); ++loop)
            {
                // SafeArrayPutElement requires an array of indexes for each dimension of the array
                // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
                long index[1] = { static_cast<long>(loop) };

                unsigned long value = data[loop];
                THROW_IF_FAILED(::SafeArrayPutElement(temp_safe_array, index, &value));
            }

            wil::unique_variant variant;
            variant.parray = temp_safe_array;
            variant.vt = VT_UI4 | VT_ARRAY;

            // don't free the SAFEARRAY on success - its lifetime is transferred to variant
            guard_array.release();
            return variant;
        }

        inline wil::unique_variant create_variant_array(const std::vector<unsigned short>& data)
        {
            // WMI marshaler complaines type mismatch using VT_UI2 | VT_ARRAY, and VT_I4 | VT_ARRAY works fine.
            const auto temp_safe_array = ::SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(data.size()));
            THROW_IF_NULL_ALLOC(temp_safe_array);
            auto guard_array = wil::scope_exit([&]() {::SafeArrayDestroy(temp_safe_array); });

            for (size_t loop = 0; loop < data.size(); ++loop)
            {
                // SafeArrayPutElement requires an array of indexes for each dimension of the array
                // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
                long index[1] = { static_cast<long>(loop) };

                // Expand unsigned short to long because the SAFEARRAY created assumes VT_I4 elements
                long value = data[loop];
                THROW_IF_FAILED(::SafeArrayPutElement(temp_safe_array, index, &value));
            }

            wil::unique_variant variant;
            variant.parray = temp_safe_array;
            variant.vt = VT_I4 | VT_ARRAY;

            // don't free the SAFEARRAY on success - its lifetime is transferred to variant
            guard_array.release();
            return variant;
        }

        inline wil::unique_variant create_variant_array(const std::vector<unsigned char>& data)
        {
            const auto temp_safe_array = ::SafeArrayCreateVector(VT_UI1, 0, static_cast<ULONG>(data.size()));
            THROW_IF_NULL_ALLOC(temp_safe_array);
            auto guard_array = wil::scope_exit([&]() {::SafeArrayDestroy(temp_safe_array); });

            for (size_t loop = 0; loop < data.size(); ++loop)
            {
                // SafeArrayPutElement requires an array of indexes for each dimension of the array
                // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
                long index[1] = { static_cast<long>(loop) };

                unsigned char value = data[loop];
                THROW_IF_FAILED(::SafeArrayPutElement(temp_safe_array, index, &value));
            }

            wil::unique_variant variant;
            variant.parray = temp_safe_array;
            variant.vt = VT_UI1 | VT_ARRAY;

            // don't free the SAFEARRAY on success - its lifetime is transferred to variant
            guard_array.release();
            return variant;
        }

        inline std::vector<std::wstring> retrieve_from_BSTR_array(const wil::unique_variant& variant)
        {
            FAIL_FAST_IF(variant.vt != (VT_BSTR | VT_ARRAY));

            BSTR* stringArray{};
            THROW_IF_FAILED(::SafeArrayAccessData(variant.parray, reinterpret_cast<void**>(&stringArray)));
            auto unaccessArray = wil::scope_exit([&]() {::SafeArrayUnaccessData(variant.parray); });

            std::vector<std::wstring> returnData;
            for (unsigned loop = 0; loop < variant.parray->rgsabound[0].cElements; ++loop)
            {
                returnData.emplace_back(stringArray[loop]);
            }

            return returnData;
        }

        std::vector<unsigned long> retrieve_from_UI4_array(const wil::unique_variant& variant)
        {
            FAIL_FAST_IF(variant.vt != (VT_UI4 | VT_ARRAY));

            unsigned long* intArray{};
            THROW_IF_FAILED(::SafeArrayAccessData(variant.parray, reinterpret_cast<void**>(&intArray)));
            auto unaccessArray = wil::scope_exit([&]() {::SafeArrayUnaccessData(variant.parray); });

            std::vector<unsigned long> returnData;
            for (unsigned loop = 0; loop < variant.parray->rgsabound[0].cElements; ++loop)
            {
                returnData.push_back(intArray[loop]);
            }

            return returnData;
        }

        inline std::vector<unsigned short> retrieve_from_I4_array(const wil::unique_variant& variant)
        {
            // WMI marshaler complaines type mismatch using VT_UI2 | VT_ARRAY, and VT_I4 | VT_ARRAY works fine.
            FAIL_FAST_IF(variant.vt != (VT_I4 | VT_ARRAY));

            long* intArray{};
            THROW_IF_FAILED(::SafeArrayAccessData(variant.parray, reinterpret_cast<void**>(&intArray)));
            auto unaccessArray = wil::scope_exit([&]() {::SafeArrayUnaccessData(variant.parray); });

            std::vector<unsigned short> returnData;
            for (unsigned loop = 0; loop < variant.parray->rgsabound[0].cElements; ++loop)
            {
                THROW_HR_IF(E_INVALIDARG, intArray[loop] > MAXUINT16);
                returnData.push_back(static_cast<unsigned short>(intArray[loop]));
            }

            return returnData;
        }

        inline std::vector<unsigned char> retrieve_from_UI1_array(const wil::unique_variant& variant)
        {
            FAIL_FAST_IF(variant.vt != (VT_UI1 | VT_ARRAY));

            unsigned char* charArray{};
            THROW_IF_FAILED(::SafeArrayAccessData(variant.parray, reinterpret_cast<void**>(&charArray)));
            auto unaccessArray = wil::scope_exit([&]() {::SafeArrayUnaccessData(variant.parray); });

            std::vector<unsigned char> returnData;
            for (unsigned loop = 0; loop < variant.parray->rgsabound[0].cElements; ++loop)
            {
                returnData.push_back(charArray[loop]);
            }

            return returnData;
        }

        template <typename T>
        wil::com_ptr<T> retrieve_from_comptr(const wil::unique_variant& variant)
        {
            FAIL_FAST_IF(variant.vt != VT_UNKNOWN);

            wil::com_ptr<T> returnComPtr;
            THROW_IF_FAILED(variant.punkVal->QueryInterface(__uuidof(T), reinterpret_cast<void**>(returnComPtr.put())));
            return returnComPtr;
        }

        template <typename T>
        std::vector<wil::com_ptr<T>> retrieve_from_comptr_array(const wil::unique_variant& variant)
        {
            FAIL_FAST_IF(variant.vt != (VT_UNKNOWN | VT_ARRAY));

            IUnknown** iUnknownArray;
            THROW_IF_FAILED(::SafeArrayAccessData(variant.parray, reinterpret_cast<void**>(&iUnknownArray)));
            auto unaccessArray = wil::scope_exit([&]() {::SafeArrayUnaccessData(variant.parray); });

            std::vector<wil::com_ptr<T>> returnData;
            for (unsigned loop = 0; loop < variant.parray->rgsabound[0].cElements; ++loop)
            {
                wil::com_ptr<T> tempPtr;
                THROW_IF_FAILED(iUnknownArray[loop]->QueryInterface(__uuidof(T), reinterpret_cast<void**>(tempPtr.put())));
                returnData.push_back(tempPtr);
            }

            return returnData;
        }
    }
    
    class ctWmiInstance
    {
    public:
        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// Constructors:
        /// - requires a IWbemServices object already connected to WMI
        ///   
        /// - one c'tor creates an empty instance (if set later)
        /// - one c'tor takes the WMI class name to instantiate a new instance
        /// - one c'tor takes an existing IWbemClassObject instance
        ///
        /// Default d'tor, copy c'tor, and copy assignment operator
        ///
        ////////////////////////////////////////////////////////////////////////////////
        explicit ctWmiInstance(ctWmiService service) noexcept :
            m_wbemServices(std::move(service))
        {
        }

        ctWmiInstance(ctWmiService service, PCWSTR className) :
            m_wbemServices(std::move(service))
        {
            // get the object from the WMI service
            wil::com_ptr<IWbemClassObject> classObject;
            THROW_IF_FAILED(m_wbemServices->GetObject(
                wil::make_bstr(className).get(),
                0,
                nullptr,
                classObject.addressof(),
                nullptr));
            // spawn an instance of this object
            THROW_IF_FAILED(classObject->SpawnInstance(
                0,
                m_instanceObject.put()));
        }

        ctWmiInstance(ctWmiService service, wil::com_ptr<IWbemClassObject> classObject) noexcept :
            m_wbemServices(std::move(service)),
            m_instanceObject(std::move(classObject))
        {
        }

        bool operator ==(const ctWmiInstance& _obj) const noexcept
        {
            return m_wbemServices == _obj.m_wbemServices &&
                m_instanceObject == _obj.m_instanceObject;
        }

        bool operator !=(const ctWmiInstance& _obj) const noexcept
        {
            return !(*this == _obj);
        }

        wil::unique_bstr get_path() const
        {
            wil::unique_variant object_path_variant;
            get(L"__RELPATH", object_path_variant.addressof());

            if (V_VT(&object_path_variant) == VT_EMPTY || V_VT(&object_path_variant) == VT_NULL)
            {
                return nullptr;
            }
            if (V_VT(&object_path_variant) == VT_BSTR)
            {
                return wil::make_bstr(V_BSTR(&object_path_variant));
            }

            THROW_HR(E_INVALIDARG);
        }

        ctWmiService get_service() const noexcept
        {
            return m_wbemServices;
        }

        // Retrieves the class name this ctWmiInstance is representing if any
        wil::unique_bstr get_class_name() const
        {
            wil::unique_variant class_variant;
            get(L"__CLASS", class_variant.addressof());

            if (V_VT(&class_variant) == VT_EMPTY || V_VT(&class_variant) == VT_NULL)
            {
                return nullptr;
            }
            if (V_VT(&class_variant) == VT_BSTR)
            {
                return wil::make_bstr(V_BSTR(&class_variant));
            }

            THROW_HR(E_INVALIDARG);
        }

        wil::com_ptr<IWbemClassObject> get_instance_object() const noexcept
        {
            return m_instanceObject;
        }

        // Returns a class object for the class represented by this instance
        ctWmiClassObject get_class_object() const noexcept
        {
            return { m_wbemServices, m_instanceObject };
        }

        /// Writes the instantiated object to the WMI repository
        // Supported wbemFlags:
        //   WBEM_FLAG_CREATE_OR_UPDATE
        //   WBEM_FLAG_UPDATE_ONLY
        //   WBEM_FLAG_CREATE_ONLY
        void write_class_object(const wil::com_ptr<IWbemContext>& context, const LONG wbem_flags = WBEM_FLAG_CREATE_OR_UPDATE)
        {
            wil::com_ptr<IWbemCallResult> result;
            THROW_IF_FAILED(m_wbemServices->PutInstance(
                m_instanceObject.get(),
                wbem_flags | WBEM_FLAG_RETURN_IMMEDIATELY,
                const_cast<IWbemContext*>(context.get()),
                result.addressof()));

            // wait for the call to complete
            HRESULT status;
            THROW_IF_FAILED(result->GetCallStatus(WBEM_INFINITE, &status));
            THROW_IF_FAILED(status);
        }

        void write_class_object(LONG wbemFlags = WBEM_FLAG_CREATE_OR_UPDATE)
        {
            const wil::com_ptr<IWbemContext> null_context;
            write_class_object(null_context, wbemFlags);
        }

        void delete_class_object()
        {
            // delete the instance based off the __REPATH property
            wil::com_ptr<IWbemCallResult> result;
            THROW_IF_FAILED(m_wbemServices->DeleteInstance(
                get_path().get(),
                WBEM_FLAG_RETURN_IMMEDIATELY,
                nullptr,
                result.addressof()));

            // wait for the call to complete
            HRESULT status;
            THROW_IF_FAILED(result->GetCallStatus(WBEM_INFINITE, &status));
            THROW_IF_FAILED(status);
        }

        // Invokes an instance method with zero arguments from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        ctWmiInstance execute_method(_In_ PCWSTR _method)
        {
            return execute_method_impl(_method, nullptr);
        }

        // Invokes an instance method with one argument from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1>
        ctWmiInstance execute_method(_In_ PCWSTR _method, Arg1 _arg1)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                _method,
                0,
                inParamsDefinition.addressof(),
                nullptr));

            // spawn an instance to store the params
            wil::com_ptr<IWbemClassObject> inParamsInstance;
            THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

            // Instantiate a class object to iterate through each property
            const ctWmiClassObject property_object(m_wbemServices, inParamsDefinition);
            auto property_iter = property_object.property_begin();

            // write the property
            ctWmiInstance propertyclassObject(m_wbemServices, inParamsInstance);
            propertyclassObject.set(*property_iter, _arg1);

            // execute the method with the properties set
            return execute_method_impl(_method, inParamsInstance.get());
        }

        // Invokes an instance method with two arguments from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1, typename Arg2>
        ctWmiInstance execute_method(_In_ PCWSTR _method, Arg1 _arg1, Arg2 _arg2)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                _method,
                0,
                inParamsDefinition.addressof(),
                nullptr));

            // spawn an instance to store the params
            wil::com_ptr<IWbemClassObject> inParamsInstance;
            THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

            // Instantiate a class object to iterate through each property
            const ctWmiClassObject property_object(m_wbemServices, inParamsDefinition);
            auto property_iter = property_object.property_begin();

            // write each property
            ctWmiInstance propertyclassObject(m_wbemServices, inParamsInstance);
            propertyclassObject.set(*property_iter, _arg1);
            ++property_iter;
            propertyclassObject.set(*property_iter, _arg2);

            // execute the method with the properties set
            return execute_method_impl(_method, inParamsInstance.get());
        }

        // Invokes an instance method with three arguments from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1, typename Arg2, typename Arg3>
        ctWmiInstance execute_method(_In_ PCWSTR _method, Arg1 _arg1, Arg2 _arg2, Arg3 _arg3)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                _method,
                0,
                inParamsDefinition.addressof(),
                nullptr));

            // spawn an instance to store the params
            wil::com_ptr<IWbemClassObject> inParamsInstance;
            THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

            // Instantiate a class object to iterate through each property
            const ctWmiClassObject property_object(m_wbemServices, inParamsDefinition);
            auto property_iter = property_object.property_begin();

            // write each property
            ctWmiInstance propertyclassObject(m_wbemServices, inParamsInstance);
            propertyclassObject.set(*property_iter, _arg1);
            ++property_iter;
            propertyclassObject.set(*property_iter, _arg2);
            ++property_iter;
            propertyclassObject.set(*property_iter, _arg3);

            // execute the method with the properties set
            return execute_method_impl(_method, inParamsInstance.get());
        }

        // Invokes an instance method with four arguments from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
        ctWmiInstance execute_method(_In_ PCWSTR _method, Arg1 _arg1, Arg2 _arg2, Arg3 _arg3, Arg4 _arg4)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                _method,
                0,
                inParamsDefinition.addressof(),
                nullptr));

            // spawn an instance to store the params
            wil::com_ptr<IWbemClassObject> inParamsInstance;
            THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

            // Instantiate a class object to iterate through each property
            const ctWmiClassObject property_object(m_wbemServices, inParamsDefinition);
            auto property_iter = property_object.property_begin();

            // write each property
            ctWmiInstance propertyclassObject(m_wbemServices, inParamsInstance);
            propertyclassObject.set(*property_iter, _arg1);
            ++property_iter;
            propertyclassObject.set(*property_iter, _arg2);
            ++property_iter;
            propertyclassObject.set(*property_iter, _arg3);
            ++property_iter;
            propertyclassObject.set(*property_iter, _arg4);

            // execute the method with the properties set
            return execute_method_impl(_method, inParamsInstance.get());
        }

        // Invokes an instance method with four arguments from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
        ctWmiInstance execute_method(_In_ PCWSTR _method, Arg1 _arg1, Arg2 _arg2, Arg3 _arg3, Arg4 _arg4, Arg5 _arg5)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                _method,
                0,
                inParamsDefinition.addressof(),
                nullptr));

            // spawn an instance to store the params
            wil::com_ptr<IWbemClassObject> inParamsInstance;
            THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

            // Instantiate a class object to iterate through each property
            const ctWmiClassObject property_object(m_wbemServices, inParamsDefinition);
            auto property_iter = property_object.property_begin();

            // write each property
            ctWmiInstance propertyclassObject(m_wbemServices, inParamsInstance);
            propertyclassObject.set(*property_iter, _arg1);
            ++property_iter;
            propertyclassObject.set(*property_iter, _arg2);
            ++property_iter;
            propertyclassObject.set(*property_iter, _arg3);
            ++property_iter;
            propertyclassObject.set(*property_iter, _arg4);

            // execute the method with the properties set
            return execute_method_impl(_method, inParamsInstance.get());
        }

        bool is_null(_In_ PCWSTR propname) const
        {
            wil::unique_variant local_variant;
            get_property(propname, local_variant.addressof());
            return V_VT(&local_variant) == VT_NULL;
        }

        // Calling IWbemClassObject::Delete on a property of an instance resets to the default value.
        void set_default(_In_ PCWSTR propname) const
        {
            THROW_IF_FAILED(m_instanceObject->Delete(propname));
        }

        // get() and set()
        //
        //   Exposes the properties of the WMI object instantiated
        //   WMI instances don't use all VARIANT types - some specializations
        //   exist because, for example, 64-bit integers actually get passed through
        //   WMI as BSTRs (even though variants support 64-bit integers directly).
        //   See the MSDN documentation for WMI MOF Data Types (Numbers):
        //   http://msdn.microsoft.com/en-us/library/aa392716(v=VS.85).aspx
        //
        //   Even though VARIANTs support 16- and 32-bit unsigned integers, WMI passes them both 
        //   around as 32-bit signed integers. Yes, that means you can't pass very large UINT32 values
        //   correctly through WMI directly.

        bool get(_In_ PCWSTR propname, _Inout_ VARIANT* value) const
        {
            ::VariantClear(value);
            return get_property(propname, value);
        }
        void set(_In_ PCWSTR propname, _In_ const VARIANT* value) const
        {
            set_property(propname, const_cast<VARIANT*>(value));
        }

        // bool
        bool get(_In_ PCWSTR propname, _Out_ bool* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_BOOL);
            *value = V_BOOL(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const bool value) const
        {
            wil::unique_variant local_variant;
            V_BOOL(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_BOOL;
            set_property(propname, local_variant.addressof());
        }

        // char
        bool get(_In_ PCWSTR propname, _Out_ char* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_UI1);
            *value = V_UI1(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const char value) const
        {
            wil::unique_variant local_variant;
            V_UI1(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_UI1;
            set_property(propname, local_variant.addressof());
        }

        // unsigned char
        bool get(_In_ PCWSTR propname, _Out_ unsigned char* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_UI1);
            *value = V_UI1(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const unsigned char value) const
        {
            wil::unique_variant local_variant;
            V_UI1(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_UI1;
            set_property(propname, local_variant.addressof());
        }

        // short
        bool get(_In_ PCWSTR propname, _Out_ short* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_I2);
            *value = V_I2(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const short value) const
        {
            wil::unique_variant local_variant;
            V_I2(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_I2;
            set_property(propname, local_variant.addressof());
        }

        // unsigned short
        bool get(_In_ PCWSTR propname, _Out_ unsigned short* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_I4);
            *value = static_cast<unsigned short>(V_I4(local_variant.addressof()));
            return true;
        }
        void set(_In_ PCWSTR propname, const unsigned short value) const
        {
            wil::unique_variant local_variant;
            V_I2(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_I2;
            set_property(propname, local_variant.addressof());
        }

        // long
        bool get(_In_ PCWSTR propname, _Out_ long* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_I4);
            *value = V_I4(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const long value) const
        {
            wil::unique_variant local_variant;
            V_I4(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_I4;
            set_property(propname, local_variant.addressof());
        }

        // unsigned long
        bool get(_In_ PCWSTR propname, _Out_ unsigned long* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_I4);
            *value = V_I4(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const unsigned long value) const
        {
            wil::unique_variant local_variant;
            V_I4(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_I4;
            set_property(propname, local_variant.addressof());
        }

        // int
        bool get(_In_ PCWSTR propname, _Out_ int* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_I4);
            *value = V_I4(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const int value) const
        {
            wil::unique_variant local_variant;
            V_I4(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_I4;
            set_property(propname, local_variant.addressof());
        }

        // unsigned int
        bool get(_In_ PCWSTR propname, _Out_ unsigned int* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_I4);
            *value = V_I4(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const unsigned int value) const
        {
            wil::unique_variant local_variant;
            V_I4(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_I4;
            set_property(propname, local_variant.addressof());
        }

        // float
        bool get(_In_ PCWSTR propname, _Out_ float* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_R4);
            *value = V_R4(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const float value) const
        {
            wil::unique_variant local_variant;
            V_R4(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_R4;
            set_property(propname, local_variant.addressof());
        }

        // double
        bool get(_In_ PCWSTR propname, _Out_ double* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_R8);
            *value = V_R8(local_variant.addressof());
            return true;
        }
        void set(_In_ PCWSTR propname, const double value) const
        {
            wil::unique_variant local_variant;
            V_R8(local_variant.addressof()) = value;
            V_VT(local_variant.addressof()) = VT_R8;
            set_property(propname, local_variant.addressof());
        }

        // SYSTEMTIME
        bool get(_In_ PCWSTR propname, _Out_ SYSTEMTIME* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_DATE);
            THROW_LAST_ERROR_IF(!::VariantTimeToSystemTime(V_DATE(local_variant.addressof()), value));
            return true;
        }
        void set(_In_ PCWSTR propname, const SYSTEMTIME& value) const
        {
            DOUBLE time;
            THROW_LAST_ERROR_IF(!::SystemTimeToVariantTime(const_cast<SYSTEMTIME*>(&value), &time));

            wil::unique_variant local_variant;
            V_DATE(local_variant.addressof()) = time;
            V_VT(local_variant.addressof()) = VT_DATE;
            set_property(propname, local_variant.addressof());
        }

        // BSTR
        bool get(_In_ PCWSTR propname, _Out_ wil::unique_bstr* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_BSTR);
            auto released_variant = local_variant.release();
            (*value).reset(V_BSTR(&released_variant));
            return true;
        }
        void set(_In_ PCWSTR propname, _In_ BSTR value) const
        {
            // using a raw VARIANT as to not free the caller's BSTR on exit
            VARIANT local_variant;
            V_BSTR(&local_variant) = value;
            V_VT(&local_variant) = VT_BSTR;
            set_property(propname, &local_variant);
        }
        bool get(_In_ PCWSTR propname, _Out_ std::wstring* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_BSTR);
            (*value).assign(V_BSTR(local_variant.addressof()));
            return true;
        }
        void set(_In_ PCWSTR propname, _In_ PCWSTR value) const
        {
            wil::unique_variant local_variant;
            V_VT(local_variant.addressof()) = VT_BSTR;
            V_BSTR(local_variant.addressof()) = ::SysAllocString(value);
            THROW_IF_NULL_ALLOC(V_BSTR(local_variant.addressof()));
            set_property(propname, local_variant.addressof());
        }

        // vector<wstring>
        bool get(_In_ PCWSTR propname, _Out_ std::vector<std::wstring>* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != (VT_BSTR | VT_ARRAY));
            *value = details::retrieve_from_BSTR_array(local_variant);
            return true;
        }
        void set(_In_ PCWSTR propname, const std::vector<std::wstring>& value) const
        {
            wil::unique_variant local_variant(details::create_variant_array(value));
            set_property(propname, local_variant.addressof());
        }

        // vector<unsigned long>
        bool get(_In_ PCWSTR propname, _Out_ std::vector<unsigned long>* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != (VT_UI4 | VT_ARRAY));
            *value = details::retrieve_from_UI4_array(local_variant);
            return true;
        }
        void set(_In_ PCWSTR propname, const std::vector<unsigned long>& value) const
        {
            wil::unique_variant local_variant(details::create_variant_array(value));
            set_property(propname, local_variant.addressof());
        }

        // vector<unsigned short>
        bool get(_In_ PCWSTR propname, _Out_ std::vector<unsigned short>* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != (VT_I4 | VT_ARRAY));
            *value = details::retrieve_from_I4_array(local_variant);
            return true;
        }
        void set(_In_ PCWSTR propname, const std::vector<unsigned short>& value) const
        {
            wil::unique_variant local_variant(details::create_variant_array(value));
            set_property(propname, local_variant.addressof());
        }

        // vector<unsigned char>
        bool get(_In_ PCWSTR propname, _Out_ std::vector<unsigned char>* value) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }

            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != (VT_UI1 | VT_ARRAY));
            *value = details::retrieve_from_UI1_array(local_variant);
            return true;
        }
        void set(_In_ PCWSTR propname, const std::vector<unsigned char>& value) const
        {
            wil::unique_variant local_variant(details::create_variant_array(value));
            set_property(propname, local_variant.addressof());
        }

        // Even though VARIANTs support 64-bit integers, WMI passes them around as BSTRs
        //
        // This does NOT do any checks to see whether the underlying BSTR is a valid number
        // if it's a BSTR but it didn't come from the relevant type of 64-bit integer, these getters will appear to succeed
        // 
        // The output value will be whatever _wcstoi64/_wcstoui64 on the BSTR returns
        // normally the appropriate MAX or MIN value on overflow and 0 on other errors
        bool get(_In_ PCWSTR propname, _Out_ UINT64* value) const
        {
            wil::unique_variant local_variant;
            if (!get(propname, local_variant.addressof()))
            {
                return false;
            }
            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_BSTR);

            *value = _wcstoui64(V_BSTR(local_variant.addressof()), nullptr, 10);
            return true;
        }
        void set(_In_ PCWSTR propname, const UINT64 value) const
        {
            const auto bstr = wil::make_bstr(std::to_wstring(value).c_str());
            // the bstr will be freed with wil::unique_btr, thus not using a unique_variant
            VARIANT local_variant;
            ::VariantInit(&local_variant);
            V_VT(&local_variant) = VT_BSTR;
            V_BSTR(&local_variant) = bstr.get();
            set_property(propname, &local_variant);
        }

        bool get(_In_ PCWSTR propname, _Out_ INT64* value) const
        {
            wil::unique_variant local_variant;
            if (!get(propname, local_variant.addressof()))
            {
                return false;
            }
            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_BSTR);

            *value = _wcstoi64(V_BSTR(local_variant.addressof()), nullptr, 10);
            return true;
        }
        void set(_In_ PCWSTR propname, const INT64 value) const
        {
            const auto bstr = wil::make_bstr(std::to_wstring(value).c_str());
            // the bstr will be freed with wil::unique_btr, thus not using a unique_variant
            VARIANT local_variant;
            ::VariantInit(&local_variant);
            V_VT(&local_variant) = VT_BSTR;
            V_BSTR(&local_variant) = bstr.get();
            set_property(propname, &local_variant);
        }

        // com_ptr
        template <typename T>
        bool get(_In_ PCWSTR propname, _Inout_ wil::com_ptr<T>* comptr) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }
            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != VT_UNKNOWN);

            *comptr = details::retrieve_from_comptr<T>(local_variant);
            return true;
        }
        template <typename T>
        void set(_In_ PCWSTR propname, const wil::com_ptr<T>& value) const
        {
            wil::unique_variant local_variant;
            V_VT(&local_variant) = VT_UNKNOWN;
            V_UNKNOWN(local_variant.addressof()) = value.get();
            // Need at AddRef the raw pointer assigned to punkVal in the variant
            V_UNKNOWN(local_variant.addressof())->AddRef();
            set_property(propname, &local_variant);
        }

        // vector<com_ptr>
        template <typename T>
        bool get(_In_ PCWSTR propname, _Inout_ std::vector<wil::com_ptr<T>>* classObjects) const
        {
            wil::unique_variant local_variant;
            if (!get_property(propname, local_variant.addressof()))
            {
                return false;
            }
            THROW_HR_IF(E_INVALIDARG, V_VT(local_variant.addressof()) != (VT_UNKNOWN | VT_ARRAY));

            *classObjects = details::retrieve_from_comptr_array<T>(local_variant);
            return true;
        }


    private:
        // returns true if not empty or null
        bool get_property(_In_ PCWSTR propertyName, _Inout_ VARIANT* pVariant) const
        {
            // since COM doesn't support marking methods const, calls to Get() are const_cast out of necessity
            auto pInstance = const_cast<IWbemClassObject*>(m_instanceObject.get());
            THROW_IF_FAILED(pInstance->Get(
                propertyName,
                0,
                pVariant,
                nullptr,
                nullptr));
            const auto isEmptyOrNull = V_VT(pVariant) == VT_EMPTY || V_VT(pVariant) == VT_NULL;
            return !isEmptyOrNull;
        }

        void set_property(_In_ PCWSTR propname, _In_ VARIANT* pVariant) const
        {
            THROW_IF_FAILED(m_instanceObject->Put(
                propname,
                0,
                pVariant,
                0));
        }

        ctWmiInstance execute_method_impl(_In_ PCWSTR method, _In_opt_ IWbemClassObject* _inParams)
        {
            // exec the method semi-synchronously from this instance based off the __REPATH property
            wil::com_ptr<IWbemCallResult> result;
            THROW_IF_FAILED(m_wbemServices->ExecMethod(
                get_path().get(),
                wil::make_bstr(method).get(),
                WBEM_FLAG_RETURN_IMMEDIATELY,
                nullptr,
                _inParams,
                nullptr,
                result.addressof()));

            // wait for the call to complete - and get the [out] param object
            wil::com_ptr<IWbemClassObject> outParamsInstance;
            THROW_IF_FAILED(result->GetResultObject(WBEM_INFINITE, outParamsInstance.addressof()));

            // the call went through - return a ctWmiInstance from this retrieved instance
            return { m_wbemServices, outParamsInstance };
        }

        ctWmiService m_wbemServices;
        wil::com_ptr<IWbemClassObject> m_instanceObject;
    };
} // namespace ctl
