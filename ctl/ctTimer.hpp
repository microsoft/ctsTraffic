#pragma once

#include <windows.h>



namespace ctl {

    ///
    /// ctTimer namespace contains useful functions when working with QPC/QPF
    ///
    namespace ctTimer {
        /// 
        /// Conversion functions between hundred-nanoseconds <-> milliseconds
        ///
        /// nano-second == 10 ^ -9
        /// - therefore 100 nano-seconds is 10 ^ -7
        /// millisecond == 10 ^ -3
        ///

        ///
        /// convert_msec_hundredNs
        /// : converting milliseconds to one-hundred-nano-seconds
        /// (FILETIME records time in one-hundred-nano-seconds)
        ///
        inline
        long long convert_msec_hundredNs(long long _milliseconds) throw()
        {
            return static_cast<long long>(_milliseconds * 10000LL);
        }
        ///
        /// convert_hundredNs_msec
        /// : converting one-hundred-nano-seconds to milliseconds
        /// (FILETIME records time in one-hundred-nano-seconds)
        ///
        inline
        long long convert_hundredNs_msec(long long _hundred_nanoseconds) throw()
        {
            return static_cast<long long>(_hundred_nanoseconds / 10000LL);
        }
        ///
        /// convert_hundredNs_filetime
        /// : converting one-hundred-nano-seconds to FILETIME
        /// (FILETIME records time in one-hundred-nano-seconds)
        ///
        inline
        FILETIME convert_hundredNs_absolute_filetime(long long _hundred_nanoseconds) throw()
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
        FILETIME convert_hundredNs_relative_filetime(long long _hundred_nanoseconds) throw()
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
        long long convert_filetime_hundredNs(const FILETIME& _filetime) throw()
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
        FILETIME convert_msec_absolute_filetime(long long _milliseconds) throw()
        {
            return convert_hundredNs_absolute_filetime(convert_msec_hundredNs(_milliseconds));
        }
        inline
        FILETIME convert_msec_relative_filetime(long long _milliseconds) throw()
        {
            return convert_hundredNs_relative_filetime(convert_msec_hundredNs(_milliseconds));
        }
        ///
        /// convert_filetime_msec
        /// : converting FILETIME to milliseconds
        /// (FILETIME records time in one-hundred-nano-seconds)
        ///
        inline
        long long convert_filetime_msec(const FILETIME& _filetime) throw()
        {
            ULARGE_INTEGER ulong_integer;
            ulong_integer.HighPart = _filetime.dwHighDateTime;
            ulong_integer.LowPart = _filetime.dwLowDateTime;

            return convert_hundredNs_msec(ulong_integer.QuadPart);
        }

        namespace {
            ///
            /// InitOnce the QPF value as it won't change after the OS has booted
            /// - hiding within an unnamed namesapce
            ///
            static INIT_ONCE s_QpfInitOnce = INIT_ONCE_STATIC_INIT;
            static LARGE_INTEGER s_Qpf;
            static BOOL CALLBACK s_QpfInitOnceCallback(_In_ PINIT_ONCE, _In_ PVOID, _In_ PVOID*)
            {
                QueryPerformanceFrequency(&s_Qpf);
                return TRUE;
            }
        }

        inline
        long long snap_qpf() throw()
        {
            (void) ::InitOnceExecuteOnce(&s_QpfInitOnce, s_QpfInitOnceCallback, nullptr, nullptr);
            return s_Qpf.QuadPart;
        }

        ///
        /// Returns the current 'time' from QPC/QPF in terms of milliseconds
        ///
        inline
        long long snap_qpc_msec() throw()
        {
            (void) ::InitOnceExecuteOnce(&s_QpfInitOnce, s_QpfInitOnceCallback, nullptr, nullptr);
            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            // multiplying by 1000 as (qpc / qpf) == seconds
            return static_cast<long long>((qpc.QuadPart * 1000LL) / s_Qpf.QuadPart);
        }
        ///
        /// Returns the current 'time' from QPC/QPF as a FILETIME
        /// (FILETIME records time in one-hundred-nano-seconds)
        ///
        inline
        FILETIME snap_qpc_filetime() throw()
        {
            return convert_hundredNs_absolute_filetime(snap_qpc_msec());
        }

        ///
        /// Returns the current 'time' from QPC/QPF as a FILETIME
        /// (FILETIME records time in one-hundred-nano-seconds)
        ///
        inline
        FILETIME snap_system_time_filetime() throw()
        {
            FILETIME return_filetime;
            ::GetSystemTimeAsFileTime(&return_filetime);
            return return_filetime;
        }
        ///
        /// Returns the current 'time' from GetSystemTimeAsFiletime in terms of milliseconds
        ///
        inline
        long long snap_system_time_msec() throw()
        {
            return convert_filetime_msec(snap_system_time_filetime());
        }

    } // namespace ctTimer

} // namespace ctl