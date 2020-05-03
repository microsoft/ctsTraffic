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
#include <wil/resource.h>

namespace ctsTraffic
{
    struct ctsSafeIntErrorPolicy
    {
        static __declspec(noreturn) void __stdcall SafeIntOnOverflow() noexcept
        {
            FAIL_FAST_MSG("SafeInt has detected an integer overflow");
        }
        static __declspec(noreturn) void __stdcall SafeIntOnDivZero() noexcept
        {
            FAIL_FAST_MSG("SafeInt has detected divide by zero");
        }
    };

    using ctsUnsignedLong = msl::utilities::SafeInt<unsigned long, ctsSafeIntErrorPolicy>;
    using ctsUnsignedLongLong = msl::utilities::SafeInt<unsigned long long, ctsSafeIntErrorPolicy>;
    using ctsSignedLong = msl::utilities::SafeInt<signed long, ctsSafeIntErrorPolicy>;
    using ctsSignedLongLong = msl::utilities::SafeInt<long long, ctsSafeIntErrorPolicy>;
    using ctsSizeT = msl::utilities::SafeInt<size_t, ctsSafeIntErrorPolicy>;

    using ctsSafeIntException = msl::utilities::SafeIntException;

    inline PCWSTR ctsPrintSafeIntException(const ctsSafeIntException& _ex) noexcept
    {
        switch (_ex.m_code)
        {
            case msl::utilities::SafeIntNoError: return L"SafeInt - No Error";
            case msl::utilities::SafeIntArithmeticOverflow: return L"SafeInt - Arithmetic Overflow";
            case msl::utilities::SafeIntDivideByZero: return L"SafeInt - Divide By Zero";
            default:
                return nullptr;
        }
    }

} // namespace
