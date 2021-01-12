/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
// os headers
#include <Windows.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
// project headers
#include "ctsConfig.h"
#include "ctsPrintStatus.hpp"

namespace ctsTraffic
{
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Base class for all ctsTraffic Loggers
    ///
    ///
    /// - all concrete types must implement:
    ///     message_impl(_In_ PCWSTR)
    ///     error_impl(_In_ PCWSTR)
    ///
    ///   Note: all logging functions are no-throw
    ///         only the c'tor can throw
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////

    class ctsLogger
    {
    public:
        explicit ctsLogger(ctsConfig::StatusFormatting format) noexcept :
            m_format(format)
        {
        }
        virtual ~ctsLogger() noexcept = default;

        void LogLegend(const std::shared_ptr<ctsStatusInformation>& status_info) noexcept
        {
            const auto* const message = status_info->PrintLegend(m_format);
            if (message != nullptr)
            {
                LogMessageImpl(message);
            }
        }

        void LogHeader(const std::shared_ptr<ctsStatusInformation>& status_info) noexcept
        {
            const auto* const message = status_info->PrintHeader(m_format);
            if (message != nullptr)
            {
                LogMessageImpl(message);
            }
        }

        void LogStatus(const std::shared_ptr<ctsStatusInformation>& status_info, long long current_time, bool clear_status) noexcept
        {
            const auto* const message = status_info->PrintStatus(m_format, current_time, clear_status);
            if (message != nullptr)
            {
                LogMessageImpl(message);
            }
        }

        void LogMessage(_In_ PCWSTR message) noexcept
        {
            LogMessageImpl(message);
        }

        void LogError(_In_ PCWSTR message) noexcept
        {
            LogErrorImpl(message);
        }

        [[nodiscard]] bool IsCsvFormat() const noexcept
        {
            return ctsConfig::StatusFormatting::Csv == m_format;
        }

        // not copyable
        ctsLogger(const ctsLogger&) = delete;
        ctsLogger& operator=(const ctsLogger&) = delete;
        ctsLogger(ctsLogger&&) = delete;
        ctsLogger& operator=(ctsLogger&&) = delete;

    private:
        ctsConfig::StatusFormatting m_format;

        /// pure virtual methods concrete classes must implement
        virtual void LogMessageImpl(_In_ PCWSTR message) noexcept = 0;
        virtual void LogErrorImpl(_In_ PCWSTR message) noexcept = 0;
    };

    class ctsTextLogger : public ctsLogger
    {
    public:
        ctsTextLogger(_In_ PCWSTR file_name, ctsConfig::StatusFormatting format) :
            ctsLogger(format)
        {
            m_fileHandle.reset(CreateFileW(
                file_name,
                GENERIC_WRITE,
                FILE_SHARE_READ, // allow others to read the file while we write to it
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr));
            THROW_LAST_ERROR_IF_NULL(m_fileHandle.get());

            // write the UTF16 Byte order mark
            constexpr WCHAR bom_utf16 = 0xFEFF;
            DWORD bytesWritten{};
            THROW_LAST_ERROR_IF(!WriteFile(
                m_fileHandle.get(),
                &bom_utf16,
                static_cast<DWORD>(sizeof WCHAR),
                &bytesWritten,
                nullptr));
        }
        ~ctsTextLogger() noexcept override = default;

        void LogMessageImpl(_In_ PCWSTR message) noexcept override
        {
            WriteImpl(message);
        }

        void LogErrorImpl(_In_ PCWSTR message) noexcept override
        {
            WriteImpl(message);
        }

        ctsTextLogger(const ctsTextLogger&) = delete;
        ctsTextLogger& operator=(const ctsTextLogger&) = delete;
        ctsTextLogger(ctsTextLogger&&) = delete;
        ctsTextLogger& operator=(ctsTextLogger&&) = delete;

    private:
        wil::critical_section m_fileLock{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
        wil::unique_hfile m_fileHandle;

        void WriteImpl(_In_ PCWSTR message) noexcept
        {
            const auto lock = m_fileLock.lock();
            DWORD bytesWritten{};
            THROW_LAST_ERROR_IF(!WriteFile(
                m_fileHandle.get(),
                message,
                static_cast<DWORD>(wcslen(message) * sizeof(WCHAR)),
                &bytesWritten,
                nullptr));
        }
    };

} // namespace
