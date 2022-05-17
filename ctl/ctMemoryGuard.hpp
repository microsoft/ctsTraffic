/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include <winnt.h>

namespace ctl
{
//////////////////////////////////////////////////////////////////////////////////////////
///
/// Can concurrent-safely read from both const and non-const
///  long long * 
///  long *
///
//////////////////////////////////////////////////////////////////////////////////////////
inline long long ctMemoryGuardRead(const long long* value) noexcept
{
    return ::InterlockedCompareExchange64(const_cast<long long*>(value), 0LL, 0LL);
}

inline long ctMemoryGuardRead(const long* value) noexcept
{
    return ::InterlockedCompareExchange(const_cast<long*>(value), 0LL, 0LL);
}

inline long long ctMemoryGuardRead(_In_ long long* value) noexcept
{
    return ::InterlockedCompareExchange64(value, 0LL, 0LL);
}

inline long ctMemoryGuardRead(_In_ long* value) noexcept
{
    return ::InterlockedCompareExchange(value, 0LL, 0LL);
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
inline long long ctMemoryGuardWrite(_Inout_ long long* value, long long newValue) noexcept
{
    return ::InterlockedExchange64(value, newValue);
}

inline long ctMemoryGuardWrite(_Inout_ long* value, long newValue) noexcept
{
    return ::InterlockedExchange(value, newValue);
}

inline long long ctMemoryGuardWriteConditionally(_Inout_ long long* value, long long newValue, long long ifEquals) noexcept
{
    return ::InterlockedCompareExchange64(value, newValue, ifEquals);
}

inline long ctMemoryGuardWriteConditionally(_Inout_ long* value, long newValue, long ifEquals) noexcept
{
    return ::InterlockedCompareExchange(value, newValue, ifEquals);
}

inline long long ctMemoryGuardAdd(_Inout_ long long* value, long long addValue) noexcept
{
    return ::InterlockedExchangeAdd64(value, addValue);
}

inline long ctMemoryGuardAdd(_Inout_ long* value, long addValue) noexcept
{
    return ::InterlockedExchangeAdd(value, addValue);
}

inline long long ctMemoryGuardSubtract(_Inout_ long long* value, long long subtractValue) noexcept
{
    return ::InterlockedExchangeAdd64(value, subtractValue * -1LL);
}

inline long ctMemoryGuardSubtract(_Inout_ long* value, long subtractValue) noexcept
{
    return ::InterlockedExchangeAdd(value, subtractValue * -1L);
}

inline long long ctMemoryGuardIncrement(_Inout_ long long* value) noexcept
{
    return ::InterlockedIncrement64(value);
}

inline long ctMemoryGuardIncrement(_Inout_ long* value) noexcept
{
    return ::InterlockedIncrement(value);
}

inline long long ctMemoryGuardDecrement(_Inout_ long long* value) noexcept
{
    return ::InterlockedDecrement64(value);
}

inline long ctMemoryGuardDecrement(_Inout_ long* value) noexcept
{
    return ::InterlockedDecrement(value);
}
} // namespace
