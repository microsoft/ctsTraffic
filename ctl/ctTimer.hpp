/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include <windows.h>

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

		///
		/// convert_msec_hundredNs
		/// : converting milliseconds to one-hundred-nano-seconds
		/// (FILETIME records time in one-hundred-nano-seconds)
		///
		constexpr
		long long convert_msec_hundredNs(long long _milliseconds) noexcept
		{
			return static_cast<long long>(_milliseconds * 10000LL);
		}

		///
		/// convert_hundredNs_msec
		/// : converting one-hundred-nano-seconds to milliseconds
		/// (FILETIME records time in one-hundred-nano-seconds)
		///
		constexpr
		long long convert_hundredNs_msec(long long _hundred_nanoseconds) noexcept
		{
			return static_cast<long long>(_hundred_nanoseconds / 10000LL);
		}

		///
		/// convert_hundredNs_filetime
		/// : converting one-hundred-nano-seconds to FILETIME
		/// (FILETIME records time in one-hundred-nano-seconds)
		///
		inline
		FILETIME convert_hundredNs_absolute_filetime(long long _hundred_nanoseconds) noexcept
		{
			ULARGE_INTEGER ulong_integer;
			ulong_integer.QuadPart = _hundred_nanoseconds;

			FILETIME return_filetime;
			return_filetime.dwHighDateTime = ulong_integer.HighPart;
			return_filetime.dwLowDateTime = ulong_integer.LowPart;
			return return_filetime;
		}

		///
		/// Create a negative FILETIME, which for some timer APIs indicate a 'relative' time
		/// - e.g. SetThreadpoolTimer, where a negative value indicates the amount of time to wait relative to the current time 
		///
		inline
		FILETIME convert_hundredNs_relative_filetime(long long _hundred_nanoseconds) noexcept
		{
			ULARGE_INTEGER ulong_integer;
			ulong_integer.QuadPart = static_cast<ULONGLONG>(-_hundred_nanoseconds);

			FILETIME return_filetime;
			return_filetime.dwHighDateTime = ulong_integer.HighPart;
			return_filetime.dwLowDateTime = ulong_integer.LowPart;
			return return_filetime;
		}

		///
		/// convert_hundredNs_filetime
		/// : converting one-hundred-nano-seconds to FILETIME
		/// (FILETIME records time in one-hundred-nano-seconds)
		///
		inline
		long long convert_filetime_hundredNs(const FILETIME& _filetime) noexcept
		{
			ULARGE_INTEGER ulong_integer;
			ulong_integer.HighPart = _filetime.dwHighDateTime;
			ulong_integer.LowPart = _filetime.dwLowDateTime;

			return ulong_integer.QuadPart;
		}

		///
		/// convert_msec_filetime
		/// : converting milliseconds to FILETIME
		/// (FILETIME records time in one-hundred-nano-seconds)
		///
		inline
		FILETIME convert_msec_absolute_filetime(long long _milliseconds) noexcept
		{
			return convert_hundredNs_absolute_filetime(convert_msec_hundredNs(_milliseconds));
		}

		inline
		FILETIME convert_msec_relative_filetime(long long _milliseconds) noexcept
		{
			return convert_hundredNs_relative_filetime(convert_msec_hundredNs(_milliseconds));
		}

		///
		/// convert_filetime_msec
		/// : converting FILETIME to milliseconds
		/// (FILETIME records time in one-hundred-nano-seconds)
		///
		inline
		long long convert_filetime_msec(const FILETIME& _filetime) noexcept
		{
			ULARGE_INTEGER ulong_integer;
			ulong_integer.HighPart = _filetime.dwHighDateTime;
			ulong_integer.LowPart = _filetime.dwLowDateTime;

			return convert_hundredNs_msec(ulong_integer.QuadPart);
		}

		namespace details
		{
			///
			/// InitOnce the QPF value as it won't change after the OS has booted
			/// - hiding within an unnamed namesapce
			///
			// ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
			static INIT_ONCE s_QpfInitOnce = INIT_ONCE_STATIC_INIT;
			static LARGE_INTEGER s_Qpf;

			static BOOL CALLBACK s_QpfInitOnceCallback(_In_ PINIT_ONCE, _In_ PVOID, _In_ PVOID*) noexcept
			{
				::QueryPerformanceFrequency(&s_Qpf);
				return TRUE;
			}
		}

		inline
		long long snap_qpf() noexcept
		{
			(void)::InitOnceExecuteOnce(&details::s_QpfInitOnce, details::s_QpfInitOnceCallback, nullptr, nullptr);
			return details::s_Qpf.QuadPart;
		}

		///
		/// Returns the current 'time' from QPC/QPF in terms of milliseconds
		/// - leaving undefined for unit tests which need to control 'time' for their tests
		///
#ifdef CTSTRAFFIC_UNIT_TESTS
        inline
        long long snap_qpc_as_msec() noexcept;
#else
		inline
		long long snap_qpc_as_msec() noexcept
		{
			(void)::InitOnceExecuteOnce(&details::s_QpfInitOnce, details::s_QpfInitOnceCallback, nullptr, nullptr);
			LARGE_INTEGER qpc;
			::QueryPerformanceCounter(&qpc);
			// multiplying by 1000 as (qpc / qpf) == seconds
			return static_cast<long long>(qpc.QuadPart * 1000LL / details::s_Qpf.QuadPart);
		}
#endif
		///
		/// Returns the current 'time' from QPC/QPF as a FILETIME
		/// (FILETIME records time in one-hundred-nano-seconds)
		///
		inline
		FILETIME snap_qpc_as_filetime() noexcept
		{
			return convert_hundredNs_absolute_filetime(snap_qpc_as_msec());
		}

		///
		/// Returns the current 'time' from QPC/QPF as a FILETIME
		/// (FILETIME records time in one-hundred-nano-seconds)
		///
		inline
		FILETIME snap_system_time_as_filetime() noexcept
		{
			FILETIME return_filetime;
			::GetSystemTimeAsFileTime(&return_filetime);
			return return_filetime;
		}

		///
		/// Returns the current 'time' from GetSystemTimeAsFiletime in terms of milliseconds
		///
		inline
		long long snap_system_time_as_msec() noexcept
		{
			return convert_filetime_msec(snap_system_time_as_filetime());
		}
	} // namespace ctTimer
} // namespace ctl
