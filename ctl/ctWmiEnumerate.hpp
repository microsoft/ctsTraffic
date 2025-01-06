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
#include <memory>
#include <utility>
// os headers
#include <Windows.h>
#include <WbemIdl.h>
// local headers
#include "ctWmiService.hpp"
#include "ctWmiInstance.hpp"
// wil headers
#include <wil/com.h>

namespace ctl
{
// Exposes enumerating instances of a WMI Provider through an iterator interface.
class ctWmiEnumerate
{
public:
    // A forward iterator class type to enable forward-traversing instances of the queried WMI provider
    // 
    class iterator
    {
    public:
        explicit iterator(ctWmiService service) noexcept :
            m_wbemServices(std::move(service))
        {
        }

        iterator(ctWmiService service, wil::com_ptr<IEnumWbemClassObject> wbemEnumerator) :
            m_index(0),
            m_wbemServices(std::move(service)),
            m_wbemEnumerator(std::move(wbemEnumerator))
        {
            increment();
        }

        ~iterator() noexcept = default;
        iterator(const iterator&) noexcept = default;
        iterator& operator =(const iterator&) noexcept = default;
        iterator(iterator&&) noexcept = default;
        iterator& operator =(iterator&&) noexcept = default;

        void swap(_Inout_ iterator& rhs) noexcept
        {
            using std::swap;
            swap(m_index, rhs.m_index);
            swap(m_wbemServices, rhs.m_wbemServices);
            swap(m_wbemEnumerator, rhs.m_wbemEnumerator);
            swap(m_wmiInstance, rhs.m_wmiInstance);
        }

        [[nodiscard]] uint32_t location() const noexcept
        {
            return m_index;
        }

        ctWmiInstance& operator*() const noexcept
        {
            return *m_wmiInstance;
        }

        ctWmiInstance* operator->() const noexcept
        {
            return m_wmiInstance.get();
        }

        bool operator==(const iterator&) const noexcept;
        bool operator!=(const iterator&) const noexcept;

        iterator& operator++(); // pre-increment
        iterator operator++(int); // post-increment
        iterator& operator+=(uint32_t); // increment by integer

        // iterator_traits
        // - allows <algorithm> functions to be used
        using iterator_category = std::forward_iterator_tag;
        using value_type = ctWmiInstance;
        using difference_type = int;
        using pointer = ctWmiInstance*;
        using reference = ctWmiInstance&;

    private:
        void increment();

        static constexpr uint32_t c_endIteratorIndex = ULONG_MAX;
        uint32_t m_index = c_endIteratorIndex;
        ctWmiService m_wbemServices;
        wil::com_ptr<IEnumWbemClassObject> m_wbemEnumerator;
        std::shared_ptr<ctWmiInstance> m_wmiInstance;
    };


    explicit ctWmiEnumerate(ctWmiService wbemServices) noexcept :
        m_wbemServices(std::move(wbemServices))
    {
    }

    // Allows for executing a WMI query against the WMI service for an enumeration of WMI objects.
    // Assumes the query of the WQL query language.
    const ctWmiEnumerate& query(_In_ PCWSTR query)
    {
        THROW_IF_FAILED(m_wbemServices->ExecQuery(
            wil::make_bstr(L"WQL").get(),
            wil::make_bstr(query).get(),
            WBEM_FLAG_BIDIRECTIONAL,
            nullptr,
            m_wbemEnumerator.put()));
        return *this;
    }

    const ctWmiEnumerate& query(_In_ PCWSTR query, const wil::com_ptr<IWbemContext>& context)
    {
        THROW_IF_FAILED(m_wbemServices->ExecQuery(
            wil::make_bstr(L"WQL").get(),
            wil::make_bstr(query).get(),
            WBEM_FLAG_BIDIRECTIONAL,
            context.get(),
            m_wbemEnumerator.put()));
        return *this;
    }

    iterator begin() const
    {
        if (nullptr == m_wbemEnumerator.get())
        {
            return end();
        }
        THROW_IF_FAILED(m_wbemEnumerator->Reset());
        return {m_wbemServices, m_wbemEnumerator};
    }

    iterator end() const noexcept
    {
        return iterator(m_wbemServices);
    }

    iterator cbegin() const
    {
        if (nullptr == m_wbemEnumerator.get())
        {
            return cend();
        }
        THROW_IF_FAILED(m_wbemEnumerator->Reset());
        return {m_wbemServices, m_wbemEnumerator};
    }

    iterator cend() const noexcept
    {
        return iterator(m_wbemServices);
    }

private:
    ctWmiService m_wbemServices;
    // Marking wbemEnumerator mutable to allow for const correctness of begin() and end()
    // specifically, invoking Reset() is an implementation detail and should not affect external contracts
    mutable wil::com_ptr<IEnumWbemClassObject> m_wbemEnumerator;
};

inline bool ctWmiEnumerate::iterator::operator==(const iterator& iter) const noexcept
{
    if (m_index != c_endIteratorIndex)
    {
        return m_index == iter.m_index &&
               m_wbemServices == iter.m_wbemServices &&
               m_wbemEnumerator == iter.m_wbemEnumerator &&
               m_wmiInstance == iter.m_wmiInstance;
    }
    return m_index == iter.m_index &&
           m_wbemServices == iter.m_wbemServices;
}

inline bool ctWmiEnumerate::iterator::operator!=(const iterator& iter) const noexcept
{
    return !(*this == iter);
}

inline ctWmiEnumerate::iterator& ctWmiEnumerate::iterator::operator++()
{
    increment();
    return *this;
}

inline ctWmiEnumerate::iterator ctWmiEnumerate::iterator::operator++(int)
{
    auto temp(*this);
    increment();
    return temp;
}

inline ctWmiEnumerate::iterator& ctWmiEnumerate::iterator::operator+=(uint32_t inc)
{
    for (auto loop = 0ul; loop < inc; ++loop)
    {
        increment();
        if (m_index == c_endIteratorIndex)
        {
            throw std::out_of_range("ctWmiEnumerate::iterator::operator+= - invalid subscript");
        }
    }
    return *this;
}

inline void ctWmiEnumerate::iterator::increment()
{
    if (m_index == c_endIteratorIndex)
    {
        throw std::out_of_range("ctWmiEnumerate::iterator::increment at the end");
    }

    ULONG uReturn;
    wil::com_ptr<IWbemClassObject> wbemTarget;
    THROW_IF_FAILED(m_wbemEnumerator->Next(
        WBEM_INFINITE,
        1,
        wbemTarget.put(),
        &uReturn));

    if (0 == uReturn)
    {
        // at the end...
        m_index = c_endIteratorIndex;
        m_wmiInstance.reset();
    }
    else
    {
        ++m_index;
        m_wmiInstance = std::make_shared<ctWmiInstance>(m_wbemServices, wbemTarget);
    }
}
} // namespace ctl
