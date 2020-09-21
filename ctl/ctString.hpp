/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <cstdarg>
#include <string>
// os headers
#include <Windows.h>
// wil headers
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


namespace ctl
{
    namespace ctString
    {
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// ctConvertToString
        /// ctConvertToWstring
        ///
        /// Converts between std::string and std::wstring using win32 conversion functions
        ///
        /// These use UTF8 for all conversion operations
        ///
        /// Can throw wil::ResultException on failures from the underlying conversion calls
        /// Can throw std::bad_alloc
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        inline std::string ctConvertToString(const std::wstring& wstr)
        {
            if (wstr.length() == 0)
            {
                return std::string();
            }

            int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
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

            buf.resize(len - 1); // We needed room for it earlier, but the \0 isn't considered a part of the std::string
            return buf;
        }

        inline std::wstring ctConvertToWstring(const std::string& str)
        {
            if (str.length() == 0)
            {
                return std::wstring();
            }

            int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
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

            buf.resize(len - 1); // We needed room for it earlier, but the \0 isn't considered a part of the std::string
            return buf;
        }


        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// ctOrdinalEquals
        /// ctOrdinalEqualsCaseInsensative
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
        ///    if (ctOrdinalEquals(hello, L"Hello)) { printf(L"Correct!"); }
        ///
        ///    std::wstring wshello (L"hello");
        ///    if (ctOrdinalEqualsCaseInsensative(hello, wshello)) { printf(L"Correct!"); }
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////


        // Note: wcslen(convert_to_ptr(x)) == get_string_length(x) is strictly required for any pair of
        // convert_to_ptr/get_string_length implementations, but can't be cleanly expressed in OACR annotations

        namespace Detail
        {
            inline bool OrdinalEquals(
                _In_NLS_string_(_lhs_size) const wchar_t* lhs,
                size_t lhs_size,
                _In_NLS_string_(_rhs_size) const wchar_t* rhs,
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

                    case CSTR_LESS_THAN: // fall-through
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

                    case CSTR_LESS_THAN: // fall-through
                    case CSTR_GREATER_THAN:
                        return false;

                    default:
                    {
                        THROW_WIN32_MSG(GetLastError(), "CompareStringA");
                    }
                }
            }
#else
            inline
                bool OrdinalEquals(
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
        bool ctOrdinalEquals(LeftStringT lhs, RightStringT rhs)
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
        bool ctOrdinalEqualsCaseInsensative(LeftStringT lhs, RightStringT rhs)
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
        /// ctOridinalStartsWith
        /// ctOrdinalStartsWithCaseInsensative
        /// ctOrdinalEndsWith
        /// ctOrdinalEndsWithCaseInsensative
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

        inline bool ctOridinalStartsWith(const std::wstring& haystack, const std::wstring& needle)
        {
            return
                haystack.size() >= needle.size() &&
                ctOrdinalEquals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
        }

        inline bool ctOrdinalStartsWithCaseInsensative(const std::wstring& haystack, const std::wstring& needle)
        {
            return
                haystack.size() >= needle.size() &&
                ctOrdinalEqualsCaseInsensative(haystack.substr(0, needle.size()).c_str(), needle.c_str());
        }

        inline bool ctOrdinalEndsWith(const std::wstring& haystack, const std::wstring& needle)
        {
            return
                haystack.size() >= needle.size() &&
                ctOrdinalEquals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
        }

        inline bool ctOrdinalEndsWithCaseInsensative(const std::wstring& haystack, const std::wstring& needle)
        {
            return
                haystack.size() >= needle.size() &&
                ctOrdinalEqualsCaseInsensative(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
        }

        inline bool ctOridinalStartsWith(const std::string& haystack, const std::string& needle)
        {
            return
                haystack.size() >= needle.size() &&
                ctOrdinalEquals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
        }

        inline bool ctOrdinalStartsWithCaseInsensative(const std::string& haystack, const std::string& needle)
        {
            return
                haystack.size() >= needle.size() &&
                ctOrdinalEqualsCaseInsensative(haystack.substr(0, needle.size()).c_str(), needle.c_str());
        }

        inline bool ctOrdinalEndsWith(const std::string& haystack, const std::string& needle)
        {
            return
                haystack.size() >= needle.size() &&
                ctOrdinalEquals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
        }

        inline bool ctOrdinalEndsWithCaseInsensative(const std::string& haystack, const std::string& needle)
        {
            return
                haystack.size() >= needle.size() &&
                ctOrdinalEqualsCaseInsensative(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
        }

        inline std::wstring ctFormatMessage(DWORD messageId)
        {
            constexpr DWORD cchBuffer = 1024;
            WCHAR stringBuffer[cchBuffer]{};

            // We carefully avoid using the FORMAT_MESSAGE_ALLOCATE_BUFFER flag.
            // It triggers a use of the LocalAlloc() function. LocalAlloc() and LocalFree() are in an API set that is obsolete.
            constexpr DWORD FormatMsgFlags =
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS |
                FORMAT_MESSAGE_MAX_WIDTH_MASK;

            const auto dwReturn = FormatMessageW(
                FormatMsgFlags,
                nullptr, // just search the system
                messageId,
                0, // allow for proper MUI language fallback
                stringBuffer,
                cchBuffer,
                nullptr);
            if (0 == dwReturn)
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
        inline void ctReplaceAll(std::wstring& original_string, const std::wstring& search_string, const std::wstring& replacement_string)  // NOLINT(google-runtime-references)
        {
            const auto search_size = search_string.size();
            const auto replacement_size = replacement_string.size();
            size_t index = 0;
            while ((index = original_string.find(search_string, index)) != std::wstring::npos)
            {
                original_string.replace(index, search_size, replacement_string);
                index += replacement_size;
            }
        }

        inline
            std::wstring ctReplaceAllCopy(std::wstring original_string, const std::wstring& search_string, const std::wstring& replacement_string)
        {
            ctReplaceAll(original_string, search_string, replacement_string);
            return original_string;
        }

        inline void ctReplaceAll(std::string& original_string, const std::string& search_string, const std::string& replacement_string)  // NOLINT(google-runtime-references)
        {
            const auto search_size = search_string.size();
            const auto replacement_size = replacement_string.size();
            size_t index = 0;
            while ((index = original_string.find(search_string, index)) != std::string::npos)
            {
                original_string.replace(index, search_size, replacement_string);
                index += replacement_size;
            }
        }

        inline std::string ctReplaceAllCopy(std::string original_string, const std::string& search_string, const std::string& replacement_string)
        {
            ctReplaceAll(original_string, search_string, replacement_string);
            return original_string;
        }
    } // namespace ctString
} // namespace ctl
