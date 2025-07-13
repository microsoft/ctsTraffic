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
#include <exception>
#include <stdexcept>
#include <utility>
// os headers
#include <Windows.h>
#include <oleauto.h>
#include <WbemIdl.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
#include <wil/com.h>
// local headers
#include "ctWmiService.hpp"


namespace ctl
{
// class ctWmiClassObject
// Exposes enumerating properties of a WMI Provider through a property_iterator interface.
class ctWmiClassObject
{
private:
    ctWmiService m_wbemServices;
    wil::com_ptr<IWbemClassObject> m_wbemClassObject;

public:
    //
    // forward declare iterator classes
    //
    class property_iterator;
    class method_iterator;

    ctWmiClassObject(ctWmiService wbemServices, wil::com_ptr<IWbemClassObject> wbemClass) noexcept :
        m_wbemServices(std::move(wbemServices)),
        m_wbemClassObject(std::move(wbemClass))
    {
    }

    ctWmiClassObject(ctWmiService wbemServices, _In_ PCWSTR className) :
        m_wbemServices(std::move(wbemServices))
    {
        THROW_IF_FAILED(m_wbemServices->GetObject(
            wil::make_bstr(className).get(),
            0,
            nullptr,
            m_wbemClassObject.put(),
            nullptr));
    }

    ctWmiClassObject(ctWmiService wbemServices, BSTR className) :
        m_wbemServices(std::move(wbemServices))
    {
        THROW_IF_FAILED(m_wbemServices->GetObjectW(
            className,
            0,
            nullptr,
            m_wbemClassObject.put(),
            nullptr));
    }

    [[nodiscard]] wil::com_ptr<IWbemClassObject> get_class_object() const noexcept
    {
        return m_wbemClassObject;
    }

    [[nodiscard]] property_iterator property_begin(bool fNonSystemPropertiesOnly = true) const
    {
        return {m_wbemClassObject, fNonSystemPropertiesOnly};
    }

    [[nodiscard]] static property_iterator property_end() noexcept
    {
        return {};
    }

    //
    // Not yet implemented
    //
    // method_iterator method_begin(bool _fLocalMethodsOnly = true)
    // {
    //     return method_iterator(wbemClass, _fLocalMethodsOnly);
    // }
    // method_iterator method_end() noexcept
    // {
    //     return method_iterator();
    // }

    // A forward property_iterator class type to enable forward-traversing instances of the queried WMI provider
    class property_iterator
    {
    private:
        static constexpr uint32_t c_endIteratorIndex = ULONG_MAX;

        wil::com_ptr<IWbemClassObject> m_wbemClassObj;
        wil::shared_bstr m_propertyName;
        CIMTYPE m_propertyType = 0;
        uint32_t m_index = c_endIteratorIndex;

    public:
        property_iterator() = default;

        property_iterator(wil::com_ptr<IWbemClassObject> classObj, bool fNonSystemPropertiesOnly) :
            m_wbemClassObj(std::move(classObj)),
            m_index(0)
        {
            THROW_IF_FAILED(m_wbemClassObj->BeginEnumeration(fNonSystemPropertiesOnly ? WBEM_FLAG_NONSYSTEM_ONLY : 0));
            increment();
        }

        ~property_iterator() noexcept = default;
        property_iterator(const property_iterator&) = default;
        property_iterator& operator =(const property_iterator&) = default;
        property_iterator(property_iterator&&) = default;
        property_iterator& operator=(property_iterator&&) = default;

        void swap(property_iterator& rhs) noexcept
        {
            using std::swap;
            swap(m_index, rhs.m_index);
            swap(m_wbemClassObj, rhs.m_wbemClassObj);
            swap(m_propertyName, rhs.m_propertyName);
            swap(m_propertyType, rhs.m_propertyType);
        }

        // accessors:
        // - dereference operators to access the property name
        // - explicit type() method to expose its CIM type
        BSTR operator*()
        {
            if (m_index == c_endIteratorIndex)
            {
                throw std::out_of_range("ctWmiClassObject::property_iterator::operator * - invalid subscript");
            }
            return m_propertyName.get();
        }

        BSTR operator*() const
        {
            if (m_index == c_endIteratorIndex)
            {
                throw std::out_of_range("ctWmiClassObject::property_iterator::operator * - invalid subscript");
            }
            return m_propertyName.get();
        }

        BSTR* operator->()
        {
            if (m_index == c_endIteratorIndex)
            {
                throw std::out_of_range("ctWmiClassObject::property_iterator::operator-> - invalid subscript");
            }
            return m_propertyName.addressof();
        }

        [[nodiscard]] CIMTYPE type() const
        {
            if (m_index == c_endIteratorIndex)
            {
                throw std::out_of_range("ctWmiClassObject::property_iterator::type - invalid subscript");
            }
            return m_propertyType;
        }

        bool operator==(const property_iterator& iter) const noexcept
        {
            if (m_index != c_endIteratorIndex)
            {
                return m_index == iter.m_index &&
                       m_wbemClassObj == iter.m_wbemClassObj;
            }
            return m_index == iter.m_index;
        }

        bool operator!=(const property_iterator& iter) const noexcept
        {
            return !(*this == iter);
        }

        // preincrement
        property_iterator& operator++()
        {
            increment();
            return *this;
        }

        // postincrement
        property_iterator operator++(int)
        {
            property_iterator temp(*this);
            increment();
            return temp;
        }

        // increment by integer
        property_iterator& operator+=(uint32_t inc)
        {
            for (auto loop = 0ul; loop < inc; ++loop)
            {
                increment();
                if (m_index == c_endIteratorIndex)
                {
                    throw std::out_of_range("ctWmiClassObject::property_iterator::operator+= - invalid subscript");
                }
            }
            return *this;
        }

        // property_iterator_traits
        // - allows <algorithm> functions to be used
        using iterator_category = std::forward_iterator_tag;
        using value_type = wil::shared_bstr;
        using difference_type = int;
        using pointer = BSTR;
        using reference = wil::shared_bstr&;

    private:
        void increment()
        {
            if (m_index == c_endIteratorIndex)
            {
                throw std::out_of_range("ctWmiClassObject::property_iterator - cannot increment: at the end");
            }

            CIMTYPE nextCimtype{};
            wil::shared_bstr nextName;
            switch (const auto hr = m_wbemClassObj->Next(
                0,
                nextName.put(),
                nullptr,
                &nextCimtype,
                nullptr))
            {
                case WBEM_S_NO_ERROR:
                {
                    // update the instance members
                    ++m_index;
                    using std::swap;
                    swap(m_propertyName, nextName);
                    swap(m_propertyType, nextCimtype);
                    break;
                }
                case WBEM_S_NO_MORE_DATA:
                {
                    // at the end...
                    m_index = c_endIteratorIndex;
                    m_propertyName.reset();
                    m_propertyType = 0;
                    break;
                }

                default:
                    THROW_HR(hr);
            }
        }
    };
};
} // namespace ctl
