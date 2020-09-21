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

namespace ctl
{
    ///
    /// ctTimer namespace contains useful functions when working with QPC/QPF
    ///
    namespace ctTimer
    {
        /// 
        /// Conversion functions between hundred-nanoseconds <-> milliseconds
        ///
        /// nano-second == 10 ^ -9
        /// - therefore 100 nano-seconds == 10 ^ -7
        /// millisecond == 10 ^ -3
        ///

        constexpr long long ctConvertMillisToHundredNs(long long milliseconds) noexcept
        {
            return static_cast<long long>(milliseconds * 10000LL);
        }

        constexpr long long ctConvertHundredNsToMillis(long long _hundred_nanoseconds) noexcept
        {
            return static_cast<long long>(_hundred_nanoseconds / 10000LL);
        }

        inline FILETIME ctConvertHundredNsToAbsoluteFiletime(long long hundred_nanoseconds) noexcept
        {
            ULARGE_INTEGER ulong_integer;
            ulong_integer.QuadPart = hundred_nanoseconds;

            FILETIME return_filetime;
            return_filetime.dwHighDateTime = ulong_integer.HighPart;
            return_filetime.dwLowDateTime = ulong_integer.LowPart;
            return return_filetime;
        }

        // Create a negative FILETIME, which for some timer APIs indicate a 'relative' time
        // - e.g. SetThreadpoolTimer, where a negative value indicates the amount of time to wait relative to the current time 
        inline FILETIME ctConvertHundredNsToRelativeFiletime(long long hundred_nanoseconds) noexcept
        {
            ULARGE_INTEGER ulong_integer;
            ulong_integer.QuadPart = static_cast<ULONGLONG>(-hundred_nanoseconds);

            FILETIME return_filetime;
            return_filetime.dwHighDateTime = ulong_integer.HighPart;
            return_filetime.dwLowDateTime = ulong_integer.LowPart;
            return return_filetime;
        }

        inline long long ctConvertFiletimeToHundredNs(const FILETIME& filetime) noexcept
        {
            ULARGE_INTEGER ulong_integer;
            ulong_integer.HighPart = filetime.dwHighDateTime;
            ulong_integer.LowPart = filetime.dwLowDateTime;

            return ulong_integer.QuadPart;
        }

        inline FILETIME ctConvertMillisToAbsoluteFiletime(long long milliseconds) noexcept
        {
            return ctConvertHundredNsToAbsoluteFiletime(ctConvertMillisToHundredNs(milliseconds));
        }

        // Create a negative FILETIME, which for some timer APIs indicate a 'relative' time
        // - e.g. SetThreadpoolTimer, where a negative value indicates the amount of time to wait relative to the current time 
        inline FILETIME ctConvertMillisToRelativeFiletime(long long milliseconds) noexcept
        {
            return ctConvertHundredNsToRelativeFiletime(ctConvertMillisToHundredNs(milliseconds));
        }

        inline long long ctConvertFiletimeToMillis(const FILETIME& filetime) noexcept
        {
            ULARGE_INTEGER ulong_integer;
            ulong_integer.HighPart = filetime.dwHighDateTime;
            ulong_integer.LowPart = filetime.dwLowDateTime;

            return ctConvertHundredNsToMillis(ulong_integer.QuadPart);
        }

        namespace Details
        {
            ///
            /// InitOnce the QPF value as it won't change after the OS has booted
            /// - hiding within an unnamed namesapce
            ///
            // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
            static INIT_ONCE g_QpfInitOnce = INIT_ONCE_STATIC_INIT;
            static LARGE_INTEGER g_Qpf;

            static BOOL CALLBACK QpfInitOnceCallback(_In_ PINIT_ONCE, _In_ PVOID, _In_ PVOID*) noexcept
            {
                QueryPerformanceFrequency(&g_Qpf);
                return TRUE;
            }
        }

        inline long long ctSnapQpf() noexcept
        {
            (void)InitOnceExecuteOnce(&Details::g_QpfInitOnce, Details::QpfInitOnceCallback, nullptr, nullptr);
            return Details::g_Qpf.QuadPart;
        }

#ifdef CTSTRAFFIC_UNIT_TESTS
        inline long long ctSnapQpcInMillis() noexcept;
#else
        inline long long ctSnapQpcInMillis() noexcept
        {
            (void)InitOnceExecuteOnce(&Details::g_QpfInitOnce, Details::QpfInitOnceCallback, nullptr, nullptr);
            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            // multiplying by 1000 as (qpc / qpf) == seconds
            return static_cast<long long>(qpc.QuadPart * 1000LL / Details::g_Qpf.QuadPart);
        }
#endif

        inline FILETIME ctSnapQpcAsFiletime() noexcept
        {
            return ctConvertHundredNsToAbsoluteFiletime(ctSnapQpcInMillis());
        }

        inline long long ctSnapSystemTimeInMillis() noexcept
        {
            FILETIME filetime;
            GetSystemTimeAsFileTime(&filetime);
            return ctConvertFiletimeToMillis(filetime);
        }
    } // namespace ctTimer
} // namespace ctl
