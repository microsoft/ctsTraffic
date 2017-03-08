#pragma once

#include <stdio.h>
#include <Windows.h>

#include <string>
#include <vector>
#include <tuple>

#include "ctScopeGuard.hpp"
#include "ctException.hpp"
#include "ctString.hpp"
#include "ctMath.hpp"


namespace ctsPerf {

    class ctsWriteDetails {
    private:
        HANDLE file_handle;

    public:
        // not making copyable
        ctsWriteDetails(const ctsWriteDetails&) = delete;
        ctsWriteDetails& operator=(const ctsWriteDetails&) = delete;


        ctsWriteDetails(_In_ LPCWSTR _file_name) :
            file_handle(INVALID_HANDLE_VALUE)
        {
            file_handle = ::CreateFileW(
                _file_name,
                GENERIC_WRITE,
                FILE_SHARE_READ, // allow others to read the file while we write to it
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL);
            if (INVALID_HANDLE_VALUE == file_handle) {
                throw ctl::ctException(
                    ::GetLastError(),
                    ctl::ctString::format_string(L"CreateFile(%s)", _file_name).c_str(),
                    true);
            }
            ctlScopeGuard(closeHandleOnError, { ::CloseHandle(file_handle); file_handle = INVALID_HANDLE_VALUE; });

            // write the UTF16 Byte order mark
            static const WCHAR BOM_UTF16 = 0xFEFF;
            DWORD length = sizeof(WCHAR);
            DWORD written;
            if (!::WriteFile(file_handle, &BOM_UTF16, length, &written, NULL)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }

            static const WCHAR Header[] = L"PerfCounter(CounterName),TotalCount,Min,Max,-1Std,Mean,+1Std,-1IQR,Median,+1IQR\r\n";
            length = sizeof(Header);
            if (!::WriteFile(file_handle, Header, length, &written, NULL)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }

            // everything succeeded, dismiss the scope guards
            closeHandleOnError.dismiss();
        }

        ~ctsWriteDetails() throw()
        {
            ::CloseHandle(file_handle);
        }

        //
        // The vector *will* be sorted before being returned (this is why it's non-const).
        //
        template <typename T>
        void write_details(_In_ LPCWSTR _class_name, _In_ LPCWSTR _counter_name, std::vector<T>& _data)
        {
            if (_data.empty()) {
                return;
            }

            start_row(_class_name, _counter_name);

            if (!_data.empty()) {
                // sort the data for IQR calculations
                sort(_data.begin(), _data.end());

                auto std_tuple = ctSampledStandardDeviation(_data.begin(), _data.end());
                auto interquartile_tuple = ctInterquartileRange(_data.begin(), _data.end());

                std::wstring formatted_data(write(static_cast<DWORD>(_data.size())));  // TotalCount
                formatted_data += write(*_data.begin(), *_data.rbegin()); // Min,Max
                formatted_data += write(std::get<0>(std_tuple), std::get<1>(std_tuple), std::get<2>(std_tuple)); // -1Std,Mean,+1Std
                formatted_data += write(std::get<0>(interquartile_tuple), std::get<1>(interquartile_tuple), std::get<2>(interquartile_tuple)); // -1IQR,Median,+1IQR

                DWORD length = static_cast<DWORD>(formatted_data.length() * sizeof(wchar_t));
                DWORD written;
                if (!::WriteFile(file_handle, formatted_data.c_str(), length, &written, NULL)) {
                    throw ctl::ctException(::GetLastError(), L"WriteFile", false);
                }
            }

            end_row();
        }

        template <typename T>
        void write_difference(_In_ LPCWSTR _class_name, _In_ LPCWSTR _counter_name, std::vector<T>& _data)
        {
            if (_data.empty()) {
                return;
            }

            start_row(_class_name, _counter_name);

            std::wstring difference = write(*_data.rbegin() - *_data.begin());

            DWORD length = static_cast<DWORD>(difference.length() * sizeof(wchar_t));
            DWORD written;
            if (!::WriteFile(file_handle, difference.c_str(), length, &written, NULL)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }

            end_row();
        }

    private:
        void start_row(_In_ LPCWSTR _class_name, _In_ LPCWSTR _counter_name)
        {
            auto formatted_string(ctl::ctString::format_string(
                L"%s (%s)", _class_name, _counter_name));
            // since writing to csv, can't embed a comma in the data
            ctl::ctString::replace_all(formatted_string, L",", L"-");

            DWORD length = static_cast<DWORD>(formatted_string.length() * sizeof(wchar_t));
            DWORD written;
            if (!::WriteFile(file_handle, formatted_string.c_str(), length, &written, NULL)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }
        }

        void end_row()
        {
            static const WCHAR EOL[2] = { L'\r', L'\n' };
            static const DWORD EOL_LEN = sizeof(EOL);
            DWORD written;
            if (!::WriteFile(file_handle, EOL, EOL_LEN, &written, NULL)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }
        }
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// write( ... )
        /// - overloads for ULONG and ULONGLONG data types
        /// - overloads for 1, 2, or 3 data points
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        std::wstring write(ULONGLONG _first_value)
        {
            return ctl::ctString::format_string(L",%llu", _first_value);
        }
        std::wstring write(ULONG _first_value)
        {
            return ctl::ctString::format_string(L",%lu", _first_value);
        }
        std::wstring write(double _first_value)
        {
            return ctl::ctString::format_string(L",%f", _first_value);
        }
        std::wstring write(ULONGLONG _first_value, ULONGLONG _second_value)
        {
            return ctl::ctString::format_string(L",%llu,%llu", _first_value, _second_value);
        }
        std::wstring write(ULONG _first_value, ULONG _second_value)
        {
            return ctl::ctString::format_string(L",%lu,%lu", _first_value, _second_value);
        }
        std::wstring write(double _first_value, double _second_value)
        {
            return ctl::ctString::format_string(L",%f,%f", _first_value, _second_value);
        }
        std::wstring write(ULONGLONG _first_value, ULONGLONG _second_value, ULONGLONG _third_value)
        {
            return ctl::ctString::format_string(L",%llu,%llu,%llu", _first_value, _second_value, _third_value);
        }
        std::wstring write(ULONG _first_value, ULONG _second_value, ULONG _third_value)
        {
            return ctl::ctString::format_string(L",%lu,%lu,%lu", _first_value, _second_value, _third_value);
        }
        std::wstring write(double _first_value, double _second_value, double _third_value)
        {
            return ctl::ctString::format_string(L",%f,%f,%f", _first_value, _second_value, _third_value);
        }
    };
}