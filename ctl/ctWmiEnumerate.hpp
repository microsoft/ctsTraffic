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
#include <exception>
#include <stdexcept>
#include <memory>
// os headers
#include <windows.h>
#include <Wbemidl.h>
// local headers
#include "ctComInitialize.hpp"
#include "ctWmiService.hpp"
#include "ctWmiInstance.hpp"
#include "ctWmiException.hpp"
#include "ctVersionConversion.hpp"


namespace ctl
{

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// class ctWmiEnumerate
///
/// Exposes enumerating instances of a WMI Provider through an iterator interface.
///
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
class ctWmiEnumerate
{
public:
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// class ctWmiEnumerate::iterator
    ///
    /// A forward iterator class type to enable forward-traversing instances of the queried
    ///  WMI provider
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    class iterator
    {
    public:

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// c'tor and d'tor
        /// - default c'tor is an 'end' iterator
        /// - c'tor can take a reference to the parent's WMI Enum interface (to traverse)
        ///
        ////////////////////////////////////////////////////////////////////////////////
        explicit iterator(_In_ const ctWmiService& _services) NOEXCEPT : 
            index(END_ITERATOR_INDEX),
            wbemServices(_services),
            wbemEnumerator(), 
            ctInstance()
        {
        }
        iterator(_In_ const ctWmiService& _services, _In_ const ctComPtr<IEnumWbemClassObject>& _wbemEnumerator) : 
            index(0), 
            wbemServices(_services),
            wbemEnumerator(_wbemEnumerator), 
            ctInstance()
        {
            this->increment();
        }
        ~iterator() NOEXCEPT
        {
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// copy c'tor and copy assignment
        ///
        /// Note that both are no-fail/no-throw operations
        ///
        ////////////////////////////////////////////////////////////////////////////////
        iterator(_In_ const iterator& _i) NOEXCEPT : 
            index(_i.index),
            wbemServices(_i.wbemServices),
            wbemEnumerator(_i.wbemEnumerator),
            ctInstance(_i.ctInstance)
        {
        }

        iterator& operator =(_In_ const iterator& _i) NOEXCEPT
        {
            iterator copy(_i);
            this->swap(copy);
            return *this;
        }

        void swap(_Inout_ iterator& _i) NOEXCEPT
        {
            using std::swap;
            swap(this->index, _i.index);
            swap(this->wbemServices, _i.wbemServices);
            swap(this->wbemEnumerator, _i.wbemEnumerator);
            swap(this->ctInstance, _i.ctInstance);
        }

        unsigned long location() const NOEXCEPT
        {
            return this->index;
        }
        
        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// accessors:
        /// - dereference operators to access the internal WMI class object
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctWmiInstance& operator*() NOEXCEPT
        {
            return *this->ctInstance;
        }
        ctWmiInstance* operator->() NOEXCEPT
        {
            return this->ctInstance.get();
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// comparison and arithmatic operators
        /// 
        /// comparison operators are no-throw/no-fail
        /// arithmatic operators can fail 
        /// - throwing a ctWmiException object capturing the WMI failures
        ///
        ////////////////////////////////////////////////////////////////////////////////
        bool operator==(_In_ const iterator&) const NOEXCEPT;
        bool operator!=(_In_ const iterator&) const NOEXCEPT;

        iterator& operator++(); // preincrement
        iterator  operator++(_In_ int); // postincrement
        iterator& operator+=(_In_ DWORD); // increment by integer

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// iterator_traits
        /// - allows <algorithm> functions to be used
        ///
        ////////////////////////////////////////////////////////////////////////////////
        typedef std::forward_iterator_tag  iterator_category;
        typedef ctWmiInstance              value_type;
        typedef int                        difference_type;
        typedef ctWmiInstance*             pointer;
        typedef ctWmiInstance&             reference;


    private:
        void increment();

        static const unsigned long END_ITERATOR_INDEX = 0xffffffff;

        unsigned long index;
        ctWmiService wbemServices;
        ctComPtr<IEnumWbemClassObject> wbemEnumerator;
        std::shared_ptr<ctWmiInstance> ctInstance;
    };


public:
    ////////////////////////////////////////////////////////////////////////////////
    ///
    /// c'tor takes a reference to the initialized ctWmiService interface
    ///
    /// Default d'tor, copy c'tor, and copy assignment operators
    //
    ////////////////////////////////////////////////////////////////////////////////
    explicit ctWmiEnumerate(_In_ const ctWmiService& _wbemServices) NOEXCEPT :
        wbemServices(_wbemServices),
        wbemEnumerator()
    {
    }

    ////////////////////////////////////////////////////////////////////////////////
    ///
    /// query(LPCWSTR)
    ///
    /// Allows for executing a WMI query against the WMI service for an enumeration
    /// of WMI objects.
    /// Assumes the query of of the WQL query language.
    ///
    /// Will throw a ctWmiException if the WMI query fails
    /// Will throw a std::bad_alloc if fails to low-resources
    ///
    ////////////////////////////////////////////////////////////////////////////////
    void query(_In_ LPCWSTR _query)
    {
        ctComBstr wql(L"WQL");
        ctComBstr query(_query);

        HRESULT hr = this->wbemServices->ExecQuery(
            wql.get(), 
            query.get(),
            WBEM_FLAG_BIDIRECTIONAL, 
            nullptr,
            this->wbemEnumerator.get_addr_of());
        if (FAILED(hr)) {
            throw ctWmiException(hr, L"IWbemServices::ExecQuery", L"ctWmiEnumerate::query", false);
        }
    }
    void query(_In_ LPCWSTR _query, _In_ const ctComPtr<IWbemContext>& _context)
    {
        ctComBstr wql(L"WQL");
        ctComBstr query(_query);

        // forced to const-cast to make this const-correct as COM does not have
        //   an expression for saying a method call is const
        HRESULT hr = this->wbemServices->ExecQuery(
            wql.get(), 
            query.get(),
            WBEM_FLAG_BIDIRECTIONAL, 
            const_cast<IWbemContext*>(_context.get()),
            this->wbemEnumerator.get_addr_of());
        if (FAILED(hr)) {
            throw ctWmiException(hr, L"IWbemServices::ExecQuery", L"ctWmiEnumerate::query", false);
        }
    }


    ////////////////////////////////////////////////////////////////////////////////
    ///
    /// access methods to the child iterator class
    ///
    /// begin() - iterator pointing to the first of the contained enumerator
    /// end()   - defined end-iterator for comparison
    ///
    /// cbegin() / cend() - const versions of the above
    ///
    /// Iterator construction can fail - will throw a ctWmiException
    ///
    ////////////////////////////////////////////////////////////////////////////////
    iterator begin() const
    {
        if (NULL == this->wbemEnumerator.get()) {
            return end();
        }

        HRESULT hr = this->wbemEnumerator->Reset();
        if (FAILED(hr)) {
            throw ctWmiException(hr, L"IEnumWbemClassObject::Reset", L"ctWmiEnumerate::begin", false);
        }
        
        return iterator(this->wbemServices, this->wbemEnumerator);
    }
    iterator end() const NOEXCEPT
    {
        return iterator(this->wbemServices);
    }
    const iterator cbegin() const
    {
        if (NULL == this->wbemEnumerator.get()) {
            return cend();
        }

        HRESULT hr = this->wbemEnumerator->Reset();
        if (FAILED(hr)) {
            throw ctWmiException(hr, L"IEnumWbemClassObject::Reset", L"ctWmiEnumerate::cbegin", false);
        }

        return iterator(this->wbemServices, this->wbemEnumerator);
    }
    const iterator cend() const NOEXCEPT
    {
        return iterator(this->wbemServices);
    }

private:
    ctWmiService wbemServices;
    //
    // Marking wbemEnumerator mutabale to allow for const correctness of begin() and end()
    //   specifically, invoking Reset() is an implementation detail and should not affect external contracts
    //
    mutable ctComPtr<IEnumWbemClassObject> wbemEnumerator;
};


////////////////////////////////////////////////////////////////////////////////
///
/// Definitions of iterator comparison operators and arithmatic operators
///
////////////////////////////////////////////////////////////////////////////////
inline
bool ctWmiEnumerate::iterator::operator==(_In_ const iterator& _iter) const NOEXCEPT
{
    if (this->index != this->END_ITERATOR_INDEX) {
        return ( (this->index == _iter.index) && 
                 (this->wbemServices == _iter.wbemServices) &&
                 (this->wbemEnumerator == _iter.wbemEnumerator) &&
                 (this->ctInstance == _iter.ctInstance) );
    } else {
        return ( (this->index == _iter.index) && 
                 (this->wbemServices == _iter.wbemServices) );
    }
}
inline
bool ctWmiEnumerate::iterator::operator!=(_In_ const iterator& _iter) const NOEXCEPT
{
    return !(*this == _iter);
}
// preincrement
inline
ctWmiEnumerate::iterator& ctWmiEnumerate::iterator::operator++()
{
    this->increment();
    return *this;
}
// postincrement
inline
ctWmiEnumerate::iterator  ctWmiEnumerate::iterator::operator++(_In_ int)
{
    iterator temp (*this);
    this->increment();
    return temp;
}
// increment by integer
inline
ctWmiEnumerate::iterator& ctWmiEnumerate::iterator::operator+=(_In_ DWORD _inc)
{
    for (unsigned loop = 0; loop < _inc; ++loop) {
        this->increment();
        if (this->index == END_ITERATOR_INDEX) {
            throw std::out_of_range("ctWmiEnumerate::iterator::operator+= - invalid subscript");
        }
    }
    return *this;
}
inline
void ctWmiEnumerate::iterator::increment()
{
    if (index == END_ITERATOR_INDEX) {
        throw std::out_of_range("ctWmiEnumerate::iterator::increment at the end");
    }

    ULONG uReturn;
    ctComPtr<IWbemClassObject> wbemTarget;
    HRESULT hr = this->wbemEnumerator->Next(
        WBEM_INFINITE,
        1,
        wbemTarget.get_addr_of(),
        &uReturn);
    if (FAILED(hr)) {
        throw ctWmiException(hr, L"IEnumWbemClassObject::Next", L"ctWmiEnumerate::iterator::increment", false);
    }

    if (0 == uReturn) {
        // at the end...
        this->index = END_ITERATOR_INDEX;
        this->ctInstance.reset();
    } else {
        ++this->index;
        this->ctInstance = std::make_shared<ctWmiInstance>(this->wbemServices, wbemTarget);
    }
}

} // namespace ctl
