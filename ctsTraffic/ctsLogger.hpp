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
#include <windows.h>
// wil headers
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
        explicit ctsLogger(ctsConfig::StatusFormatting _format) noexcept :
            m_format(_format)
        {
        }
        virtual ~ctsLogger() noexcept = default;

        void LogLegend(const std::shared_ptr<ctsStatusInformation>& _status_info) noexcept
        {
            const auto message = _status_info->print_legend(m_format);
            if (message != nullptr)
            {
                log_message_impl(message);
            }
        }

        void LogHeader(const std::shared_ptr<ctsStatusInformation>& _status_info) noexcept
        {
            const auto message = _status_info->print_header(m_format);
            if (message != nullptr)
            {
                log_message_impl(message);
            }
        }

        void LogStatus(const std::shared_ptr<ctsStatusInformation>& _status_info, long long _current_time, bool _clear_status) noexcept
        {
            const auto message = _status_info->print_status(m_format, _current_time, _clear_status);
            if (message != nullptr)
            {
                log_message_impl(message);
            }
        }

        void LogMessage(_In_ PCWSTR _message) noexcept
        {
            log_message_impl(_message);
        }

        void LogError(_In_ PCWSTR _message) noexcept
        {
            log_error_impl(_message);
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
        virtual void log_message_impl(_In_ PCWSTR _message) noexcept = 0;
        virtual void log_error_impl(_In_ PCWSTR _message) noexcept = 0;
    };

    class ctsTextLogger : public ctsLogger
    {
    public:
        ctsTextLogger(_In_ PCWSTR _file_name, ctsConfig::StatusFormatting _format) :
            ctsLogger(_format)
        {
            file_handle.reset(CreateFileW(
                _file_name,
                GENERIC_WRITE,
                FILE_SHARE_READ, // allow others to read the file while we write to it
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr));
            THROW_LAST_ERROR_IF_NULL(file_handle.get());

            // write the UTF16 Byte order mark
            constexpr WCHAR bom_utf16 = 0xFEFF;
            DWORD bytesWritten{};
            THROW_LAST_ERROR_IF(!WriteFile(
                file_handle.get(),
                &bom_utf16,
                static_cast<DWORD>(sizeof WCHAR),
                &bytesWritten,
                nullptr));
        }
        ~ctsTextLogger() noexcept = default;

        void log_message_impl(_In_ PCWSTR _message) noexcept override
        {
            write_impl(_message);
        }

        void log_error_impl(_In_ PCWSTR _message) noexcept override
        {
            write_impl(_message);
        }

        ctsTextLogger(const ctsTextLogger&) = delete;
        ctsTextLogger& operator=(const ctsTextLogger&) = delete;
        ctsTextLogger(ctsTextLogger&&) = delete;
        ctsTextLogger& operator=(ctsTextLogger&&) = delete;

    private:
        wil::critical_section file_cs;
        wil::unique_hfile file_handle;

        void write_impl(_In_ PCWSTR _message) noexcept
        {
            const auto lock = file_cs.lock();
            DWORD bytesWritten{};
            THROW_LAST_ERROR_IF(!WriteFile(
                file_handle.get(),
                _message,
                static_cast<DWORD>(wcslen(_message) * sizeof(WCHAR)),
                &bytesWritten,
                nullptr));
        }
    };

} // namespace
