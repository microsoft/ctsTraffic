/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// declaration header
#include "ctsWriteDetails.h"

// cpp headers
#include <cstdio>
#include <string>

// os headers
#include <Windows.h>

// ctl headers
#include <ctScopeGuard.hpp>
#include <ctException.hpp>
#include <ctString.hpp>


namespace ctsPerf {

    void ctsWriteDetails::create_file(bool _mean_only)
    {
        file_handle = ::CreateFileW(
            file_name.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ, // allow others to read the file while we write to it
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (INVALID_HANDLE_VALUE == file_handle) {
            throw ctl::ctException(
                ::GetLastError(),
                ctl::ctString::format_string(L"CreateFile(%ws)", file_name.c_str()).c_str(),
                true);
        }
        ctlScopeGuard(closeHandleOnError, { ::CloseHandle(file_handle); file_handle = INVALID_HANDLE_VALUE; });

        // write the UTF16 Byte order mark
        static const WCHAR BOM_UTF16 = 0xFEFF;
        DWORD length = sizeof(WCHAR);
        DWORD written;
        if (!::WriteFile(file_handle, &BOM_UTF16, length, &written, nullptr)) {
            throw ctl::ctException(::GetLastError(), L"WriteFile", false);
        }

        if (_mean_only) {
            static const WCHAR MeanHeader[] = L"PerfCounter(CounterName),SampleCount,Min,Max,Mean\r\n";
            length = sizeof MeanHeader;
            if (!::WriteFile(file_handle, MeanHeader, length, &written, nullptr)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }
        } else {
            static const WCHAR DetailedHeader[] = L"PerfCounter(CounterName),SampleCount,Min,Max,-1Std,Mean,+1Std,-1IQR,Median,+1IQR\r\n";
            length = sizeof DetailedHeader;
            if (!::WriteFile(file_handle, DetailedHeader, length, &written, nullptr)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }
        }

        // everything succeeded, dismiss the scope guards
        closeHandleOnError.dismiss();
    }
    void ctsWriteDetails::create_file(const std::wstring& _banner_text)
    {
        file_handle = ::CreateFileW(
            file_name.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ, // allow others to read the file while we write to it
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (INVALID_HANDLE_VALUE == file_handle) {
            throw ctl::ctException(
                ::GetLastError(),
                ctl::ctString::format_string(L"CreateFile(%ws)", file_name.c_str()).c_str(),
                true);
        }
        ctlScopeGuard(closeHandleOnError, { ::CloseHandle(file_handle); file_handle = INVALID_HANDLE_VALUE; });

        // write the UTF16 Byte order mark
        static const WCHAR BOM_UTF16 = 0xFEFF;
        DWORD length = sizeof(WCHAR);
        DWORD written;
        if (!::WriteFile(file_handle, &BOM_UTF16, length, &written, nullptr)) {
            throw ctl::ctException(::GetLastError(), L"WriteFile", false);
        }

        length = static_cast<DWORD>(_banner_text.length() * sizeof(WCHAR));  // NOLINT
        if (!::WriteFile(file_handle, _banner_text.c_str(), length, &written, nullptr)) {
            throw ctl::ctException(::GetLastError(), L"WriteFile", false);
        }

        end_row();

        // everything succeeded, dismiss the scope guards
        closeHandleOnError.dismiss();
    }

    void ctsWriteDetails::write_row(const std::wstring& text) const noexcept
    {
        const auto length = static_cast<DWORD>(text.length() * sizeof(WCHAR)); // NOLINT
        DWORD written;
        if (!::WriteFile(file_handle, text.c_str(), length, &written, nullptr)) {
            const auto gle = ::GetLastError();
            wprintf(L"\t[ctsWriteDetails::write_row] WriteFile failed (%u)\n", gle);
        }

        end_row();
    }

	void ctsWriteDetails::write_empty_row() const noexcept
	{
		end_row();
	}

    void ctsWriteDetails::start_row(LPCWSTR _class_name, LPCWSTR _counter_name) const noexcept
    {
        auto formatted_string(ctl::ctString::format_string(
            L"%ws (%ws)", _class_name, _counter_name));
        // since writing to csv, can't embed a comma in the data
        ctl::ctString::replace_all(formatted_string, L",", L"-");

        const auto length = static_cast<DWORD>(formatted_string.length() * sizeof(WCHAR)); // NOLINT
        DWORD written;
        if (!::WriteFile(file_handle, formatted_string.c_str(), length, &written, nullptr)) {
            const auto gle = ::GetLastError();
            wprintf(L"\t[ctsWriteDetails::start_row] WriteFile failed (%u)\n", gle);
        }
    }

    void ctsWriteDetails::end_row() const noexcept
    {
        static const WCHAR EOL[2] = { L'\r', L'\n' };
        static const DWORD EOL_LEN = sizeof EOL;
        DWORD written;
        if (!::WriteFile(file_handle, EOL, EOL_LEN, &written, nullptr)) {
            const auto gle = ::GetLastError();
            wprintf(L"\t[ctsWriteDetails::end_row] WriteFile failed (%u)\n", gle);
        }
    }
}
