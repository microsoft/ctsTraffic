#include "ctsWriteDetails.h"

#include <stdio.h>
#include <Windows.h>

#include <string>

#include "ctScopeGuard.hpp"
#include "ctException.hpp"
#include "ctString.hpp"


namespace ctsPerf {

    ctsWriteDetails::ctsWriteDetails(_In_ LPCWSTR _file_name, bool _mean_only) :
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

        if (_mean_only) {
            static const WCHAR MeanHeader[] = L"PerfCounter(CounterName),TotalCount,Min,Max,Mean\r\n";
            length = sizeof(MeanHeader);
            if (!::WriteFile(file_handle, MeanHeader, length, &written, NULL)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }
        } else {
            static const WCHAR DetailedHeader[] = L"PerfCounter(CounterName),TotalCount,Min,Max,-1Std,Mean,+1Std,-1IQR,Median,+1IQR\r\n";
            length = sizeof(DetailedHeader);
            if (!::WriteFile(file_handle, DetailedHeader, length, &written, NULL)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }
        }

        // everything succeeded, dismiss the scope guards
        closeHandleOnError.dismiss();
    }

    ctsWriteDetails::~ctsWriteDetails() NOEXCEPT
    {
        ::CloseHandle(file_handle);
    }

    void ctsWriteDetails::start_row(_In_ LPCWSTR _class_name, _In_ LPCWSTR _counter_name)
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

    void ctsWriteDetails::end_row()
    {
        static const WCHAR EOL[2] = { L'\r', L'\n' };
        static const DWORD EOL_LEN = sizeof(EOL);
        DWORD written;
        if (!::WriteFile(file_handle, EOL, EOL_LEN, &written, NULL)) {
            throw ctl::ctException(::GetLastError(), L"WriteFile", false);
        }
    }
}