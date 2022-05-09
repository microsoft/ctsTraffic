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
#include <wil/stl.h>
#include <wil/resource.h>
// project headers
#include "ctMath.hpp"


namespace ctsPerf { namespace Details
    {
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// write( ... )
        /// - overloads for ULONG and ULONGLONG data types
        /// - overloads for 1, 2, or 3 data points
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        inline std::wstring Write(ULONGLONG firstValue)
        {
            return wil::str_printf<std::wstring>(L",%llu", firstValue);
        }

        inline std::wstring Write(ULONG firstValue)
        {
            return wil::str_printf<std::wstring>(L",%lu", firstValue);
        }

        inline std::wstring Write(double firstValue)
        {
            return wil::str_printf<std::wstring>(L",%.3f", firstValue);
        }

        inline std::wstring Write(ULONGLONG firstValue, ULONGLONG secondValue)
        {
            return wil::str_printf<std::wstring>(L",%llu,%llu", firstValue, secondValue);
        }

        inline std::wstring Write(ULONG firstValue, ULONG secondValue)
        {
            return wil::str_printf<std::wstring>(L",%lu,%lu", firstValue, secondValue);
        }

        inline std::wstring Write(double firstValue, double secondValue)
        {
            return wil::str_printf<std::wstring>(L",%.3f,%.3f", firstValue, secondValue);
        }

        inline std::wstring Write(ULONGLONG firstValue, ULONGLONG secondValue, ULONGLONG thirdValue)
        {
            return wil::str_printf<std::wstring>(L",%llu,%llu,%llu", firstValue, secondValue, thirdValue);
        }

        inline std::wstring Write(ULONG firstValue, ULONG secondValue, ULONG thirdValue)
        {
            return wil::str_printf<std::wstring>(L",%lu,%lu,%lu", firstValue, secondValue, thirdValue);
        }

        inline std::wstring Write(double firstValue, double secondValue, double thirdValue)
        {
            return wil::str_printf<std::wstring>(L",%.3f,%.3f,%.3f", firstValue, secondValue, thirdValue);
        }
    }

    class ctsWriteDetails
    {
    private:
        void StartRow(_In_ PCWSTR className, _In_ PCWSTR counterName) const noexcept;
        void EndRow() const noexcept;

        std::wstring m_fileName;
        wil::unique_hfile m_fileHandle;

    public:
        template <typename T>
        static std::wstring PrintMeanStdDev(const std::vector<T>& data)
        {
            auto stdTuple = ctl::ctSampledStandardDeviation(data.begin(), data.end());
            return Details::Write(std::get<0>(stdTuple), std::get<1>(stdTuple)); // Mean,StdDev
        }

        template <typename T>
        static std::wstring PrintDetails(std::vector<T>& data)
        {
            if (data.empty())
            {
                return std::wstring();
            }

            // sort the data for IQR calculations
            sort(data.begin(), data.end());

            auto stdTuple = ctl::ctSampledStandardDeviation(data.begin(), data.end());
            auto interquartileTuple = ctl::ctInterquartileRange(data.begin(), data.end());

            auto formattedData = Details::Write(static_cast<DWORD>(data.size())); // SampleCount
            formattedData += Details::Write(*data.begin(), *data.rbegin()); // Min,Max
            formattedData += Details::Write(std::get<0>(stdTuple) - std::get<1>(stdTuple), std::get<0>(stdTuple), std::get<0>(stdTuple) + std::get<1>(stdTuple)); // -1Std,Mean,+1Std
            formattedData += Details::Write(std::get<0>(interquartileTuple), std::get<1>(interquartileTuple), std::get<2>(interquartileTuple)); // -1IQR,Median,+1IQR
            return formattedData;
        }

        explicit ctsWriteDetails(_In_ PCWSTR file_name) :
            m_fileName(file_name)
        {
        }

        ~ctsWriteDetails() noexcept = default;

        ctsWriteDetails(const ctsWriteDetails&) = delete;
        ctsWriteDetails& operator=(const ctsWriteDetails&) = delete;

        ctsWriteDetails(ctsWriteDetails&& rhs) noexcept = default;
        ctsWriteDetails& operator=(ctsWriteDetails&& rhs) noexcept = default;

        void CreateFile(bool meanOnly = false);
        void CreateFile(const std::wstring& bannerText);

        void WriteRow(const std::wstring& text) const noexcept;
        void WriteEmptyRow() const noexcept;

        //
        // The vector *will* be sorted before being returned (this is why it's non-const).
        //
        template <typename T>
        void WriteDetails(_In_ PCWSTR className, _In_ PCWSTR counterName, std::vector<T>& data)
        {
            if (data.empty())
            {
                return;
            }

            StartRow(className, counterName);

            const std::wstring formattedData(PrintDetails(data));
            const auto length = static_cast<DWORD>(formattedData.length() * sizeof(wchar_t));
            DWORD written{};
            THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), formattedData.c_str(), length, &written, nullptr));

            EndRow();
        }

        template <typename T>
        void WriteDifference(_In_ PCWSTR className, _In_ PCWSTR counterName, const std::vector<T>& data)
        {
            if (data.size() < 3)
            {
                return;
            }

            StartRow(className, counterName);
            // [0] == count
            // [1] == first
            // [2] == last
            const std::wstring difference = Details::Write(data[0], data[2] - data[1]);
            const auto length = static_cast<DWORD>(difference.length() * sizeof(wchar_t));
            DWORD written{};
            THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), difference.c_str(), length, &written, nullptr));

            EndRow();
        }

        template <typename T>
        void WriteMean(_In_ PCWSTR className, _In_ PCWSTR counterName, const std::vector<T>& data)
        {
            if (data.size() < 4)
            {
                return;
            }

            StartRow(className, counterName);
            // assumes vector is formatted as:
            // [0] == count
            // [1] == min
            // [2] == max
            // [3] == mean
            const std::wstring meanString = Details::Write(data[0], data[1]) + Details::Write(data[2], data[3]);
            const auto length = static_cast<DWORD>(meanString.length() * sizeof(wchar_t));
            DWORD written{};
            THROW_LAST_ERROR_IF(!::WriteFile(m_fileHandle.get(), meanString.c_str(), length, &written, nullptr));

            EndRow();
        }
    };
}
