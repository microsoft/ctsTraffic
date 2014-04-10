/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// C++ headers
#include <stdio.h>
#include <string>
#include <exception>
#include <memory>
// OS headers
#include <windows.h>
// ctl headers
#include <ctException.hpp>
#include <ctString.hpp>
#include <ctScopeGuard.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsPrintStatus.hpp"


namespace ctsTraffic {
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Base class for all ctsTraffic Loggers
    ///
    ///
    /// - all concrete types must implement:
    ///     message_impl(LPCWSTR)
    ///     error_impl(LPCWSTR)
    ///
    ///   Note: all logging functions are no-throw
    ///         only the c'tor can throw
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////

    class ctsLogger {
    public:
        ctsLogger(ctsConfig::StatusFormatting _format) throw() : format(_format)
        {
        }
        virtual ~ctsLogger() throw()
        {
        }

        void LogLegend(const std::shared_ptr<ctsTraffic::ctsStatusInformation>& _status_info) throw()
        {
            LPCWSTR message = _status_info->print_legend(this->format);
            if (message != nullptr) {
                log_message_impl(message);
            }
        }

        void LogHeader(const std::shared_ptr<ctsTraffic::ctsStatusInformation>& _status_info) throw()
        {
            LPCWSTR message = _status_info->print_header(this->format);
            if (message != nullptr) {
                log_message_impl(message);
            }
        }

        void LogStatus(const std::shared_ptr<ctsTraffic::ctsStatusInformation>& _status_info, long long _current_time, bool _clear_status) throw()
        {
            LPCWSTR message = _status_info->print_status(this->format, _current_time, _clear_status);
            if (message != nullptr) {
                log_message_impl(message);
            }
        }

        void LogMessage(_In_ LPCWSTR _message) throw()
        {
            log_message_impl(_message);
        }

        void LogError(_In_ LPCWSTR _message) throw()
        {
            log_error_impl(_message);
        }

        bool IsCsvFormat() const throw()
        {
            return ctsConfig::StatusFormatting::Csv == this->format;
        }

        // not copyable
        ctsLogger(const ctsLogger&) = delete;
        ctsLogger& operator=(const ctsLogger&) = delete;

    private:
        ctsConfig::StatusFormatting format;

        /// pure virtual methods concrete classes must implement
        virtual void log_message_impl(_In_ LPCWSTR _message) throw() = 0;
        virtual void log_error_impl(_In_ LPCWSTR _message) throw() = 0;
    };

    class ctsTextLogger : public ctsLogger {
    public:
        ctsTextLogger(_In_ LPCWSTR _file_name, ctsConfig::StatusFormatting _format)
        : ctsLogger(_format),
          file_cs(),
          file_handle(INVALID_HANDLE_VALUE)
        {
            if (!::InitializeCriticalSectionEx(&file_cs, 4000, 0)) {
                throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsTextLogger", false);
            }
            ctlScopeGuard(deleteCSOnError, { ::DeleteCriticalSection(&file_cs); });

            file_handle = ::CreateFileW(
                _file_name,
                GENERIC_WRITE,
                FILE_SHARE_READ, // allow others to read the file while we write to it
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL);
            if (INVALID_HANDLE_VALUE == file_handle) {
                throw ctl::ctException(
                    ::GetLastError(),
                    ctl::ctString::format_string(L"CreateFile(%s)", _file_name).c_str(),
                    L"ctsTextLogger",
                    true);
            }
            ctlScopeGuard(closeHandleOnError, { ::CloseHandle(file_handle); file_handle = INVALID_HANDLE_VALUE; });

            // write the UTF16 Byte order mark
            static const WCHAR BOM_UTF16 = 0xFEFF;
            DWORD BytesWritten;
            if (!::WriteFile(
                file_handle,
                &BOM_UTF16,
                static_cast<DWORD>(sizeof WCHAR),
                &BytesWritten,
                NULL)) {
                throw ctl::ctException(::GetLastError(), L"WriteFile", L"ctsTextLogger", false);
            }

            // everything succeeded, dismiss the scope guards
            closeHandleOnError.dismiss();
            deleteCSOnError.dismiss();
        }
        ~ctsTextLogger() throw()
        {
            ::CloseHandle(file_handle);
            ::DeleteCriticalSection(&file_cs);
        }

        void log_message_impl(_In_ LPCWSTR _message) throw()
        {
            write_impl(_message);
        }

        void log_error_impl(_In_ LPCWSTR _message) throw()
        {
            write_impl(_message);
        }

    private:
        CRITICAL_SECTION file_cs;
        HANDLE file_handle;

        void write_impl(_In_ LPCWSTR _message) throw()
        {
            try {
                auto converted_string(ctl::ctString::replace_all_copy(_message, L"\n", L"\r\n"));
                DWORD BytesWritten;
                if (!::WriteFile(
                    file_handle,
                    converted_string.c_str(),
                    static_cast<DWORD>(converted_string.length() * sizeof(WCHAR)),
                    &BytesWritten,
                    NULL
                    )) {
                    ctl::ctException write_error(::GetLastError(), L"WriteFile", L"ctsTextLogger", false);
                    ctsConfig::PrintException(write_error);
                }
            }
            catch (const std::exception& e) {
                ctsConfig::PrintException(e);
            }
        }
    };

} // namespace