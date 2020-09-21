/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <utility>
#include <vector>
#include <algorithm>
#include <utility>
// os headers
#include <Windows.h>
#include <objbase.h>
#include <OleAuto.h>
#include <WbemIdl.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
#include <wil/com.h>
// local headers
#include "ctWmiService.hpp"
#include "ctWmiClassObject.hpp"
#include "ctWmiVariant.hpp"


namespace ctl
{
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

        bool operator ==(const ctWmiInstance& obj) const noexcept
        {
            return m_wbemServices == obj.m_wbemServices &&
                m_instanceObject == obj.m_instanceObject;
        }

        bool operator !=(const ctWmiInstance& obj) const noexcept
        {
            return !(*this == obj);
        }

        [[nodiscard]] wil::unique_bstr get_path() const
        {
            wil::unique_variant object_path_variant;
            get(L"__RELPATH", object_path_variant.addressof());

            if (ctIsVariantEmptyOrNull(object_path_variant.addressof()))
            {
                return nullptr;
            }

            if (V_VT(&object_path_variant) != VT_BSTR)
            {
                THROW_HR(E_INVALIDARG);
            }

            return wil::make_bstr(V_BSTR(&object_path_variant));
        }

        [[nodiscard]] ctWmiService get_service() const noexcept
        {
            return m_wbemServices;
        }

        // Retrieves the class name this ctWmiInstance is representing if any
        [[nodiscard]] wil::unique_bstr get_class_name() const
        {
            wil::unique_variant class_variant;
            get(L"__CLASS", class_variant.addressof());

            if (ctIsVariantEmptyOrNull(class_variant.addressof()))
            {
                return nullptr;
            }
            if (V_VT(&class_variant) != VT_BSTR)
            {
                THROW_HR(E_INVALIDARG);
            }

            return wil::make_bstr(V_BSTR(&class_variant));
        }

        [[nodiscard]] wil::com_ptr<IWbemClassObject> get_instance_object() const noexcept
        {
            return m_instanceObject;
        }

        // Returns a class object for the class represented by this instance
        [[nodiscard]] ctWmiClassObject get_class_object() const noexcept
        {
            return { m_wbemServices, m_instanceObject };
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
        //
        // get() returns false if the value is empty or null
        //       returns true if retrieved the matching type

        bool get(_In_ PCWSTR propname, _Inout_ VARIANT* value) const
        {
            VariantClear(value);
            get_property(propname, value);
            return !ctIsVariantEmptyOrNull(value);
        }
        void set(_In_ PCWSTR propname, _In_ const VARIANT* value) const
        {
            set_property(propname, const_cast<VARIANT*>(value));
        }

        template <typename T>
        void set(_In_ PCWSTR propname, const T value) const
        {
            set_property(propname, ctWmiMakeVariant(value).addressof());
        }

        template <typename T>
        bool get(_In_ PCWSTR propname, _Out_ T* value) const
        {
            wil::unique_variant variant;
            get_property(propname, variant.addressof());
            return ctWmiReadFromVariant(variant.addressof(), value);
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
        ctWmiInstance execute_method(_In_ PCWSTR method)
        {
            return execute_method_impl(method, nullptr);
        }

        // Invokes an instance method with one argument from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1>
        ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                method,
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
            propertyclassObject.set(*property_iter, arg1);

            // execute the method with the properties set
            return execute_method_impl(method, inParamsInstance.get());
        }

        // Invokes an instance method with two arguments from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1, typename Arg2>
        ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1, Arg2 arg2)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                method,
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
            propertyclassObject.set(*property_iter, arg1);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg2);

            // execute the method with the properties set
            return execute_method_impl(method, inParamsInstance.get());
        }

        // Invokes an instance method with three arguments from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1, typename Arg2, typename Arg3>
        ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1, Arg2 arg2, Arg3 arg3)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                method,
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
            propertyclassObject.set(*property_iter, arg1);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg2);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg3);

            // execute the method with the properties set
            return execute_method_impl(method, inParamsInstance.get());
        }

        // Invokes an instance method with four arguments from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
        ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                method,
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
            propertyclassObject.set(*property_iter, arg1);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg2);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg3);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg4);

            // execute the method with the properties set
            return execute_method_impl(method, inParamsInstance.get());
        }

        // Invokes an instance method with five arguments from the instantiated IWbemClassObject
        // Returns a ctWmiInstace containing the [out] parameters from the method call
        //   (the property "ReturnValue" contains the return value)
        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
        ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5)
        {
            // establish the class object for the [in] params to the method
            wil::com_ptr<IWbemClassObject> inParamsDefinition;
            THROW_IF_FAILED(m_instanceObject->GetMethod(
                method,
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
            propertyclassObject.set(*property_iter, arg1);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg2);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg3);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg4);
            ++property_iter;
            propertyclassObject.set(*property_iter, arg5);

            // execute the method with the properties set
            return execute_method_impl(method, inParamsInstance.get());
        }

        bool is_null(_In_ PCWSTR propname) const
        {
            wil::unique_variant variant;
            get_property(propname, variant.addressof());
            return V_VT(&variant) == VT_NULL;
        }

        // Calling IWbemClassObject::Delete on a property of an instance resets to the default value.
        void set_default(_In_ PCWSTR propname) const
        {
            THROW_IF_FAILED(m_instanceObject->Delete(propname));
        }

    private:
        void get_property(_In_ PCWSTR propertyName, _Inout_ VARIANT* pVariant) const
        {
            // since COM doesn't support marking methods const, calls to Get() are const_cast out of necessity
            auto* pInstance = const_cast<IWbemClassObject*>(m_instanceObject.get());
            THROW_IF_FAILED(pInstance->Get(
                propertyName,
                0,
                pVariant,
                nullptr,
                nullptr));
        }

        void set_property(_In_ PCWSTR propname, _In_ VARIANT* pVariant) const
        {
            THROW_IF_FAILED(m_instanceObject->Put(
                propname,
                0,
                pVariant,
                0));
        }

        ctWmiInstance execute_method_impl(_In_ PCWSTR method, _In_opt_ IWbemClassObject* inParams)
        {
            // exec the method semi-synchronously from this instance based off the __REPATH property
            wil::com_ptr<IWbemCallResult> result;
            THROW_IF_FAILED(m_wbemServices->ExecMethod(
                get_path().get(),
                wil::make_bstr(method).get(),
                WBEM_FLAG_RETURN_IMMEDIATELY,
                nullptr,
                inParams,
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
