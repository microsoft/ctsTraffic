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
#include <wil/stl.h>
#include <wil/resource.h>
// ctl headers
#include <ctString.hpp>


namespace ctsPerf
{
void ctsWriteDetails::CreateFile(bool meanOnly)
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
    constexpr WCHAR bomUtf16 = 0xFEFF;
    DWORD length = sizeof(WCHAR);
    DWORD written;
    THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), &bomUtf16, length, &written, nullptr));

    if (meanOnly)
    {
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

void ctsWriteDetails::CreateFile(const std::wstring& bannerText)
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
    constexpr WCHAR bomUtf16 = 0xFEFF;
    constexpr DWORD bomLength{sizeof(WCHAR)};
    DWORD written{};
    THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), &bomUtf16, bomLength, &written, nullptr));

    const auto bannerLength{static_cast<DWORD>((bannerText.length() * sizeof(WCHAR)))};
    THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), bannerText.c_str(), bannerLength, &written, nullptr));

    EndRow();
}

void ctsWriteDetails::WriteRow(const std::wstring& text) const noexcept
{
    const auto length{static_cast<DWORD>((text.length() * sizeof(WCHAR)))};
    DWORD written{};
    if (!WriteFile(m_fileHandle.get(), text.c_str(), length, &written, nullptr))
    {
        const auto gle = GetLastError();
        wprintf(L"\t[ctsWriteDetails::write_row] WriteFile failed (%u)\n", gle);
    }

    EndRow();
}

void ctsWriteDetails::WriteEmptyRow() const noexcept
{
    EndRow();
}

void ctsWriteDetails::StartRow(_In_ PCWSTR className, _In_ PCWSTR counterName) const noexcept try
{
    auto formattedString(wil::str_printf<std::wstring>(
        L"%ws (%ws)", className, counterName));
    // since writing to csv, can't embed a comma in the data
    ctl::ctString::replace_all(formattedString, L",", L"-");

    const auto length{static_cast<DWORD>((formattedString.length() * sizeof(WCHAR)))};
    DWORD written{};
    if (!WriteFile(m_fileHandle.get(), formattedString.c_str(), length, &written, nullptr))
    {
        const auto gle = GetLastError();
        wprintf(L"\t[ctsWriteDetails::start_row] WriteFile failed (%u)\n", gle);
    }
}
CATCH_LOG()

void ctsWriteDetails::EndRow() const noexcept
{
    constexpr WCHAR endOfLine[2]{L'\r', L'\n'};
    constexpr DWORD endOfLineLength{sizeof endOfLine};
    DWORD written{};
    if (!WriteFile(m_fileHandle.get(), endOfLine, endOfLineLength, &written, nullptr))
    {
        const auto gle = GetLastError();
        wprintf(L"\t[ctsWriteDetails::end_row] WriteFile failed (%u)\n", gle);
    }
}
}
