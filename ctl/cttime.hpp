/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// CPP headers
#include <time.h>
#include <cassert>
#include <exception>
// OS headers
#include <Windows.h>
// local project headers
#include "ctException.hpp"
#include "ctHandle.hpp"
#include "ctVersionConversion.hpp"


namespace ctl {

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //  ctTime
    //
    //  A class to encapsulate the various ways in which one can represent time in Windows
    //    - as a DOS time  (2 WORDs)
    //    - as a ULONGLONG from the eventlog (in milliseconds)
    //  - as a SYSTEMTIME structure
    //  - as a FILETIME structure
    //
    //  Also handles "translations" from UTC and local time
    //
    //  Note:  time_t is not yet implemented, since the size can differ, and I need a consistent
    //         way to handle it (32 bit value, 64 bit value, or both)
    //
    //  Methods not specified NOEXCEPT can throw a ctException c++ exception on failure
    //  - if an underlying Win32 API fails
    //
    //  All Methods which can throw maintain a strong-exception-guarantee
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctTime
    {
    public:
        ///////////////////////////////////////////////////////////////////////////////
        //
        // Default Constructor
        //
        ///////////////////////////////////////////////////////////////////////////////
        ctTime(bool _setCurrentTime = false) NOEXCEPT;

        ///////////////////////////////////////////////////////////////////////////////
        //
        // Constructors, taking
        // - a DOS Date
        // - a ULONGLONG for milliseconds
        // - a SYSTEMTIME structure
        // - a FILETIME structure
        // - a DATETIME (CIM_DATETIME) WMI string
        //
        // Failure condition: will throw a ctException if the Win32 call fails
        //
        ///////////////////////////////////////////////////////////////////////////////
        ctTime(_In_ WORD wDate, _In_ WORD wTime);
        ctTime(_In_ ULONGLONG ullMilliseconds, _In_ bool bUTCTime = true);
        ctTime(_In_ const SYSTEMTIME& systemTime, _In_ bool bUTCTime = true);
        ctTime(_In_ const FILETIME& fileTime, _In_ bool bUTCTime = true);
        ctTime(_In_ LPCWSTR _datetime);

        ///////////////////////////////////////////////////////////////////////////////
        //
        // Default:
        // - destructor
        // - copy constructor
        // - assignment operator
        //
        ///////////////////////////////////////////////////////////////////////////////

        ///////////////////////////////////////////////////////////////////////////////
        //
        // Comparison operators returning bool
        //
        ///////////////////////////////////////////////////////////////////////////////
        bool operator==(_In_ const ctTime& _refTime) const NOEXCEPT;
        bool operator!=(_In_ const ctTime& _refTime) const NOEXCEPT;
        bool operator>(_In_ const ctTime& _refTime) const NOEXCEPT;
        bool operator<(_In_ const ctTime& _refTime) const NOEXCEPT;
        bool operator>=(_In_ const ctTime& _refTime) const NOEXCEPT;
        bool operator<=(_In_ const ctTime& _refTime) const NOEXCEPT;

        ///////////////////////////////////////////////////////////////////////////////
        //
        // Arithmatic operators
        //
        ///////////////////////////////////////////////////////////////////////////////
        ctTime operator+(_In_ const ctTime& _refTime) const NOEXCEPT;
        ctTime operator-(_In_ const ctTime& _refTime) const NOEXCEPT;
        ctTime& operator+=(_In_ const ctTime& _refTime) NOEXCEPT;
        ctTime& operator-=(_In_ const ctTime& _refTime) NOEXCEPT;

        ///////////////////////////////////////////////////////////////////////////////
        //
        // setters
        //
        ///////////////////////////////////////////////////////////////////////////////

        /// resets time back to 0
        void reset() NOEXCEPT;
        /// Sets time based on the current time in the system (UTC)
        void setCurrentSystemTime() NOEXCEPT;
        /// Sets time taking a DOS Date (UTC)
        void setDOSTime(_In_ WORD wDate, _In_ WORD wTime);
        /// Sets time as # ms from Jan. 1, 1970 and bool if UTC time
        void setMilliSeconds(_In_ ULONGLONG dwTime, _In_ bool bUTCTime = true);
        /// Sets time taking a SYSTEMTIME structure and bool if UTC time
        void setSystemTime(_In_ const SYSTEMTIME& systemTime, _In_ bool bUTCTime = true);
        /// Sets time taking a FILETIME structure and bool if UTC time
        void setFileTime(_In_ const FILETIME& fileTime, _In_ bool bUTCTime = true);
        /// Sets time taking a DATETIME (CIM_DATETIME or Interval) string
        void setDateTime(_In_ LPCWSTR datetime);

        ///////////////////////////////////////////////////////////////////////////////
        //
        // getters
        //
        ///////////////////////////////////////////////////////////////////////////////

        /// Retrieve time as a DOS time in 2 WORD 's (UTC)
        void getDOSTime(_In_ WORD& wDate, _In_ WORD& wTime) const;

        /// Retrieves time in ms since Jan 1, 1970 (UTC)
        ULONGLONG getMilliSeconds() const NOEXCEPT;
        /// Retrieves time in ms since Jan 1, 1970 (Local)
        ULONGLONG getLocalMilliSeconds() const;

        /// Retrieves time in a LARGE_INTEGER, suitable for use with win32 timer functions (UTC)
        LARGE_INTEGER getLargeIntegerTime() const;

        /// Retrieves time in a SYSTEMTIME struct (UTC)
        SYSTEMTIME getSystemTime() const;
        /// Retrieves time in a SYSTEMTIME struct (Local)
        SYSTEMTIME getLocalSystemTime() const;

        /// Retrieves time in a FILETIME struct (UTC)
        FILETIME getFileTime() const NOEXCEPT;
        /// Retrieves time in a FILETIME struct (Local)
        FILETIME getLocalFileTime() const;
        /// Retrieves time in a CIM_DATETIME string (UTC)
        std::wstring getCIMDateTime() const;
        /// Retrieves time in a readably-friendly format (Local)
        std::wstring getFriendlyDateTime() const;

        ///////////////////////////////////////////////////////////////////////////////
        //
        // win32 timer helpers
        //
        ///////////////////////////////////////////////////////////////////////////////
        _Enum_is_bitflag_
        enum TimerFlags
        {
            None = 0x0,
            ManualReset = 0x1,
            ResumesSystem = 0x2
        };
        HANDLE startWaitableTimer(TimerFlags _timerFlags) const;

    private:
        void ConvertToUTC(_In_ const FILETIME& localTime);

        // Number of 100 nanosecond units from 1/1/1601 to 1/1/1970.
        static const ULONGLONG WIN32_FILETIME_EPOCH_BIAS = 116444736000000000;

        // FILETIME tracks 100-nanosecond intervals since January 1, 1601 (UTC)
        FILETIME fileUTCTime;
    };


    ///////////////////////////////////////////////////////////////////////////////
    //
    // Default Constructor
    //
    // No-fail operation
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline ctTime::ctTime(bool _setCurrentTime) NOEXCEPT
    {
        if (_setCurrentTime) {
            this->setCurrentSystemTime();
        } else {
            fileUTCTime.dwLowDateTime = 0UL;
            fileUTCTime.dwHighDateTime = 0UL;
        }
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Constructor taking a DOS Date
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline ctTime::ctTime(_In_ WORD _wDate, _In_ WORD _wTime)
    {
        setDOSTime(_wDate, _wTime);
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Constructor taking a ULONGLONG for # of milliseconds
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline ctTime::ctTime(_In_ ULONGLONG _ullMilliseconds, _In_ bool _bUTCTime)
    {
        setMilliSeconds(_ullMilliseconds, _bUTCTime);
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Constructor taking a SYSTEMTIME structure
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline ctTime::ctTime(_In_ const SYSTEMTIME& _systemTime, _In_ bool _bUTCTime)
    {
        setSystemTime(_systemTime, _bUTCTime);
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Constructor taking a FILETIME structure
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline ctTime::ctTime(_In_ const FILETIME& _fileTime, _In_ bool _bUTCTime)
    {
        setFileTime(_fileTime, _bUTCTime);
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Constructor taking a DATETIME string - in CIM_DATETIME or Interval format
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline ctTime::ctTime(_In_ LPCWSTR _datetime)
    {
        setDateTime(_datetime);
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Comparison operators
    //        operator == (ctTime&)
    //        operator != (ctTime&)
    //
    // No-Fail operations
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline bool ctTime::operator==(_In_ const ctTime& _refTime) const NOEXCEPT
    {
        return ((fileUTCTime.dwHighDateTime == _refTime.fileUTCTime.dwHighDateTime) &&
                (fileUTCTime.dwLowDateTime == _refTime.fileUTCTime.dwLowDateTime));
    }
    inline bool ctTime::operator!=(_In_ const ctTime& _refTime) const NOEXCEPT
    {
        return ((fileUTCTime.dwHighDateTime != _refTime.fileUTCTime.dwHighDateTime) ||
                (fileUTCTime.dwLowDateTime != _refTime.fileUTCTime.dwLowDateTime));
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Comparison operators
    //        operator == (ctTime&)
    //        operator != (ctTime&)
    //
    // No-Fail operations
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline ctTime& ctTime::operator+=(_In_ const ctTime& _refTime) NOEXCEPT
    {
        ULARGE_INTEGER ulIntThis;
        ulIntThis.LowPart = fileUTCTime.dwLowDateTime;
        ulIntThis.HighPart = fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntRef;
        ulIntRef.LowPart = _refTime.fileUTCTime.dwLowDateTime;
        ulIntRef.HighPart = _refTime.fileUTCTime.dwHighDateTime;

        ulIntThis.QuadPart += ulIntRef.QuadPart;

        fileUTCTime.dwLowDateTime = ulIntThis.LowPart;
        fileUTCTime.dwHighDateTime = ulIntThis.HighPart;
        return *this;
    }
    inline ctTime& ctTime::operator-=(_In_ const ctTime& _refTime) NOEXCEPT
    {
        ULARGE_INTEGER ulIntThis;
        ulIntThis.LowPart = fileUTCTime.dwLowDateTime;
        ulIntThis.HighPart = fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntRef;
        ulIntRef.LowPart = _refTime.fileUTCTime.dwLowDateTime;
        ulIntRef.HighPart = _refTime.fileUTCTime.dwHighDateTime;

        ulIntThis.QuadPart -= ulIntRef.QuadPart;

        fileUTCTime.dwLowDateTime = ulIntThis.LowPart;
        fileUTCTime.dwHighDateTime = ulIntThis.HighPart;
        return *this;
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Greater-than/Less-than operators
    //        operator + (ctTime&)
    //        operator - (ctTime&)
    //
    // Failure condition:  constructor might fail when building a new ctTime
    //    - will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline ctTime ctTime::operator+(_In_ const ctTime& _refTime) const NOEXCEPT
    {
        ULARGE_INTEGER ulIntThis;
        ulIntThis.LowPart = fileUTCTime.dwLowDateTime;
        ulIntThis.HighPart = fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntRef;
        ulIntRef.LowPart = _refTime.fileUTCTime.dwLowDateTime;
        ulIntRef.HighPart = _refTime.fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntTemp;
        ulIntTemp.QuadPart = ulIntThis.QuadPart + ulIntRef.QuadPart;

        FILETIME ftTemp;
        ftTemp.dwLowDateTime = ulIntTemp.LowPart;
        ftTemp.dwHighDateTime = ulIntTemp.HighPart;

        return ctTime(ftTemp);
    }
    inline ctTime ctTime::operator-(_In_ const ctTime& _refTime) const NOEXCEPT
    {
        ULARGE_INTEGER ulIntThis;
        ulIntThis.LowPart = fileUTCTime.dwLowDateTime;
        ulIntThis.HighPart = fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntRef;
        ulIntRef.LowPart = _refTime.fileUTCTime.dwLowDateTime;
        ulIntRef.HighPart = _refTime.fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntTemp;
        ulIntTemp.QuadPart = ulIntThis.QuadPart - ulIntRef.QuadPart;

        FILETIME ftTemp;
        ftTemp.dwLowDateTime = ulIntTemp.LowPart;
        ftTemp.dwHighDateTime = ulIntTemp.HighPart;

        return ctTime(ftTemp);
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Greater-than/Less-than operators
    //        operator > (ctTime&)
    //        operator < (ctTime&)
    //
    // No-Fail operations
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline bool ctTime::operator>(_In_ const ctTime& _refTime) const NOEXCEPT
    {
        ULARGE_INTEGER ulIntThis;
        ulIntThis.LowPart = fileUTCTime.dwLowDateTime;
        ulIntThis.HighPart = fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntRef;
        ulIntRef.LowPart = _refTime.fileUTCTime.dwLowDateTime;
        ulIntRef.HighPart = _refTime.fileUTCTime.dwHighDateTime;

        return (ulIntThis.QuadPart > ulIntRef.QuadPart);
    }
    inline bool ctTime::operator<(_In_ const ctTime& _refTime) const NOEXCEPT
    {
        ULARGE_INTEGER ulIntThis;
        ulIntThis.LowPart = fileUTCTime.dwLowDateTime;
        ulIntThis.HighPart = fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntRef;
        ulIntRef.LowPart = _refTime.fileUTCTime.dwLowDateTime;
        ulIntRef.HighPart = _refTime.fileUTCTime.dwHighDateTime;

        return (ulIntThis.QuadPart < ulIntRef.QuadPart);
    }
    ///////////////////////////////////////////////////////////////////////////////
    //
    // Greater-than-or-equals/Less-than-or-equals operators
    //        operator >= (ctTime&)
    //        operator <= (ctTime&)
    //
    // No-Fail operations
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline bool ctTime::operator>=(_In_ const ctTime& _refTime) const NOEXCEPT
    {
        ULARGE_INTEGER ulIntThis;
        ulIntThis.LowPart = fileUTCTime.dwLowDateTime;
        ulIntThis.HighPart = fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntRef;
        ulIntRef.LowPart = _refTime.fileUTCTime.dwLowDateTime;
        ulIntRef.HighPart = _refTime.fileUTCTime.dwHighDateTime;

        return (ulIntThis.QuadPart >= ulIntRef.QuadPart);
    }
    inline bool ctTime::operator<=(_In_ const ctTime& _refTime) const NOEXCEPT
    {
        ULARGE_INTEGER ulIntThis;
        ulIntThis.LowPart = fileUTCTime.dwLowDateTime;
        ulIntThis.HighPart = fileUTCTime.dwHighDateTime;

        ULARGE_INTEGER ulIntRef;
        ulIntRef.LowPart = _refTime.fileUTCTime.dwLowDateTime;
        ulIntRef.HighPart = _refTime.fileUTCTime.dwHighDateTime;

        return (ulIntThis.QuadPart <= ulIntRef.QuadPart);
    }
    ///////////////////////////////////////////////////////////////////////////////
    //  reset()
    //
    //  Resets time back to zero
    //
    //  No-Fail operation
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline void ctTime::reset() NOEXCEPT
    {
        fileUTCTime.dwLowDateTime = 0UL;
        fileUTCTime.dwHighDateTime = 0UL;
    }
    ///////////////////////////////////////////////////////////////////////////////
    //  setCurrentSystemTime()
    //
    //  Setter for the current time in the system
    //
    //  No-Fail operation
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline void ctTime::setCurrentSystemTime() NOEXCEPT
    {
        ::GetSystemTimeAsFileTime(&fileUTCTime);
    }
    ///////////////////////////////////////////////////////////////////////////////
    // setDOSTime()
    //
    // Setter taking a DOS Date
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline void ctTime::setDOSTime(_In_ WORD _wDate, _In_ WORD _wTime)
    {
        if (!::DosDateTimeToFileTime(_wDate, _wTime, &fileUTCTime)) {
            throw ctException(::GetLastError(), L"DosDateTimeToFileTime", L"ctTime::ctTime", false);
        }

        ConvertToUTC(fileUTCTime);
    }
    ///////////////////////////////////////////////////////////////////////////////
    // setMilliSeconds
    //
    // Setter taking # of milliseconds since midnight, Jan. 1, 1970
    //  - e.g. from ReadEventLog()
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline void ctTime::setMilliSeconds(_In_ ULONGLONG _ullTime, _In_ bool _bUTCTime)
    {
        // convert seconds to 100-nano-seconds
        // - then add the epoch bias
        ULONGLONG ullong = (_ullTime * 10000);
        if (ullong < _ullTime) {
            throw std::runtime_error("ctl::ctTime::setMilliSeconds - ULONGLONG overflow");
        }
        ullong += WIN32_FILETIME_EPOCH_BIAS;
        if (ullong < WIN32_FILETIME_EPOCH_BIAS) {
            throw std::runtime_error("ctl::ctTime::setMilliSeconds - ULONGLONG overflow");
        }

        fileUTCTime.dwLowDateTime = static_cast<DWORD>(ullong & 0x00000000ffffffff);
        fileUTCTime.dwHighDateTime = static_cast<DWORD>(ullong >> 32);

        if (!_bUTCTime) {
            ConvertToUTC(fileUTCTime);
        }
    }
    ///////////////////////////////////////////////////////////////////////////////
    // setSystemTime()
    //
    // Setter taking a SYSTEMTIME structure
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline void ctTime::setSystemTime(_In_ const SYSTEMTIME& _systemTime, _In_ bool _bUTCTime)
    {
        if (!::SystemTimeToFileTime(&_systemTime, &fileUTCTime)) {
            throw ctException(::GetLastError(), L"SystemTimeToFileTime", L"ctTime::setSystemTime", false);
        }

        if (!_bUTCTime) {
            ConvertToUTC(fileUTCTime);
        }
    }
    ///////////////////////////////////////////////////////////////////////////////
    // setFileTime()
    //
    // Setter taking a FILETIME structure
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline void ctTime::setFileTime(_In_ const FILETIME& _fileTime, _In_ bool _bUTCTime)
    {
        fileUTCTime.dwLowDateTime = _fileTime.dwLowDateTime;
        fileUTCTime.dwHighDateTime = _fileTime.dwHighDateTime;

        if (!_bUTCTime) {
            ConvertToUTC(fileUTCTime);
        }
    }
    ///////////////////////////////////////////////////////////////////////////////
    // setDateTime()
    //
    // Taking a DATETIME (CIM_DATETIME) string:
    //   yyyymmddHHMMSS.mmmmmmsUUU  (strlen = 25)
    //
    // Taking a DATETIME (Interval) string:
    //   ddddddddHHMMSS.mmmmmm:000  (strlen = 25)
    //
    // 1st format: CIM_DATETIME
    // -----------------------------
    // - yyyy: Four-digit year (0000 through 9999).
    // - mm:   Two-digit month (01 through 12).
    // - dd:   Two-digit day of the month (01 through 31).
    // - HH:   Two-digit hour of the day using the 24-hour clock (00 through 23).
    // - MM:   Two-digit minute in the hour (00 through 59).
    // - SS:   Two-digit number of seconds in the minute (00 through 59).
    // - mmmmmm: Six-digit number of microseconds in the second (000000 through 999999). Your implementation does not have to support evaluation using this field.
    //           However, this field must always be present to preserve the fixed-length nature of the string.
    // - mmm:   Three-digit number of milliseconds in the minute (000 through 999).
    // - s:     Plus sign (+) or minus sign (-) to indicate a positive or negative offset from Coordinated Universal Times (UTC).
    // - UUU:   Three-digit offset indicating the number of minutes that the originating time zone deviates from UTC.
    //          For WMI, it is encouraged, but not required, to convert times to GMT (a UTC offset of zero).
    //
    // 2nd format: Interval DATETIME
    // -----------------------------
    // - dddddddd: Eight digits that represent a number of days (00000000 through 99999999).
    // - HH: Two-digit hour of the day that uses the 24-hour clock (00 through 23).
    // - MM: Two-digit minute in the hour (00 through 59).
    // - SS: Two-digit number of seconds in the minute (00 through 59).
    // - mmmmmm: Six-digit number of microseconds in the second (000000 through 999999). Your implementation is not required to support evaluation using this field, 
    //           but this field must always be present to preserve the fixed-length nature of the string.
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline void ctTime::setDateTime(_In_ LPCWSTR _datetime)
    {
        size_t len = wcslen(_datetime);
        if (len != 25) {
            throw ctl::ctException(1, L"DateTime string invalid", L"ctl::ctTime::setDateTime", false);
        }
        //
        // largest datetime segment is 9 in length
        //
        size_t offset = 0;
        WCHAR conversion_string[9];
        wmemset(conversion_string, L'\0', 9);

        //
        // fork whether this is a calendar datetime or interval datetime
        //
        if (0 == wmemcmp(L":000", &_datetime[21], 4)) {
            //
            // this is an interval datetime
            //
            // read the years
            ::wmemcpy(conversion_string, _datetime + offset, 8);
            conversion_string[8] = L'\0';
            unsigned days = static_cast<unsigned>(::_wtoi(conversion_string));
            offset += 8;
            // read the hours
            ::wmemcpy(conversion_string, _datetime + offset, 2);
            conversion_string[2] = L'\0';
            unsigned hours = static_cast<unsigned>(::_wtoi(conversion_string));
            offset += 2;
            // read the minutes
            ::wmemcpy(conversion_string, _datetime + offset, 2);
            conversion_string[2] = L'\0';
            unsigned minutes = static_cast<unsigned>(::_wtoi(conversion_string));
            offset += 2;
            // read the seconds
            ::wmemcpy(conversion_string, _datetime + offset, 2);
            conversion_string[2] = L'\0';
            unsigned seconds = static_cast<unsigned>(::_wtoi(conversion_string));
            offset += 2;
            // jump the dot
            if (_datetime[offset] != L'.') {
                throw ctl::ctException(2, L"DateTime string invalid", L"ctl::ctTime::setDateTime", false);
            }
            ++offset;
            // read the milliseconds, then jump past the microseconds
            ::wmemcpy(conversion_string, _datetime + offset, 3);
            conversion_string[3] = L'\0';
            ULONGLONG milliseconds = static_cast<ULONGLONG>(::_wtoi(conversion_string));
            milliseconds += static_cast<ULONGLONG>(seconds) * 1000ULL;
            milliseconds += static_cast<ULONGLONG>(minutes) * 60ULL * 1000ULL;
            milliseconds += static_cast<ULONGLONG>(hours) * 60ULL * 60ULL * 1000ULL;
            milliseconds += static_cast<ULONGLONG>(days) * 24ULL * 60ULL * 60ULL * 1000ULL;

            this->setMilliSeconds(milliseconds);
        } else {
            //
            // this is a CIM datetime
            //
            SYSTEMTIME st;
            // read the years
            ::wmemcpy(conversion_string, _datetime + offset, 4);
            conversion_string[4] = L'\0';
            st.wYear = static_cast<WORD>(::_wtoi(conversion_string));
            offset += 4;
            // read the months
            ::wmemcpy(conversion_string, _datetime + offset, 2);
            conversion_string[2] = L'\0';
            st.wMonth = static_cast<WORD>(::_wtoi(conversion_string));
            offset += 2;
            // read the days
            ::wmemcpy(conversion_string, _datetime + offset, 2);
            conversion_string[2] = L'\0';
            st.wDay = static_cast<WORD>(::_wtoi(conversion_string));
            offset += 2;
            // read the hours
            ::wmemcpy(conversion_string, _datetime + offset, 2);
            conversion_string[2] = L'\0';
            st.wHour = static_cast<WORD>(::_wtoi(conversion_string));
            offset += 2;
            // read the minutes
            ::wmemcpy(conversion_string, _datetime + offset, 2);
            conversion_string[2] = L'\0';
            st.wMinute = static_cast<WORD>(::_wtoi(conversion_string));
            offset += 2;
            // read the seconds
            ::wmemcpy(conversion_string, _datetime + offset, 2);
            conversion_string[2] = L'\0';
            st.wSecond = static_cast<WORD>(::_wtoi(conversion_string));
            offset += 2;
            // jump the dot
            if (_datetime[offset] != L'.') {
                throw ctl::ctException(2, L"DateTime string invalid", L"ctl::ctTime::setDateTime", false);
            }
            ++offset;
            // read the milliseconds, then jump past the microseconds
            ::wmemcpy(conversion_string, _datetime + offset, 3);
            conversion_string[3] = L'\0';
            st.wMilliseconds = static_cast<WORD>(::_wtoi(conversion_string));
            offset += 6;
            //
            // last segment will be one of the following:
            // "+UUU"
            // "-UUU"
            // - both meaning number of minutes +/- to get to UTC
            //
            // read the number of minutes to move forward/backward to set to UTC time
            //
            WCHAR utc_marker = _datetime[offset];
            ++offset;
            // read the number of minutes to UTC
            signed int utc_minute_variance;
            ::wmemcpy(conversion_string, _datetime + offset, 3);
            conversion_string[3] = L'\0';
            // update the variance according to the marker
            if (utc_marker == L'+') {
                utc_minute_variance = ::_wtoi(conversion_string);
            } else if (utc_marker == L'-') {
                utc_minute_variance = (-1) * ::_wtoi(conversion_string);
            } else {
                throw ctl::ctException(3, L"DateTime string invalid", L"ctl::ctTime::setDateTime", false);
            }
            //
            // convert the SYSTEMTIME to FILETIME for this object
            // - and adjust to UTC
            //
            this->setSystemTime(st);
            // adjust # of minutes as necessary
            ULONGLONG filetimeticks = (utc_minute_variance > 0) ? (utc_minute_variance) : ((-1) * utc_minute_variance);
            filetimeticks *= 60ULL; // calculate seconds
            filetimeticks *= 1000ULL; // calculate milliseconds
            filetimeticks *= 10000ULL; // calculate hundreds-of-nanoseconds

            ULARGE_INTEGER ulIntThis;
            ulIntThis.LowPart = fileUTCTime.dwLowDateTime;
            ulIntThis.HighPart = fileUTCTime.dwHighDateTime;

            // We're converting FROM a local time at the given UTC variance TO UTC,
            // so we need to go the opposite direction of the UTC offset
            if (utc_minute_variance > 0) {
                ulIntThis.QuadPart -= filetimeticks;
            } else {
                ulIntThis.QuadPart += filetimeticks;
            }

            fileUTCTime.dwLowDateTime = ulIntThis.LowPart;
            fileUTCTime.dwHighDateTime = ulIntThis.HighPart;
        }
    }
    ///////////////////////////////////////////////////////////////////////////////
    // getDOSTime()
    //
    // Retrieve time as a DOS time in 2 WORD 's
    //    
    ///////////////////////////////////////////////////////////////////////////////
    inline void ctTime::getDOSTime(_In_ WORD& _wDate, _In_ WORD& _wTime) const
    {
        FILETIME localTime;
        if (!::FileTimeToLocalFileTime(&fileUTCTime, &localTime)) {
            throw ctException(::GetLastError(), L"::FileTimeToLocalFileTime", L"ctTime::getDOSTime", false);
        }

        if (!::FileTimeToDosDateTime(&localTime, &_wDate, &_wTime)) {
            throw ctException(::GetLastError(), L"FileTimeToDosDateTime", L"ctTime::getDOSTime", false);
        }
    }
    ///////////////////////////////////////////////////////////////////////////////
    //  getMilliSeconds / getLocalMilliSeconds
    //
    //  Retrieves from the object a DWORD
    //
    //  The reverse of the setSeconds() algorithm #define UInt32x32To64()
    //
    //  #define UInt32x32To64(a, b) ((ULONGLONG)((DWORD)(a)) * (ULONGLONG)((DWORD)(b)))
    //
    //  Algorithm from setMilliseconds():
    //  - ULONGLONG llong = Int32x32To64(dwTime, 10000) + WIN32_FILETIME_EPOCH_BIAS;
    //  - tempTime.dwLowDateTime = static_cast<DWORD>(llong);
    //  - tempTime.dwHighDateTime = static_cast<DWORD>(llong >> 32);
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline ULONGLONG ctTime::getMilliSeconds() const NOEXCEPT
    {
        ULARGE_INTEGER ulIntTime;
        ulIntTime.LowPart = fileUTCTime.dwLowDateTime;
        ulIntTime.HighPart = fileUTCTime.dwHighDateTime;

        ulIntTime.QuadPart -= WIN32_FILETIME_EPOCH_BIAS;
        ulIntTime.QuadPart /= 10000;

        return ulIntTime.QuadPart;
    }
    inline ULONGLONG ctTime::getLocalMilliSeconds() const
    {
        FILETIME localFileTime;
        if (!::FileTimeToLocalFileTime(&fileUTCTime, &localFileTime)) {
            throw ctException(::GetLastError(), L"::FileTimeToLocalFileTime", L"ctTime::getLocalMilliSeconds", false);
        }

        ULARGE_INTEGER ulIntTime;
        ulIntTime.LowPart = localFileTime.dwLowDateTime;
        ulIntTime.HighPart = localFileTime.dwHighDateTime;

        ulIntTime.QuadPart -= WIN32_FILETIME_EPOCH_BIAS;
        ulIntTime.QuadPart /= 10000;

        return ulIntTime.QuadPart;
    }

    ///////////////////////////////////////////////////////////////////////////////
    // getLargeIntegerTime()
    //
    // Retrieves time in a LARGE_INTEGER, suitable for passing to win32 timer APIs
    //
    // Failure condition: Throws ctException if time can't fit in LARGE_INTEGER
    //
    // It is by design that no Local version is provided - this time format is only
    // useful as an argument to APIs which universally expect UTC input
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline LARGE_INTEGER ctTime::getLargeIntegerTime() const
    {
        if (fileUTCTime.dwHighDateTime > LONG_MAX) {
            throw ctException(
                ERROR_ARITHMETIC_OVERFLOW, 
                L"Integer overflow assigning fileUTCTime.dwHighDateTime to LONG", 
                L"ctTime::getLargeIntegerTime", 
                false);
        }

        LARGE_INTEGER lIntTime;
        lIntTime.HighPart = fileUTCTime.dwHighDateTime;
        lIntTime.LowPart = fileUTCTime.dwLowDateTime;

        return lIntTime;
    }

    ///////////////////////////////////////////////////////////////////////////////
    // getSystemTime() / getLocalSystemTime()
    //
    // Retrieve time in a SYSTEMTIME struct
    //    
    ///////////////////////////////////////////////////////////////////////////////
    inline SYSTEMTIME ctTime::getSystemTime() const
    {
        SYSTEMTIME systemTime;
        if (!::FileTimeToSystemTime(&fileUTCTime, &systemTime)) {
            throw ctException(::GetLastError(), L"::FileTimeToSystemTime", L"ctTime::getSystemTime", false);
        }
        return systemTime;
    }
    inline SYSTEMTIME ctTime::getLocalSystemTime() const
    {
        FILETIME tempFileTime;
        if (!::FileTimeToLocalFileTime(&fileUTCTime, &tempFileTime)) {
            throw ctException(::GetLastError(), L"::FileTimeToLocalFileTime", L"ctTime::getLocalSystemTime", false);
        }
        SYSTEMTIME systemTime;
        if (!::FileTimeToSystemTime(&tempFileTime, &systemTime)) {
            throw ctException(::GetLastError(), L"::FileTimeToSystemTime", L"ctTime::getLocalSystemTime", false);
        }
        return systemTime;
    }
    ///////////////////////////////////////////////////////////////////////////////
    // getFileTime() / getLocalFileTime()
    //
    // Retrieve time in a FILETIME struct
    //    
    ///////////////////////////////////////////////////////////////////////////////
    inline FILETIME ctTime::getFileTime() const NOEXCEPT
    {
        return fileUTCTime;
    }
    inline FILETIME ctTime::getLocalFileTime() const
    {
        FILETIME tempFileTime;
        if (!::FileTimeToLocalFileTime(&fileUTCTime, &tempFileTime)) {
            throw ctException(::GetLastError(), L"::FileTimeToLocalFileTime", L"ctTime::getLocalFileTime", false);
        }

        return tempFileTime;
    }

    ///////////////////////////////////////////////////////////////////////////////
    // getCIMDateTime()
    //
    // Retrieve time as a wstring in WMI's CIM_DATETIME string format:
    //
    //   yyyymmddHHMMSS.mmmmmmsUUU
    //
    // See documentation for setDateTime, above, for an explanation of the terms
    // in the string.
    //
    // This method will always return a string formatted against UTC time; that is,
    // the final four characters (the sUUU) will always be +000.
    //
    // This method will throw if any value exceeds the maximum length allowed by
    // the format (in particular, if the year is greater than 9999). It may also
    // throw under low memory since it must allocate a std::wstring.
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline std::wstring ctTime::getCIMDateTime() const
    {
        SYSTEMTIME st = this->getSystemTime();
        wchar_t rawCimDateTime[26];

        // The sprintf can pad numbers with 0s, but won't enforce maximum lengths
        // We need to validate that first.
        if (st.wYear > 9999) {
            throw ctException(
                st.wYear, 
                L"ctTime instance invalid for conversion to CIM_DATETIME (year too large)", 
                L"ctTime::getCIMDateTime", 
                false);
        }

        int ret = _snwprintf_s(rawCimDateTime, 26, 25,
                               L"%04hu"  L"%02hu"   L"%02hu" L"%02hu"  L"%02hu"    L"%02hu"   L"."  L"%03hu"         L"000+000",
                               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds
                               );

        if (ret != 25) {
            throw ctException(ret, L"_snwprintf_s", L"ctTime::getCIMDateTime", false);
        }

        return rawCimDateTime;
    }

    ///////////////////////////////////////////////////////////////////////////////
    // startWaitableTimer
    //
    // Creates and sets a win32 Waitable Timer for the absolute time specified by
    // this ctTime object.
    //
    // The caller is responsible for closing the HANDLE with CloseHandle once
    // they're done with it. The recommended method of doing so is to immediately
    // wrap the returned handle in a ctScopedHandle or ctSharedHandle.
    //
    //
    // Use ::CreateWaitableTimer directly if you need to set special security attributes
    // or specify a callback
    //
    // Flag meanings (may be combined with bitwise operations):
    //   * ManualReset:
    //       If set, this timer is a manual-reset notification timer. Otherwise, it's a
    //       synchronization timer. See bManualReset parameter of CreateWaitableTimer.
    //   * ResumesSystem:
    //       If set, a system in suspended power conservation mode when this timer is to
    //       be set to signaled will be resumed. Otherwise, the system will not be resumed.
    //       See fResume parameter of SetWaitableTimer.
    //
    // If underlying win32 APIs fail, throws a ctException.
    // Failure condition: will throw a ctException if the Win32 call fails. Will
    // never return NULL or INVALID_HANDLE
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline HANDLE ctTime::startWaitableTimer(ctTime::TimerFlags _timerFlags = ctTime::TimerFlags::None) const
    {
        ctScopedHandle timerHandle(::CreateWaitableTimer(
            NULL, // Default security attributes
            !!(_timerFlags & ctTime::TimerFlags::ManualReset),
            NULL)); // No name

        if (timerHandle.get() == NULL) {
            throw ctException(::GetLastError(), L"CreateWaitableTimer", L"ctTime::startWaitableTimer", false);
        }

        LARGE_INTEGER dueTime = this->getLargeIntegerTime();
        if (!::SetWaitableTimer(
            timerHandle.get(),
            &dueTime,
            0, // Not periodic
            NULL, // No completion callback
            NULL, // No completion callback context
            !!(_timerFlags & ctTime::TimerFlags::ResumesSystem))) {
            throw ctException(::GetLastError(), L"SetWaitableTimer", L"ctTime::startWaitableTimer", false);
        }

        return timerHandle.release();
    }


    ///////////////////////////////////////////////////////////////////////////////
    // getFriendlyDateTime()
    //
    // Retrieve time as a wstring in a friendly readable format, e.g.
    //   12/4/2012 16:14:38.928
    //
    // This method will always return a string formatted against the local time.
    //
    // This method will throw if any value exceeds the maximum length allowed by
    // the format (in particular, if the year is greater than 9999). It may also
    // throw under low memory since it must allocate a std::wstring.
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline std::wstring ctTime::getFriendlyDateTime() const
    {
        SYSTEMTIME st = this->getLocalSystemTime();
        wchar_t rawFriendlyDateTime[24];

        // The sprintf can pad numbers with 0s, but won't enforce maximum lengths
        // We need to validate that first.
        if (st.wYear > 9999) {
            throw ctException(
                st.wYear, 
                L"ctTime instance invalid for conversion to CIM_DATETIME (year too large)", 
                L"ctTime::getFriendlyDateTime", 
                false);
        }

        int ret = ::_snwprintf_s(
            rawFriendlyDateTime,
            24, 23, // total size, max chars
            L"%02hu/%02hu/%04hu %02hu:%02hu:%02hu.%03hu",
            st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        if (ret != 23) {
            throw ctException(ret, L"_snwprintf_s", L"ctTime::getFriendlyDateTime", false);
        }

        return rawFriendlyDateTime;
    }

    ///////////////////////////////////////////////////////////////////////////////
    // ConvertToUTC
    //
    // Private conversion function to update the member FILETIME variable
    // - to the translated "local" FILETIME
    //
    // Failure condition: will throw a ctException if the Win32 call fails
    //
    ///////////////////////////////////////////////////////////////////////////////
    inline void ctTime::ConvertToUTC(_In_ const FILETIME& _localTime)
    {
        FILETIME tempTime;
        if (!::LocalFileTimeToFileTime(&_localTime, &tempTime)) {
            throw ctException(::GetLastError(), L"::LocalFileTimeToFileTime", L"ctTime::ConvertToUTC", false);
        }
        fileUTCTime.dwLowDateTime = tempTime.dwLowDateTime;
        fileUTCTime.dwHighDateTime = tempTime.dwHighDateTime;
    }

} // namespace ctl
