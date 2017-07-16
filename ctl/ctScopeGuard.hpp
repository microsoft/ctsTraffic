/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

//
// A modern scope guard originally developed by Stephan T. Lavavej, Visual C++ Libraries Developer 
// - a couple of interface changes later made for this project
//
// This template class facilitates building exception safe code by capturing state-restoration in a stack object
// - which will be guaranteed to be run at scope exit (either by the code block executing through the end or by exception)
//
// Example with calling member functions of instantiated objects (imagine friends is an stl container).
// - Notice that in this example, we are reverting the vector to the prior state only if AddFriend fails.
//
//  void User::AddFriend(User& newFriend)
//  {
//      friends.push_back(newFriend);
// 
//      auto popOnError( [&friends] () {friends.pop_back()}; );
//      ctl::ctScopeGuard<decltype(popOnError)> guard(popOnError);
//
//      // imagine some database object instance 'db'
//      db->AddFriend(newFriend);
//      guard.dismiss();
//  }
//

// cpp headers
#include <memory> // for std::addressof()
// ctl headers
#include "ctVersionConversion.hpp"

namespace ctl
{

    template <typename F> class ctScopeGuardT
    {
    public:
        explicit ctScopeGuardT(F& f) NOEXCEPT :
            m_p(std::addressof(f))
        {
        }

        void run_once() NOEXCEPT
        {
            if (m_p) { (*m_p)(); }
            m_p = nullptr;
        }

        void dismiss() NOEXCEPT
        {
            m_p = nullptr;
        }

        ~ctScopeGuardT() NOEXCEPT
        {
            if (m_p) { (*m_p)(); }
        }

        explicit ctScopeGuardT(F&&) = delete;
        ctScopeGuardT(const ctScopeGuardT&) = delete;
        ctScopeGuardT& operator=(const ctScopeGuardT&) = delete;

    private:
        F * m_p;
    };

#define ctlScopeGuard(NAME, BODY)  \
    auto xx##NAME##xx = [&]() BODY; \
    ::ctl::ctScopeGuardT<decltype(xx##NAME##xx)> NAME(xx##NAME##xx)

} // namespace
