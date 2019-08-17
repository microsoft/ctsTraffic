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
// wil headers
#include <wil/resource.h>
// project headers
#include "ctException.hpp"
#include "ctString.hpp"
#include "ctMath.hpp"


namespace ctsPerf {
    namespace Details {
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// write( ... )
        /// - overloads for ULONG and ULONGLONG data types
        /// - overloads for 1, 2, or 3 data points
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        inline std::wstring Write(ULONGLONG first_value)
        {
            return ctl::ctString::ctFormatString(L",%llu", first_value);
        }
        inline std::wstring Write(ULONG first_value)
        {
            return ctl::ctString::ctFormatString(L",%lu", first_value);
        }
        inline std::wstring Write(double first_value)
        {
            return ctl::ctString::ctFormatString(L",%.3f", first_value);
        }
        inline std::wstring Write(ULONGLONG first_value, ULONGLONG second_value)
        {
            return ctl::ctString::ctFormatString(L",%llu,%llu", first_value, second_value);
        }
        inline std::wstring Write(ULONG first_value, ULONG second_value)
        {
            return ctl::ctString::ctFormatString(L",%lu,%lu", first_value, second_value);
        }
        inline std::wstring Write(double first_value, double second_value)
        {
            return ctl::ctString::ctFormatString(L",%.3f,%.3f", first_value, second_value);
        }
        inline std::wstring Write(ULONGLONG first_value, ULONGLONG second_value, ULONGLONG third_value)
        {
            return ctl::ctString::ctFormatString(L",%llu,%llu,%llu", first_value, second_value, third_value);
        }
        inline std::wstring Write(ULONG first_value, ULONG second_value, ULONG third_value)
        {
            return ctl::ctString::ctFormatString(L",%lu,%lu,%lu", first_value, second_value, third_value);
        }
        inline std::wstring Write(double first_value, double second_value, double third_value)
        {
            return ctl::ctString::ctFormatString(L",%.3f,%.3f,%.3f", first_value, second_value, third_value);
        }
    }

    class ctsWriteDetails {
    private:
        void start_row(PCWSTR class_name, PCWSTR counter_name) const noexcept;
        void end_row() const noexcept;

        std::wstring m_fileName;
        wil::unique_hfile m_fileHandle;

    public:
        template <typename T>
        static std::wstring PrintMeanStdDev(const std::vector<T>& data)
        {
            auto std_tuple = ctl::ctSampledStandardDeviation(data.begin(), data.end());
            return Details::Write(std::get<0>(std_tuple), std::get<1>(std_tuple)); // Mean,StdDev
        }

        template <typename T>
        static std::wstring PrintDetails(std::vector<T>& data)
        {
            if (data.empty()) {
                return std::wstring();
            }

            // sort the data for IQR calculations
            sort(data.begin(), data.end());

            auto std_tuple = ctl::ctSampledStandardDeviation(data.begin(), data.end());
            auto interquartile_tuple = ctl::ctInterquartileRange(data.begin(), data.end());

            auto formatted_data = Details::Write(static_cast<DWORD>(data.size()));  // SampleCount
            formatted_data += Details::Write(*data.begin(), *data.rbegin()); // Min,Max
            formatted_data += Details::Write(std::get<0>(std_tuple) - std::get<1>(std_tuple),  std::get<0>(std_tuple), std::get<0>(std_tuple) + std::get<1>(std_tuple)); // -1Std,Mean,+1Std
            formatted_data += Details::Write(std::get<0>(interquartile_tuple), std::get<1>(interquartile_tuple), std::get<2>(interquartile_tuple)); // -1IQR,Median,+1IQR
            return formatted_data;
        }

        explicit ctsWriteDetails(PCWSTR _file_name) : m_fileName(_file_name)
        {
        }
        ~ctsWriteDetails() noexcept = default;

        ctsWriteDetails(const ctsWriteDetails&) = delete;
        ctsWriteDetails& operator=(const ctsWriteDetails&) = delete;

        ctsWriteDetails(ctsWriteDetails&& rhs) noexcept = default;
        ctsWriteDetails& operator=(ctsWriteDetails&& rhs) noexcept = default;

        void create_file(bool mean_only = false);
        void create_file(const std::wstring& banner_text);

        void write_row(const std::wstring& text) const noexcept;
		void write_empty_row() const noexcept;

        //
        // The vector *will* be sorted before being returned (this is why it's non-const).
        //
        template <typename T>
        void write_details(PCWSTR class_name, PCWSTR counter_name, std::vector<T>& data)
        {
            if (data.empty()) {
                return;
            }

            start_row(class_name, counter_name);

            const std::wstring formattedData(PrintDetails(data));
            const auto length = static_cast<DWORD>(formattedData.length() * sizeof(wchar_t));
            DWORD written;
            if (!::WriteFile(m_fileHandle.get(), formattedData.c_str(), length, &written, nullptr)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }

            end_row();
        }

        template <typename T>
        void write_difference(PCWSTR class_name, PCWSTR counter_name, const std::vector<T>& data)
        {
            if (data.size() < 3) {
                return;
            }

            start_row(class_name, counter_name);
            // [0] == count
            // [1] == first
            // [2] == last
            const std::wstring difference = Details::Write(data[0], data[2] - data[1]);
            const auto length = static_cast<DWORD>(difference.length() * sizeof(wchar_t));
            DWORD written;
            if (!::WriteFile(m_fileHandle.get(), difference.c_str(), length, &written, nullptr)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }

            end_row();
        }

        template <typename T>
        void write_mean(PCWSTR class_name, PCWSTR counter_name, const std::vector<T>& data)
        {
            if (data.size() < 4) {
                return;
            }

            start_row(class_name, counter_name);
            // assumes vector is formatted as:
            // [0] == count
            // [1] == min
            // [2] == max
            // [3] == mean
            const std::wstring meanString = Details::Write(data[0], data[1]) + Details::Write(data[2], data[3]);
            const auto length = static_cast<DWORD>(meanString.length() * sizeof(wchar_t));
            DWORD written;
            if (!::WriteFile(m_fileHandle.get(), meanString.c_str(), length, &written, nullptr)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", false);
            }

            end_row();
        }
    };
}
