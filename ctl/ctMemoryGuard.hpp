/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
// ReSharper disable once CppUnusedIncludeDirective
#include <windows.h>

namespace ctl
{
    //////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Can concurrent-safely read from both const and non-const
    ///  long long * 
    ///  long *
    ///
    //////////////////////////////////////////////////////////////////////////////////////////
    inline long long ctMemoryGuardRead(const long long* original_value) noexcept
    {
        return ::InterlockedCompareExchange64(const_cast<long long*>(original_value), 0LL, 0LL);
    }

    inline long ctMemoryGuardRead(const long* original_value) noexcept
    {
        return ::InterlockedCompareExchange(const_cast<long*>(original_value), 0LL, 0LL);
    }

    inline long long ctMemoryGuardRead(_In_ long long* original_value) noexcept
    {
        return ::InterlockedCompareExchange64(original_value, 0LL, 0LL);
    }

    inline long ctMemoryGuardRead(_In_ long* original_value) noexcept
    {
        return ::InterlockedCompareExchange(original_value, 0LL, 0LL);
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Can concurrent-safely update a long long or long value
    /// - *Write returns the *prior* value
    /// - *WriteConditionally returns the *prior* value
    /// - *Add returns the *prior* value
    /// - *Subtract returns the *prior* value
    ///   (Note subtraction is just the adding a negative long value)
    ///
    /// - *Increment returns the *new* value
    /// - *Decrement returns the *new* value
    ///
    //////////////////////////////////////////////////////////////////////////////////////////
    inline long long ctMemoryGuardWrite(_Inout_ long long* original_value, long long new_value) noexcept
    {
        return ::InterlockedExchange64(original_value, new_value);
    }

    inline long ctMemoryGuardWrite(_Inout_ long* original_value, long new_value) noexcept
    {
        return ::InterlockedExchange(original_value, new_value);
    }

    inline long long ctMemoryGuardWriteConditionally(_Inout_ long long* original_value, long long new_value, long long if_equals) noexcept
    {
        return ::InterlockedCompareExchange64(original_value, new_value, if_equals);
    }

    inline long ctMemoryGuardWriteConditionally(_Inout_ long* original_value, long new_value, long if_equals) noexcept
    {
        return ::InterlockedCompareExchange(original_value, new_value, if_equals);
    }

    inline long long ctMemoryGuardAdd(_Inout_ long long* original_value, long long add_value) noexcept
    {
        return ::InterlockedExchangeAdd64(original_value, add_value);
    }

    inline long ctMemoryGuardAdd(_Inout_ long* original_value, long add_value) noexcept
    {
        return ::InterlockedExchangeAdd(original_value, add_value);
    }

    inline long long ctMemoryGuardSubtract(_Inout_ long long* original_value, long long subtract_value) noexcept
    {
        return ::InterlockedExchangeAdd64(original_value, subtract_value * -1LL);
    }

    inline long ctMemoryGuardSubtract(_Inout_ long* original_value, long subtract_value) noexcept
    {
        return ::InterlockedExchangeAdd(original_value, subtract_value * -1L);
    }

    inline long long ctMemoryGuardIncrement(_Inout_ long long* original_value) noexcept
    {
        return ::InterlockedIncrement64(original_value);
    }

    inline long ctMemoryGuardIncrement(_Inout_ long* original_value) noexcept
    {
        return ::InterlockedIncrement(original_value);
    }

    inline long long ctMemoryGuardDecrement(_Inout_ long long* original_value) noexcept
    {
        return ::InterlockedDecrement64(original_value);
    }

    inline long ctMemoryGuardDecrement(_Inout_ long* original_value) noexcept
    {
        return ::InterlockedDecrement(original_value);
    }
} // namespace
