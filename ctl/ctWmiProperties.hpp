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
#include <memory>
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
#include <wil/com.h>
#include <wil/resource.h>
// local headers
#include "ctWmiService.hpp"


namespace ctl
{
// class ctWmiProperties
//
// Exposes enumerating properties of a WMI Provider through an iterator interface.
class ctWmiProperties
{
private:
    ctWmiService m_wbemServices;
    wil::com_ptr<IWbemClassObject> m_wbemClass{};

public:
    class iterator;

    ctWmiProperties(ctWmiService service, wil::com_ptr<IWbemClassObject> classObject) noexcept :
        m_wbemServices(std::move(service)),
        m_wbemClass(std::move(classObject))
    {
    }

    ctWmiProperties(ctWmiService service, _In_ PCWSTR className) :
        m_wbemServices(std::move(service))
    {
        THROW_IF_FAILED(m_wbemServices->GetObjectW(
            wil::make_bstr(className).get(),
            0,
            nullptr,
            m_wbemClass.put(),
            nullptr));
    }

    ctWmiProperties(ctWmiService service, _In_ BSTR className) :
        m_wbemServices(std::move(service))
    {
        THROW_IF_FAILED(m_wbemServices->GetObjectW(
            className,
            0,
            nullptr,
            m_wbemClass.put(),
            nullptr));
    }

    [[nodiscard]] iterator begin(const bool nonSystemPropertiesOnly = true) const
    {
        return {m_wbemClass, nonSystemPropertiesOnly};
    }

    [[nodiscard]] static iterator end() noexcept
    {
        return {};
    }

    // A forward iterator to enable forward-traversing instances of the queried WMI provider
    
    class iterator
    {
        constexpr uint32_t m_endIteratorIndex = ULONG_MAX;

        wil::com_ptr<IWbemClassObject> m_wbemClassObject{};
        wil::shared_bstr m_propertyName{};
        CIMTYPE m_propertyType = 0;
        uint32_t m_index = m_endIteratorIndex;

    public:
        // Iterator requires the caller's IWbemServices interface and class name
        iterator() noexcept = default;

        iterator(wil::com_ptr<IWbemClassObject> classObject, bool nonSystemPropertiesOnly) :
            m_wbemClassObject(std::move(classObject)), m_index(0)
        {
            THROW_IF_FAILED(m_wbemClassObject->BeginEnumeration(nonSystemPropertiesOnly ? WBEM_FLAG_NONSYSTEM_ONLY : 0));
            increment();
        }

        ~iterator() noexcept = default;
        iterator(const iterator&) noexcept = default;
        iterator(iterator&&) noexcept = default;

        iterator& operator =(const iterator&) noexcept = delete;
        iterator& operator =(iterator&&) noexcept = delete;

        void swap(_Inout_ iterator& rhs) noexcept
        {
            using std::swap;
            swap(m_index, rhs.m_index);
            swap(m_wbemClassObject, rhs.m_wbemClassObject);
            swap(m_propertyName, rhs.m_propertyName);
            swap(m_propertyType, rhs.m_propertyType);
        }

        wil::shared_bstr operator*() const
        {
            if (m_index == m_endIteratorIndex)
            {
                throw std::out_of_range("ctWmiProperties::iterator::operator - invalid subscript");
            }
            return m_propertyName;
        }

        const wil::shared_bstr* operator->() const
        {
            if (m_index == m_endIteratorIndex)
            {
                throw std::out_of_range("ctWmiProperties::iterator::operator-> - invalid subscript");
            }
            return &m_propertyName;
        }

        [[nodiscard]] CIMTYPE type() const
        {
            if (m_index == m_endIteratorIndex)
            {
                throw std::out_of_range("ctWmiProperties::iterator::type - invalid subscript");
            }
            return m_propertyType;
        }

        bool operator==(const iterator& iter) const noexcept
        {
            if (m_index != m_endIteratorIndex)
            {
                return m_index == iter.m_index &&
                       m_wbemClassObject == iter.m_wbemClassObject;
            }
            return m_index == iter.m_index;
        }

        bool operator!=(const iterator& iter) const noexcept
        {
            return !(*this == iter);
        }

        // preincrement
        iterator& operator++()
        {
            increment();
            return *this;
        }

        // postincrement
        iterator operator++(int)
        {
            iterator temp(*this);
            increment();
            return temp;
        }

        // increment by integer
        iterator& operator+=(uint32_t _inc)
        {
            for (auto loop = 0ul; loop < _inc; ++loop)
            {
                increment();
                if (m_index == m_endIteratorIndex)
                {
                    throw std::out_of_range("ctWmiProperties::iterator::operator+= - invalid subscript");
                }
            }
            return *this;
        }

        // iterator_traits (allows <algorithm> functions to be used)
        using iterator_category = std::forward_iterator_tag;
        using value_type = wil::shared_bstr;
        using difference_type = int;
        using pointer = BSTR;
        using reference = wil::shared_bstr&;

    private:
        void increment()
        {
            if (m_index == m_endIteratorIndex)
            {
                throw std::out_of_range("ctWmiProperties::iterator - cannot increment: at the end");
            }

            CIMTYPE nextCimtype;
            wil::shared_bstr nextName;
            const auto hr = m_wbemClassObject->Next(
                0,
                nextName.addressof(),
                nullptr,
                &nextCimtype,
                nullptr);
            switch (hr)
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
                    m_index = m_endIteratorIndex;
                    m_propertyName.reset();
                    m_propertyType = 0;
                    break;
                }

                default: THROW_IF_FAILED(hr);
            }
        }
    };
};
} // namespace ctl
