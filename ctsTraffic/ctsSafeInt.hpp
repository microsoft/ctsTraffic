/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once
// os headers
#include <Windows.h>
#include <safeint.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctException.hpp>


namespace ctsTraffic {
    ///
    /// 'SafeInt' support differs across toolsets
    /// - using typedefs to differentiate between the various options in one place
    ///
    struct ctsSafeIntErrorPolicy {
        static __declspec(noreturn) void __stdcall SafeIntOnOverflow() NOEXCEPT
        {
            ctl::ctAlwaysFatalCondition(L"SafeInt has detected an integer overflow");
        }
        static __declspec(noreturn) void __stdcall SafeIntOnDivZero() NOEXCEPT
        {
            ctl::ctAlwaysFatalCondition(L"SafeInt has detected divide by zero");
        }
    };
    /// Visual Studio build system using safeint.h
    typedef msl::utilities::SafeInt<unsigned long, ctsSafeIntErrorPolicy>       ctsUnsignedLong;
    typedef msl::utilities::SafeInt<unsigned long long, ctsSafeIntErrorPolicy>  ctsUnsignedLongLong;
    typedef msl::utilities::SafeInt<signed long, ctsSafeIntErrorPolicy>         ctsSignedLong;
    typedef msl::utilities::SafeInt<long long, ctsSafeIntErrorPolicy>           ctsSignedLongLong;
    typedef msl::utilities::SafeInt<size_t, ctsSafeIntErrorPolicy>              ctsSizeT;

    typedef msl::utilities::SafeIntException ctsSafeIntException;

    inline
    LPCWSTR ctsPrintSafeIntException(const ctsSafeIntException& _ex) NOEXCEPT
    {
        switch (_ex.m_code) {
            case msl::utilities::SafeIntNoError: return L"SafeInt - No Error";
            case msl::utilities::SafeIntArithmeticOverflow: return L"SafeInt - Arithmetic Overflow";
            case msl::utilities::SafeIntDivideByZero: return L"SafeInt - Divide By Zero";
            default:
                return nullptr;
        }
    }

} // namespace
