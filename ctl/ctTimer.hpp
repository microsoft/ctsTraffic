/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/
// ReSharper disable CppInconsistentNaming
#pragma once

// os headers
#include <Windows.h>
#include <wil/win32_helpers.h>

// ctTimer namespace contains useful functions when working with QPC/QPF
namespace ctl {namespace ctTimer { namespace Details
        {
            ///
            /// InitOnce the QPF value as it won't change after the OS has booted
            /// - hiding within an unnamed namesapce
            ///
            // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
            static INIT_ONCE g_qpfInitOnce = INIT_ONCE_STATIC_INIT;
            static LARGE_INTEGER g_qpf;

            static BOOL CALLBACK QpfInitOnceCallback(_In_ PINIT_ONCE, _In_ PVOID, _In_ PVOID*) noexcept
            {
                QueryPerformanceFrequency(&g_qpf);
                return TRUE;
            }
        }

        // Create a negative FILETIME, which for some timer APIs indicate a 'relative' time
        // - e.g. SetThreadpoolTimer, where a negative value indicates the amount of time to wait relative to the current time 
        inline FILETIME convert_ms_to_relative_filetime(int64_t milliseconds) noexcept
        {
            return wil::filetime::from_int64(-1 * wil::filetime::convert_msec_to_100ns(milliseconds));
        }

        inline int64_t snap_qpf() noexcept
        {
            InitOnceExecuteOnce(&Details::g_qpfInitOnce, Details::QpfInitOnceCallback, nullptr, nullptr);
            return Details::g_qpf.QuadPart;
        }

#ifdef CTSTRAFFIC_UNIT_TESTS
        inline int64_t snap_qpc_as_msec() noexcept
        {
            return 0;
        }
#else
        inline int64_t snap_qpc_as_msec() noexcept
        {
            InitOnceExecuteOnce(&Details::g_qpfInitOnce, Details::QpfInitOnceCallback, nullptr, nullptr);
            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            // multiplying by 1000 as (qpc / qpf) == seconds
            return qpc.QuadPart * 1000LL / Details::g_qpf.QuadPart;
        }
#endif
    } // namespace ctTimer
} // namespace ctl
