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
#include <string>
// os headers
#include <Windows.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include <ctString.hpp>


namespace ctsPerf {

    void ctsWriteDetails::create_file(bool mean_only)
    {
        m_fileHandle.reset(::CreateFileW(
            m_fileName.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ, // allow others to read the file while we write to it
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        THROW_LAST_ERROR_IF_NULL(m_fileHandle.get());

        // write the UTF16 Byte order mark
        constexpr WCHAR bom_utf16 = 0xFEFF;
        DWORD length = sizeof(WCHAR);
        DWORD written;
        THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), &bom_utf16, length, &written, nullptr));

        if (mean_only) {
            constexpr WCHAR meanHeader[] = L"PerfCounter(CounterName),SampleCount,Min,Max,Mean\r\n";
            length = sizeof meanHeader;
            THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), meanHeader, length, &written, nullptr));
        }
        else
        {
            constexpr WCHAR detailedHeader[] = L"PerfCounter(CounterName),SampleCount,Min,Max,-1Std,Mean,+1Std,-1IQR,Median,+1IQR\r\n";
            length = sizeof detailedHeader;
            THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), detailedHeader, length, &written, nullptr));
        }
    }
    void ctsWriteDetails::create_file(const std::wstring& banner_text)
    {
        m_fileHandle.reset(::CreateFileW(
            m_fileName.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ, // allow others to read the file while we write to it
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        THROW_LAST_ERROR_IF_NULL(m_fileHandle.get());

        // write the UTF16 Byte order mark
        constexpr WCHAR bom_utf16 = 0xFEFF;
        constexpr DWORD bomLength{ sizeof(WCHAR) };
        DWORD written{};
        THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), &bom_utf16, bomLength, &written, nullptr));

        const DWORD bannerLength{ static_cast<DWORD>(banner_text.length() * sizeof(WCHAR)) };
        THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), banner_text.c_str(), bannerLength, &written, nullptr));

        end_row();
    }

    void ctsWriteDetails::write_row(const std::wstring& text) const noexcept
    {
        const DWORD length{ static_cast<DWORD>(text.length() * sizeof(WCHAR)) };
        DWORD written{};
        if (!::WriteFile(m_fileHandle.get(), text.c_str(), length, &written, nullptr)) {
            const auto gle = ::GetLastError();
            wprintf(L"\t[ctsWriteDetails::write_row] WriteFile failed (%u)\n", gle);
        }

        end_row();
    }

    void ctsWriteDetails::write_empty_row() const noexcept
    {
        end_row();
    }

    void ctsWriteDetails::start_row(PCWSTR class_name, PCWSTR counter_name) const noexcept
    try
    {
        auto formatted_string(ctl::ctString::ctFormatString(
            L"%ws (%ws)", class_name, counter_name));
        // since writing to csv, can't embed a comma in the data
        ctl::ctString::ctReplaceAll(formatted_string, L",", L"-");

        const DWORD length{ static_cast<DWORD>(formatted_string.length() * sizeof(WCHAR)) };
        DWORD written{};
        if (!::WriteFile(m_fileHandle.get(), formatted_string.c_str(), length, &written, nullptr)) {
            const auto gle = ::GetLastError();
            wprintf(L"\t[ctsWriteDetails::start_row] WriteFile failed (%u)\n", gle);
        }
    }
    CATCH_LOG()

    void ctsWriteDetails::end_row() const noexcept
    {
        constexpr WCHAR c_Eol[2]{ L'\r', L'\n' };
        constexpr DWORD c_EolLen{ sizeof c_Eol };
        DWORD written{};
        if (!::WriteFile(m_fileHandle.get(), c_Eol, c_EolLen, &written, nullptr)) {
            const auto gle = ::GetLastError();
            wprintf(L"\t[ctsWriteDetails::end_row] WriteFile failed (%u)\n", gle);
        }
    }
}
