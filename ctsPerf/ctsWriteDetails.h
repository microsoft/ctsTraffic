/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <string>
#include <vector>
#include <tuple>

// os headers
#include <Windows.h>

// project headers
#include "ctException.hpp"
#include "ctString.hpp"
#include "ctMath.hpp"


namespace ctsPerf {
    namespace details {
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// write( ... )
        /// - overloads for ULONG and ULONGLONG data types
        /// - overloads for 1, 2, or 3 data points
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        inline std::wstring write(ULONGLONG _first_value)
        {
            return ctl::ctString::format_string(L",%llu", _first_value);
        }
        inline std::wstring write(ULONG _first_value)
        {
            return ctl::ctString::format_string(L",%lu", _first_value);
        }
        inline std::wstring write(double _first_value)
        {
            return ctl::ctString::format_string(L",%.3f", _first_value);
        }
        inline std::wstring write(ULONGLONG _first_value, ULONGLONG _second_value)
        {
            return ctl::ctString::format_string(L",%llu,%llu", _first_value, _second_value);
        }
        inline std::wstring write(ULONG _first_value, ULONG _second_value)
        {
            return ctl::ctString::format_string(L",%lu,%lu", _first_value, _second_value);
        }
        inline std::wstring write(double _first_value, double _second_value)
        {
            return ctl::ctString::format_string(L",%.3f,%.3f", _first_value, _second_value);
        }
        inline std::wstring write(ULONGLONG _first_value, ULONGLONG _second_value, ULONGLONG _third_value)
        {
            return ctl::ctString::format_string(L",%llu,%llu,%llu", _first_value, _second_value, _third_value);
        }
        inline std::wstring write(ULONG _first_value, ULONG _second_value, ULONG _third_value)
        {
            return ctl::ctString::format_string(L",%lu,%lu,%lu", _first_value, _second_value, _third_value);
        }
        inline std::wstring write(double _first_value, double _second_value, double _third_value)
        {
            return ctl::ctString::format_string(L",%.3f,%.3f,%.3f", _first_value, _second_value, _third_value);
        }
    }

    class ctsWriteDetails {
    private:
        void start_row(LPCWSTR _class_name, LPCWSTR _counter_name) const noexcept;
        void end_row() const noexcept;

        std::wstring file_name;
        HANDLE file_handle = INVALID_HANDLE_VALUE;

    public:
        template <typename T>
        static std::wstring PrintMeanStdDev(const std::vector<T>& _data)
        {
            auto std_tuple = ctl::ctSampledStandardDeviation(_data.begin(), _data.end());
            return details::write(std::get<0>(std_tuple), std::get<1>(std_tuple)); // Mean,StdDev
        }

        template <typename T>
        static std::wstring PrintDetails(std::vector<T>& _data)
        {
            if (_data.empty()) {
                return std::wstring();
            }

            // sort the data for IQR calculations
            sort(_data.begin(), _data.end());

            auto std_tuple = ctl::ctSampledStandardDeviation(_data.begin(), _data.end());
            auto interquartile_tuple = ctl::ctInterquartileRange(_data.begin(), _data.end());

            auto formatted_data = details::write(static_cast<DWORD>(_data.size()));  // SampleCount
            formatted_data += details::write(*_data.begin(), *_data.rbegin()); // Min,Max
            formatted_data += details::write(std::get<0>(std_tuple) - std::get<1>(std_tuple),  std::get<0>(std_tuple), std::get<0>(std_tuple) + std::get<1>(std_tuple)); // -1Std,Mean,+1Std
            formatted_data += details::write(std::get<0>(interquartile_tuple), std::get<1>(interquartile_tuple), std::get<2>(interquartile_tuple)); // -1IQR,Median,+1IQR
            return formatted_data;
        }

        explicit ctsWriteDetails(LPCWSTR _file_name) : file_name(_file_name)
        {
        }
        ~ctsWriteDetails() noexcept
        {
            if (file_handle != INVALID_HANDLE_VALUE) {
                ::CloseHandle(file_handle);
            }
        }
        ctsWriteDetails(const ctsWriteDetails&) = delete;
        ctsWriteDetails& operator=(const ctsWriteDetails&) = delete;

        ctsWriteDetails(ctsWriteDetails&& rhs) noexcept
        : file_name(std::move(rhs.file_name)),
          file_handle(rhs.file_handle)
        {
            // don't let the moved-from object close the handle
            rhs.file_handle = INVALID_HANDLE_VALUE;
        }
        ctsWriteDetails& operator=(ctsWriteDetails&& rhs) noexcept
        {
            if (file_handle != INVALID_HANDLE_VALUE) {
                ::CloseHandle(file_handle);
            }

            file_name = std::move(rhs.file_name);
            file_handle = rhs.file_handle;
            // don't let the moved-from object close the handle
            rhs.file_handle = INVALID_HANDLE_VALUE;
            return *this;
        }

        void create_file(bool _mean_only = false);
        void create_file(const std::wstring& _banner_text);

        void write_row(const std::wstring& text) const noexcept;
		void write_empty_row() const noexcept;

        //
        // The vector *will* be sorted before being returned (this is why it's non-const).
        //
        template <typename T>
        void write_details(LPCWSTR _class_name, LPCWSTR _counter_name, std::vector<T>& _data)
        {
            if (_data.empty()) {
                return;
            }

            start_row(_class_name, _counter_name);

            std::wstring formatted_data(PrintDetails(_data));
            const auto length = static_cast<DWORD>(formatted_data.length() * sizeof(wchar_t));
            DWORD written;
            if (!::WriteFile(file_handle, formatted_data.c_str(), length, &written, nullptr)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }

            end_row();
        }

        template <typename T>
        void write_difference(LPCWSTR _class_name, LPCWSTR _counter_name, const std::vector<T>& _data)
        {
            if (_data.size() < 3) {
                return;
            }

            start_row(_class_name, _counter_name);
            // [0] == count
            // [1] == first
            // [2] == last
            std::wstring difference = details::write(_data[0], _data[2] - _data[1]);
            const auto length = static_cast<DWORD>(difference.length() * sizeof(wchar_t));
            DWORD written;
            if (!::WriteFile(file_handle, difference.c_str(), length, &written, nullptr)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }

            end_row();
        }

        template <typename T>
        void write_mean(LPCWSTR _class_name, LPCWSTR _counter_name, const std::vector<T>& _data)
        {
            if (_data.size() < 4) {
                return;
            }

            start_row(_class_name, _counter_name);
            // assumes vector is formatted as:
            // [0] == count
            // [1] == min
            // [2] == max
            // [3] == mean
            std::wstring mean_string = details::write(_data[0], _data[1]) + details::write(_data[2], _data[3]);
            const auto length = static_cast<DWORD>(mean_string.length() * sizeof(wchar_t));
            DWORD written;
            if (!::WriteFile(file_handle, mean_string.c_str(), length, &written, nullptr)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }

            end_row();
        }
    };
}
