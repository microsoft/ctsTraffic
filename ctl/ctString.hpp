/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// ReSharper disable CppInconsistentNaming
#pragma once

// cpp headers
#include <cstdarg>
#include <string>
// os headers
#include <Windows.h>
// wil headers
#include <wil/win32_helpers.h>
#include <wil/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
///
///
/// String parsing and manipulation functions to enable faster, more reliable development
///
/// Notice all functions are in the ctl::ctString namespace
///
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ctl::ctString
{
////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// convert_to_string
/// convert_to_wstring
///
/// Converts between std::string and std::wstring using win32 conversion functions
///
/// These use UTF8 for all conversion operations
///
/// Can throw wil::ResultException on failures from the underlying conversion calls
/// Can throw std::bad_alloc
///
////////////////////////////////////////////////////////////////////////////////////////////////////
inline std::string convert_to_string(const std::wstring& wstr)
{
    if (wstr.length() == 0)
    {
        return {};
    }

    auto len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len == 0)
    {
        THROW_WIN32_MSG(GetLastError(), "WideCharToMultiByte");
    }

    std::string buf(len, '\0');
    len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &buf[0], len, nullptr, nullptr);
    if (len == 0)
    {
        THROW_WIN32_MSG(GetLastError(), "WideCharToMultiByte");
    }

    // We needed room for it earlier, but the \0 cannot be embedded in the returned std::string
    buf.resize(len - 1);
    return buf;
}

inline std::wstring convert_to_wstring(const std::string& str)
{
    if (str.length() == 0)
    {
        return {};
    }

    auto len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len == 0)
    {
        THROW_WIN32_MSG(GetLastError(), "MultiByteToWideChar");
    }

    std::wstring buf(len, L'\0');
    len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &buf[0], len);
    if (len == 0)
    {
        THROW_WIN32_MSG(GetLastError(), "MultiByteToWideChar");
    }

    // We needed room for it earlier, but the \0 cannot be embedded in the returned std::string
    buf.resize(len - 1);
    return buf;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// ordinal_equals
/// iordinal_equals
///
/// Performs Ordinal comparisons of 2 strings, returning a bool if they compare equally.
/// *Note the 'i' version is a case-insensative comparison
///
/// Ordinal comparisons are desired when you want "binary equality". Examples include:
/// - want to find a system resource (file, directory, registry key)
/// - want to sort consistently regardless of user locale
/// - want to compare against a value *you* control that's not affected by user locale
///
/// Can throw a wil::ResultException if the Win32 API fails
///
/// Examples:
///
///    wchar_t hello[] = L"Hello";
///    if (ordinal_equals(hello, L"Hello)) { printf(L"Correct!"); }
///
///    std::wstring wshello (L"hello");
///    if (iordinal_equals(hello, wshello)) { printf(L"Correct!"); }
///
////////////////////////////////////////////////////////////////////////////////////////////////////


// Note: wcslen(convert_to_ptr(x)) == get_string_length(x) is strictly required for any pair of
// convert_to_ptr/get_string_length implementations, but can't be cleanly expressed in OACR annotations

namespace Detail
{
    inline bool OrdinalEquals(
        _In_NLS_string_(lhs_size) const wchar_t* lhs,
        size_t lhs_size,
        _In_NLS_string_(rhs_size) const wchar_t* rhs,
        size_t rhs_size,
        BOOL case_insensitive)
    {
        switch (CompareStringOrdinal(
            lhs,
            static_cast<int>(lhs_size),
            rhs,
            static_cast<int>(rhs_size),
            case_insensitive))
        {
            case CSTR_EQUAL:
                return true;

            case CSTR_LESS_THAN:
                [[fallthrough]];
            case CSTR_GREATER_THAN:
                return false;

            default:
            {
                THROW_WIN32_MSG(GetLastError(), "CompareStringOrdinal");
            }
        }
    }

#pragma region Desktop Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    // CompareStringA is not available for Modern Apps
    inline bool OrdinalEquals(
        _In_reads_z_(lhs_size) const char* lhs,
        size_t lhs_size,
        _In_reads_z_(rhs_size) const char* rhs,
        size_t rhs_size,
        BOOL case_insensitive)
    {
        switch (CompareStringA(
            LOCALE_INVARIANT,
            case_insensitive ? NORM_IGNORECASE : 0,
            lhs,
            static_cast<int>(lhs_size),
            rhs,
            static_cast<int>(rhs_size)))
        {
            case CSTR_EQUAL:
                return true;

            case CSTR_LESS_THAN:
                [[fallthrough]];
            case CSTR_GREATER_THAN:
                return false;

            default:
            {
                THROW_WIN32_MSG(GetLastError(), "CompareStringA");
            }
        }
    }
#else
        inline bool OrdinalEquals(
            _In_reads_z_(lhs_size) const char* lhs,
            size_t lhs_size,
            _In_reads_z_(rhs_size) const char* rhs,
            size_t rhs_size,
            BOOL case_insensitive)
        {
            UNREFERENCED_PARAMETER(_lhs);
            UNREFERENCED_PARAMETER(_lhs_size);
            UNREFERENCED_PARAMETER(_rhs);
            UNREFERENCED_PARAMETER(_rhs_size);
            UNREFERENCED_PARAMETER(_case_insensitive);

            FAIL_FAST_MSG("ctString: cannot compare char* strings in modern apps: CompareStringA is not supported");
        }
#endif /* WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP) */
#pragma endregion

    inline _Ret_z_ const wchar_t* ConvertToPtr(const std::wstring& source) noexcept
    {
        return source.c_str();
    }

    inline size_t GetStringLength(const std::wstring& source) noexcept
    {
        return source.length();
    }

    inline _Ret_z_ const wchar_t* ConvertToPtr(_In_z_ const wchar_t* source) noexcept
    {
        return source;
    }

    inline size_t GetStringLength(_In_z_ const wchar_t* source) noexcept
    {
        return wcslen(source);
    }

    inline _Ret_z_ const char* ConvertToPtr(const std::string& source) noexcept
    {
        return source.c_str();
    }

    inline size_t GetStringLength(const std::string& source) noexcept
    {
        return source.length();
    }

    inline _Ret_z_ const char* ConvertToPtr(_In_z_ const char* source) noexcept
    {
        return source;
    }

    inline size_t GetStringLength(_In_z_ const char* source) noexcept
    {
        return strlen(source);
    }
}

template <typename LeftStringT, typename RightStringT>
bool ordinal_equals(LeftStringT lhs, RightStringT rhs)
{
#pragma prefast(suppress:26018, "OACR doesn't offer a way to annotate the requirement that wcslen(convert_to_ptr(x)) == get_string_length(x)")
    return Detail::OrdinalEquals(
        Detail::ConvertToPtr(lhs),
        Detail::GetStringLength(lhs),
        Detail::ConvertToPtr(rhs),
        Detail::GetStringLength(rhs),
        FALSE);
}

template <typename LeftStringT, typename RightStringT>
bool iordinal_equals(LeftStringT lhs, RightStringT rhs)
{
#pragma prefast(suppress:26018, "OACR doesn't offer a way to annotate the requirement that wcslen(convert_to_ptr(x)) == get_string_length(x)")
    return Detail::OrdinalEquals(
        Detail::ConvertToPtr(lhs),
        Detail::GetStringLength(lhs),
        Detail::ConvertToPtr(rhs),
        Detail::GetStringLength(rhs),
        TRUE);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// starts_with
/// istarts_with
/// ends_with
/// iends_with
///
/// Searches the relevant portion of the input string for the search string, returning bool
///
/// Most useful in combination with std::bind as a predicate to find_if and its friends
///
/// The "i" versions perform case-insensitive (but *NOT* locale-sensitive) searches.
/// The non-i versions perform exact character comparison - case-sensitive, locale-insensitive
///
/// *NOT* nothrow
///
////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool starts_with(const std::wstring& haystack, const std::wstring& needle)
{
    return
        haystack.size() >= needle.size() &&
        ordinal_equals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
}

inline bool istarts_with(const std::wstring& haystack, const std::wstring& needle)
{
    return
        haystack.size() >= needle.size() &&
        iordinal_equals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
}

inline bool ends_with(const std::wstring& haystack, const std::wstring& needle)
{
    return
        haystack.size() >= needle.size() &&
        ordinal_equals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
}

inline bool iends_with(const std::wstring& haystack, const std::wstring& needle)
{
    return
        haystack.size() >= needle.size() &&
        iordinal_equals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
}

inline bool starts_with(const std::string& haystack, const std::string& needle)
{
    return
        haystack.size() >= needle.size() &&
        ordinal_equals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
}

inline bool istarts_with(const std::string& haystack, const std::string& needle)
{
    return
        haystack.size() >= needle.size() &&
        iordinal_equals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
}

inline bool ends_with(const std::string& haystack, const std::string& needle)
{
    return
        haystack.size() >= needle.size() &&
        ordinal_equals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
}

inline bool iends_with(const std::string& haystack, const std::string& needle)
{
    return
        haystack.size() >= needle.size() &&
        iordinal_equals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
}

inline std::wstring format_message(DWORD messageId)
{
    constexpr DWORD cchBuffer = 1024;
    WCHAR stringBuffer[cchBuffer]{};

    // We carefully avoid using the FORMAT_MESSAGE_ALLOCATE_BUFFER flag.
    // It triggers a use of the LocalAlloc() function. LocalAlloc() and LocalFree() are in an API set that is obsolete.
    constexpr DWORD formatMsgFlags =
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_MAX_WIDTH_MASK;

    if (FormatMessageW(
            formatMsgFlags,
            nullptr, // just search the system
            messageId,
            0, // allow for proper MUI language fallback
            stringBuffer,
            cchBuffer,
            nullptr) == 0)
    {
        return {};
    }
    return stringBuffer;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// replace_all
/// replace_all_copy
///
/// Performs a find/replace of a specific character sequence.
///
/// *Note this is an exact character comparison - case-sensitive without respect for locale
///
/// *Note key differences between the 2 functions:
///
/// - replace_all takes an original string as an std::wstring reference.
///   Meaning an implicit std::wstring object cannot be created.
///
///   This is not valid:
///     wchar_t hello[] = L"hello";
///     repalce_all(hello, L"h", L"j");
///
///   This is valid:
///     std::wstring hello(L"hello");
///     repalce_all(hello, L"h", L"j");
/// 
/// - replace_all_copy takes an original string by value into std::wstring.
///   Meaning an implicit std::wstring object *can* be created.
///   Additionally, R-value references can be passed through it via std::move semantics.
///   A new std::wstring is returned with the escaped string.
///
///   This is now valid:
///     wchar_t hello[] = L"hello";
///     std::wstring jello = repalce_all_copy(hello, L"h", L"j");
///
///   This is still valid:
///     std::wstring hello(L"hello");
///     std::wstring jello = repalce_all_copy(hello, L"h", L"j");
///
///   This is also valid - will pass down as an R-Value to avoid any copies:
///     std::wstring hello(L"ello");
///     std::wstring jello = repalce_all_copy(L"h" + hello, L"h", L"j");
///
///
/// Can throw a std::exception under low-resources
///
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void replace_all(std::wstring& originalString, const std::wstring& searchString, const std::wstring& replacementString) // NOLINT(google-runtime-references)
{
    const auto searchSize = searchString.size();
    const auto replacementSize = replacementString.size();
    size_t index = 0;
    while ((index = originalString.find(searchString, index)) != std::wstring::npos)
    {
        originalString.replace(index, searchSize, replacementString);
        index += replacementSize;
    }
}

inline
std::wstring replace_all_copy(std::wstring originalString, const std::wstring& searchString, const std::wstring& replacementString)
{
    replace_all(originalString, searchString, replacementString);
    return originalString;
}

inline void replace_all(std::string& originalString, const std::string& searchString, const std::string& replacementString) // NOLINT(google-runtime-references)
{
    const auto searchSize = searchString.size();
    const auto replacementSize = replacementString.size();
    size_t index = 0;
    while ((index = originalString.find(searchString, index)) != std::string::npos)
    {
        originalString.replace(index, searchSize, replacementString);
        index += replacementSize;
    }
}

inline std::string replace_all_copy(std::string originalString, const std::string& searchString, const std::string& replacementString)
{
    replace_all(originalString, searchString, replacementString);
    return originalString;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// escape_wmi_query
/// escape_wmi_query_copy
///
/// Escapes characters that are 'special' in the context of a WMI WQL query which could
///  inadvertently affect the result of the query.
///
/// - escape_wmi_query takes an original string as an std::wstring reference.
///   Meaning an implicit std::wstring object cannot be created.
///
/// - escape_wmi_query_copy takes an original string by value into std::wstring.
///   Meaning an implicit std::wstring object *can* be created.
///   Additionally, R-value references can be passed through it via std::move semantics.
///   A new std::wstring is returned with the escaped string.
///
/// Can throw a std::exception under low-resources
///
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void escape_wmi_query(std::wstring& unescapedString)
{
    if (unescapedString.size() > 1)
    {
        // greater than one as we need begin *and* end quotes before trimming
        if (*unescapedString.begin() == L'\'' && *unescapedString.rbegin() == L'\'' ||
            *unescapedString.begin() == L'"' && *unescapedString.rbegin() == L'"')
        {
            // trim off single quotes or double before replacing
            unescapedString.erase(unescapedString.begin());
            unescapedString.pop_back();
        }
    }
    replace_all(unescapedString, L"\\", L"\\\\");
    replace_all(unescapedString, L"'", L"\\'");
    unescapedString.insert(unescapedString.begin(), L'\'');
    unescapedString.push_back(L'\'');
}

inline std::wstring escape_wmi_query_copy(std::wstring unescapedString)
{
    escape_wmi_query(unescapedString);
    return unescapedString;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// format_string
/// format_string_va
///
/// Creates a formatted string for the caller - growing the buffer for _vsnwprintf_s
/// Returns a std::wstring with the resulting formatted string
///
/// Will continue to grow the formatted string up to MAXINT32 characters to avoid truncation
///  to ensure the entire formatted string is captured
///
/// Can throw a std::exception under low-resources
///
////////////////////////////////////////////////////////////////////////////////////////////////////
inline
std::wstring __cdecl format_string_va(_In_ _Printf_format_string_ PCWSTR pszFormat, va_list args)
{
    std::wstring formattedString;
    THROW_IF_FAILED(wil::details::str_vprintf_nothrow<std::wstring>(formattedString, pszFormat, args));
    return formattedString;
}

inline
std::wstring __cdecl format_string(_In_ _Printf_format_string_ PCWSTR pszFormat, ...)
{
    std::wstring formattedString;
    va_list args;
    va_start(args, pszFormat);
    const auto hr = wil::details::str_vprintf_nothrow<std::wstring>(formattedString, pszFormat, args);
    va_end(args);
    THROW_IF_FAILED(hr);
    return formattedString;
}
} // namespace ctl::ctString
