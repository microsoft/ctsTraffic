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
#include <vector>
#include <string>
#include <algorithm>
#include <utility>
// os headers
#include <windows.h>
// ctl headers
#include "ctException.hpp"


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
		/// all_indices_of() returns a vectors of iterators into the [in] _str string-type
		/// - where string-type can be navigated with const-forward-iterators
		/// ---  examples include:
		///      std::wstring ws(L"hello");
		///      std::string s("hello");
		///      WCHAR* wsz = L"hello";
		///      char ar[] = "hello";
		///
		/// - matching the 'tokens' (characters) as defined by the function F 
		///
		/// F must be a function-type (standalone function, functor object, lambda, std::function)
		/// - which has the prototype:  bool f(wchar_t) or f(char)
		///
		/// Can throw std::bad_alloc() under low-resource conditions
		/// Maintains a strong-exception guarantee
		/// - in this case, meaning the [in] wstring will never be modified
		///
		///
		/// Example:
		///
		///     std::wstring test_string(L"The quick brown fox jumps over the lazy dog");
		///
		///     // Example showing inline lambda instead of a separately defined function/functor
		///     auto spaces = ctl::str::all_indices_of(
		///         test_string.begin(), test_string.end(), 
		///         [](wchar_t _ch) -> bool { return _ch == L' '; });
		///
		///     std::wstring::const_iterator beginning_of_word  = std::begin(test_string);
		///     std::for_each( std::begin(spaces), std::end(spaces), [&](std::wstring::const_iterator _iter) {
		///         std::wstring next_word(beginning_of_word, _iter);
		///         wprintf(L"%ws\n", next_word.c_str());
		///         // _iter points to the space - move to the first character of the next word
		///         beginning_of_word = _iter;
		///         ++beginning_of_word;
		///     } );
		///     // beginning_of_word now points to the first character of the last word (it was incremented in for_each)
		///     // - now need to print the last word (up to std::end(test_string))
		///     // - in this case we know beginning_of_word != begin() since there are spaces in the test_string
		///     //   but showing the general case method one might process the returned data
		///     std::wstring next_word(beginning_of_word, std::end(test_string));
		///     wprintf(L"%ws\n", next_word.c_str());
		///
		////////////////////////////////////////////////////////////////////////////////////////////////////

		template <typename It, typename F>
		std::vector<It> all_indices_of(It _it_begin, It _it_end, F _f)
		{
			std::vector<It> return_iterators;

			auto string_iterator = _it_begin;
			auto string_end = _it_end;
			while (string_iterator != string_end) {
				string_iterator = std::find_if(string_iterator, string_end, _f);
				if (string_iterator != string_end) {
					return_iterators.push_back(string_iterator);
					++string_iterator;
				}
			}

			return return_iterators;
		}


		////////////////////////////////////////////////////////////////////////////////////////////////////
		///
		/// convert_to_string
		/// convert_to_wstring
		///
		/// Converts between std::string and std::wstring using win32 conversion functions
		///
		/// These use UTF8 for all conversion operations
		///
		/// Can throw ctl::ctException on failures from the underlying conversion calls
		/// Can throw std::bad_alloc
		///
		////////////////////////////////////////////////////////////////////////////////////////////////////
		inline
		std::string convert_to_string(const std::wstring& _wstr)
		{
			if (_wstr.length() == 0) {
				return std::string();
			}

			int len = ::WideCharToMultiByte(CP_UTF8, 0, _wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
			if (len == 0) {
				throw ctException(::GetLastError(), L"::WideCharToMultiByte", L"ctl::ctString::convert_to_string", false);
			}

			std::string buf(len, '\0');
			len = ::WideCharToMultiByte(CP_UTF8, 0, _wstr.c_str(), -1, &buf[0], len, nullptr, nullptr);
			if (len == 0) {
				throw ctException(::GetLastError(), L"::WideCharToMultiByte", L"ctl::ctString::convert_to_string", false);
			}

			buf.resize(len - 1); // We needed room for it earlier, but the \0 isn't considered a part of the std::string
			return buf;
		}

		inline
		std::wstring convert_to_wstring(const std::string& _str)
		{
			if (_str.length() == 0) {
				return std::wstring();
			}

			int len = ::MultiByteToWideChar(CP_UTF8, 0, _str.c_str(), -1, nullptr, 0);
			if (len == 0) {
				throw ctException(::GetLastError(), L"::MultiByteToWideChar", L"ctl::ctString::convert_to_wstring", false);
			}

			std::wstring buf(len, L'\0');
			len = ::MultiByteToWideChar(CP_UTF8, 0, _str.c_str(), -1, &buf[0], len);
			if (len == 0) {
				throw ctException(::GetLastError(), L"::MultiByteToWideChar", L"ctl::ctString::convert_to_wstring", false);
			}

			buf.resize(len - 1); // We needed room for it earlier, but the \0 isn't considered a part of the std::string
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
		/// Can throw a ctl::ctException if the Win32 API fails
		///
		/// Examples:
		///
		///    wchar_t hello[] = L"Hello";
		///    if (ordinal_equals(hello, L"Hello)) { printf(L"Correct!"); }
		///
		///    std::wstring wshello (L"hello");
		///    if (iordinal_equals(hello, wshello)) { printf(L"Correct!"); }
		///
		///    ctl::ctComBstr comhello (L"HELLO");
		///    if (!ordinal_equals(wshello, comhello)) { printf(L"Correct!"); }
		///
		////////////////////////////////////////////////////////////////////////////////////////////////////


		// These helpers are in a named namespace so that ctComBStr can define its own version in ctComInitialize.hpp
		//
		// Note: wcslen(convert_to_ptr(x)) == get_string_length(x) is strictly required for any pair of
		// convert_to_ptr/get_string_length implementations, but can't be cleanly expressed in OACR annotations

		namespace _detail
		{
			///
			/// hidden implementation which all variations call
			/// - different containers have different sources of information for their length values
			///
			inline bool impl_ordinal_equals(
				_In_NLS_string_(_lhs_size) const wchar_t* _lhs,
				size_t _lhs_size,
				_In_NLS_string_(_rhs_size) const wchar_t* _rhs,
				size_t _rhs_size,
				BOOL _case_insensitive)
			{
				switch (::CompareStringOrdinal(
					_lhs,
					static_cast<int>(_lhs_size),
					_rhs,
					static_cast<int>(_rhs_size),
					_case_insensitive))
				{
				case CSTR_EQUAL:
					return true;

				case CSTR_LESS_THAN: // fall-through
				case CSTR_GREATER_THAN:
					return false;

				default:
					throw ctException(::GetLastError(), L"CompareStringOrdinal", L"ctl::ctString::impl_ordinal_equals", false);
				}
			}

#pragma region Desktop Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
			// CompareStringA is not available for Modern Apps
			inline bool impl_ordinal_equals(
				_In_reads_z_(_lhs_size) const char* _lhs,
				size_t _lhs_size,
				_In_reads_z_(_rhs_size) const char* _rhs,
				size_t _rhs_size,
				BOOL _case_insensitive)
			{
				switch (::CompareStringA(
					LOCALE_INVARIANT,
					(_case_insensitive) ? NORM_IGNORECASE : 0,
					_lhs,
					static_cast<int>(_lhs_size),
					_rhs,
					static_cast<int>(_rhs_size)))
				{
				case CSTR_EQUAL:
					return true;

				case CSTR_LESS_THAN: // fall-through
				case CSTR_GREATER_THAN:
					return false;

				default:
					throw ctException(::GetLastError(), L"CompareString", L"ctl::ctString::impl_ordinal_equals", false);
				}
			}
#else
            inline
            bool impl_ordinal_equals(
                _In_reads_z_(_lhs_size) const char* _lhs,
                size_t _lhs_size,
                _In_reads_z_(_rhs_size) const char* _rhs,
                size_t _rhs_size,
                BOOL _case_insensitive)
            {
                UNREFERENCED_PARAMETER(_lhs);
                UNREFERENCED_PARAMETER(_lhs_size);
                UNREFERENCED_PARAMETER(_rhs);
                UNREFERENCED_PARAMETER(_rhs_size);
                UNREFERENCED_PARAMETER(_case_insensitive);

                ctl::ctAlwaysFatalCondition(L"ctString: cannot compare char* strings in modern apps: CompareStringA is not supported");
            }
#endif /* WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP) */
#pragma endregion

			inline _Ret_z_ const wchar_t* convert_to_ptr(const std::wstring& source) noexcept
			{
				return source.c_str();
			}

			inline size_t get_string_length(const std::wstring& source) noexcept
			{
				return source.length();
			}

			inline _Ret_z_ const wchar_t* convert_to_ptr(_In_z_ const wchar_t* source) noexcept
			{
				return source;
			}

			inline size_t get_string_length(_In_z_ const wchar_t* source) noexcept
			{
				return wcslen(source);
			}

			inline _Ret_z_ const char* convert_to_ptr(const std::string& source) noexcept
			{
				return source.c_str();
			}

			inline size_t get_string_length(const std::string& source) noexcept
			{
				return source.length();
			}

			inline _Ret_z_ const char* convert_to_ptr(_In_z_ const char* source) noexcept
			{
				return source;
			}

			inline size_t get_string_length(_In_z_ const char* source) noexcept
			{
				return strlen(source);
			}
		}

		template <typename LeftStringT, typename RightStringT>
		bool ordinal_equals(LeftStringT _lhs, RightStringT _rhs)
		{
#pragma prefast(suppress:26018, "OACR doesn't offer a way to annotate the requirement that wcslen(convert_to_ptr(x)) == get_string_length(x)")
			return _detail::impl_ordinal_equals(
				_detail::convert_to_ptr(_lhs),
				_detail::get_string_length(_lhs),
				_detail::convert_to_ptr(_rhs),
				_detail::get_string_length(_rhs),
				FALSE);
		}

		template <typename LeftStringT, typename RightStringT>
		bool iordinal_equals(LeftStringT _lhs, RightStringT _rhs)
		{
#pragma prefast(suppress:26018, "OACR doesn't offer a way to annotate the requirement that wcslen(convert_to_ptr(x)) == get_string_length(x)")
			return _detail::impl_ordinal_equals(
				_detail::convert_to_ptr(_lhs),
				_detail::get_string_length(_lhs),
				_detail::convert_to_ptr(_rhs),
				_detail::get_string_length(_rhs),
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

		inline
		bool starts_with(const std::wstring& haystack, const std::wstring& needle)
		{
			return
				(haystack.size() >= needle.size()) &&
				ordinal_equals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
		}

		inline
		bool istarts_with(const std::wstring& haystack, const std::wstring& needle)
		{
			return
				(haystack.size() >= needle.size()) &&
				iordinal_equals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
		}

		inline
		bool ends_with(const std::wstring& haystack, const std::wstring& needle)
		{
			return
				(haystack.size() >= needle.size()) &&
				ordinal_equals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
		}

		inline
		bool iends_with(const std::wstring& haystack, const std::wstring& needle)
		{
			return
				(haystack.size() >= needle.size()) &&
				iordinal_equals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
		}

		inline
		bool starts_with(const std::string& haystack, const std::string& needle)
		{
			return
				(haystack.size() >= needle.size()) &&
				ordinal_equals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
		}

		inline
		bool istarts_with(const std::string& haystack, const std::string& needle)
		{
			return
				(haystack.size() >= needle.size()) &&
				iordinal_equals(haystack.substr(0, needle.size()).c_str(), needle.c_str());
		}

		inline
		bool ends_with(const std::string& haystack, const std::string& needle)
		{
			return
				(haystack.size() >= needle.size()) &&
				ordinal_equals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
		}

		inline
		bool iends_with(const std::string& haystack, const std::string& needle)
		{
			return
				(haystack.size() >= needle.size()) &&
				iordinal_equals(haystack.c_str() + (haystack.size() - needle.size()), needle.c_str());
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
		inline
		void replace_all(std::wstring& _original_string, const std::wstring& _search_string, const std::wstring& _replacement_string)
		{
			const auto search_size = _search_string.size();
			const auto replacement_size = _replacement_string.size();
			size_t index = 0;
			while ((index = _original_string.find(_search_string, index)) != std::wstring::npos) {
				_original_string.replace(index, search_size, _replacement_string);
				index += replacement_size;
			}
		}

		inline
		std::wstring replace_all_copy(std::wstring _original_string, const std::wstring& _search_string, const std::wstring& _replacement_string)
		{
			replace_all(_original_string, _search_string, _replacement_string);
			return _original_string;
		}

		inline
		void replace_all(std::string& _original_string, const std::string& _search_string, const std::string& _replacement_string)
		{
			const auto search_size = _search_string.size();
			const auto replacement_size = _replacement_string.size();
			size_t index = 0;
			while ((index = _original_string.find(_search_string, index)) != std::string::npos) {
				_original_string.replace(index, search_size, _replacement_string);
				index += replacement_size;
			}
		}

		inline
		std::string replace_all_copy(std::string _original_string, const std::string& _search_string, const std::string& _replacement_string)
		{
			replace_all(_original_string, _search_string, _replacement_string);
			return _original_string;
		}


		////////////////////////////////////////////////////////////////////////////////////////////////////
		///
		/// escape_wmi_query
		/// escape_wmi_query_copy
		///
		/// Escapes characters that are 'special' in the context of a WMI WQL query which could
		///  inadvertently affect the result of the query.
		///
		/// *Note the key differences between the 2 functions are identical to the differences 
		///   with ctl::ctString::replace_all and ctl::ctString::replace_all_copy.
		///   More examples are provided with those functions.
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
		inline
		void escape_wmi_query(std::wstring& _unescaped_string)
		{
			if (_unescaped_string.size() > 1) {
				// greater than one as we need begin *and* end quotes before trimming
				if (((*_unescaped_string.begin() == L'\'') && (*_unescaped_string.rbegin() == L'\'')) ||
					((*_unescaped_string.begin() == L'"') && (*_unescaped_string.rbegin() == L'"'))) {
					// trim off single quotes or double before replacing
					_unescaped_string.erase(_unescaped_string.begin());
					_unescaped_string.pop_back();
				}
			}
			replace_all(_unescaped_string, L"\\", L"\\\\");
			replace_all(_unescaped_string, L"'", L"\\'");
			_unescaped_string.insert(_unescaped_string.begin(), L'\'');
			_unescaped_string.push_back(L'\'');
		}

		inline
		std::wstring escape_wmi_query_copy(std::wstring _unescaped_string)
		{
			escape_wmi_query(_unescaped_string);
			return _unescaped_string;
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
		/// Can throw std::bad_alloc
		///
		////////////////////////////////////////////////////////////////////////////////////////////////////
		inline
		std::wstring __cdecl format_string_va(_Printf_format_string_ LPCWSTR _format_string, va_list _args)
		{
			std::wstring formatted_string(_format_string);
			// loop until the formatted string will fit
			for (;;) {
				const auto new_size = _vsnwprintf_s(
					&formatted_string[0],
					formatted_string.size(),
					_TRUNCATE,
					_format_string,
					_args);

				if (new_size != -1) {
					// strip off the null-terminator at the end
					formatted_string.resize(new_size);
					break;
				}

				auto size_to_grow = static_cast<size_t>(formatted_string.size() * 1.5);
				// overflow comparison so we don't overflow MAXINT32
				if (size_to_grow > (MAXINT32 - formatted_string.size())) {
					// can't grow any larger - take off the null at the end wherever it is
					formatted_string.resize(formatted_string.find(L'\0'));
					break;
				}

				if (size_to_grow < 64) {
					size_to_grow = formatted_string.size() + 64;
				} else {
					size_to_grow = formatted_string.size() + size_to_grow;
				}
				formatted_string.resize(size_to_grow, L'\0');
			}
			return formatted_string;
		}

		inline
		std::wstring __cdecl format_string(_Printf_format_string_ LPCWSTR _format_string, ...)
		{
			va_list args;
			va_start(args, _format_string);
			std::wstring formatted_string(format_string_va(_format_string, args));
			// ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
			va_end(args);
			return formatted_string;
		}

		////////////////////////////////////////////////////////////////////////////////////////////////////
		///
		/// format_exception
		///
		/// Returns a std::wstring of the formatted exception
		/// Currently formats std::exception and ctl::ctException instances
		///
		/// If enabled Runtime Type Information, will detect which exception based off the actual type
		/// - this is the /GR flag to the compiler
		/// - in the sources makefile: USE_RTTI = 1
		///
		////////////////////////////////////////////////////////////////////////////////////////////////////
		inline
		std::wstring format_exception(const ctException& _exception)
		{
			const auto what_exists = wcslen(_exception.what_w()) > 0;
			const auto where_exists = wcslen(_exception.where_w()) > 0;
			const auto translation_exists = wcslen(_exception.translation_w()) > 0;

			return format_string(
				L"[ctl::ctException] %ws%ws%ws%ws [%u / 0x%x - %ws]",
				what_exists ? L" " : L"",
				what_exists ? _exception.what_w() : L"",
				where_exists ? L" at " : L"",
				where_exists ? _exception.where_w() : L"",
				_exception.why(),
				_exception.why(),
				translation_exists ? _exception.translation_w() : L"unknown error");
		}

#ifdef _CPPRTTI
		inline
		std::wstring format_exception(const std::exception& _exception)
		{
			// ReSharper disable once CppUseAuto
			const ctException* ctex = dynamic_cast<const ctException*>(&_exception);  // NOLINT
			if (ctex != nullptr) {
				return format_exception(*ctex);
			}
			return format_string(
				L"[std::exception] %hs",
				_exception.what());
		}
#else
        inline
        std::wstring format_exception(const std::exception& _exception)
        {
            return format_string(
                L"std::exception : %hs",
                _exception.what());
        }
#endif
	} // namespace ctString
} // namespace ctl
