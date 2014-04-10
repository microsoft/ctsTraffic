/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// CPP headers
#include <new>
#include <utility>

namespace ctl {

    ///////////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //  template <typename T, T tNullValue, typename Fn> class ctScopedT
    //
    //  - A template class which implements a "smart" resource class
    //    the resource of type T
    //
    //  - T tNullValue is a value of type T defining a known constant value that 
    //    does not need to be released
    //
    //  - typename Fn should reference a functor which implements:
    //            void operator()(T&) throw()'
    //    - should free the resource type T
    //
    //  All methods are specified throw() - none can throw
    //
    //  This class does not allow copy assignment or construction by design, but does
    //  allow move assignment and construction
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename T, T tNullValue, typename Fn>
    class ctScopedT {
    public:
        ///////////////////////////////////////////////////////////////
        // constructors
        // - take a reference to T to take ownership
        // - optionally take a deleter Functor instance
        // - default constructor initializes with tNullValue
        ///////////////////////////////////////////////////////////////
        ctScopedT() throw() : closeFunctor(), tValue(tNullValue)
        {
        }
        // non-explicit by design
        ctScopedT(T const& t) throw() : closeFunctor(), tValue(t)
        {
        }
        ctScopedT(T const& t, Fn const& f) throw() : closeFunctor(f), tValue(t)
        {
        }
        // allowing move construction (not copy construction)
        ctScopedT(ctScopedT&& other) throw()
            : closeFunctor(std::move(other.closeFunctor)),
            tValue(std::move(other.tValue))
        {
            // Stop the tValue from being destroyed as soon as other leaves scope
            other.tValue = tNullValue;
        }
        ctScopedT& operator=(ctScopedT&& other) throw()
        {
            ctScopedT(std::move(other)).swap(*this);
            return *this;
        }

        // default destructor (no-fail)
        ~ctScopedT() throw()
        {
            this->reset();
        }
        // getter
        const T& get() const throw()
        {
            return this->tValue;
        }
        // function to free internal resources
        T release() throw()
        {
            T value = this->tValue;
            this->tValue = tNullValue;
            return value;
        }
        void reset(T const& newValue = tNullValue) throw()
        {
            closeFunctor(this->tValue);
            this->tValue = newValue;
        }
        // implementation of swap()
        void swap(ctScopedT& tShared) throw()
        {
            using std::swap;
            swap(this->closeFunctor, tShared.closeFunctor);
            swap(this->tValue, tShared.tValue);
        }

    private:
        // Not allowing copying 
        ctScopedT(ctScopedT const&);
        ctScopedT& operator=(ctScopedT const&);

        Fn closeFunctor;
        T tValue;
    }; // class ctScopedT

    ///////////////////////////////////////////////////////////////////
    // The non-member version of swap() within the ctl namespace
    //
    // no-throw guarantee
    ///////////////////////////////////////////////////////////////////
    template <typename T, T tNullValue, typename Fn>
    void swap(ctScopedT<T, tNullValue, Fn>& a, ctScopedT<T, tNullValue, Fn>& b) throw()
    {
        a.swap(b);
    }

    ///////////////////////////////////////////////////////////////////
    // The non-member comparison operators within the ctl namespace
    //
    // no-throw guarantee
    ///////////////////////////////////////////////////////////////////
    template <typename T, T tNullValue, typename FnT, typename A, A aNullValue, typename FnA>
    bool operator==(ctScopedT<T, tNullValue, FnT> const& lhs, ctScopedT<A, aNullValue, FnA> const& rhs) throw()
    {
        return (lhs.get() == rhs.get());
    }
    template <typename T, T tNullValue, typename FnT, typename A, A aNullValue, typename FnA>
    bool operator!=(ctScopedT<T, tNullValue, FnT> const& lhs, ctScopedT<A, aNullValue, FnA> const& rhs) throw()
    {
        return (lhs.get() != rhs.get());
    }

}  // namespace ctl
