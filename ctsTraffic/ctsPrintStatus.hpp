/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <wchar.h>
// os headers
#include <windows.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctException.hpp>
#include <ctLocks.hpp>
#include <ctString.hpp>
// project headers
#include "ctsConfig.h"


namespace ctsTraffic {

    ///
    /// Abstract base class for status - printing classes
    ///
    class ctsStatusInformation {
    protected:
        enum class PrintingStatus
        {
            PrintComplete,
            NoPrint
        };

    private:
        // expanded beyond 80 to handle very long IPv6 address strings
        // - buffer is expected to be protected by only a single caller at a time
        static const unsigned long OutputBufferSize = 128;
        // one more for the null terminator
        wchar_t OutputBuffer[OutputBufferSize + 1];

        void reset_buffer()
        {
            // fill the output buffer with spaces and null terminate
            ::wmemset(OutputBuffer, L' ', OutputBufferSize);
            OutputBuffer[OutputBufferSize] = L'\0';
        }

    public:
        ctsStatusInformation() NOEXCEPT
        {
        }

        // base class is movable
        ctsStatusInformation(ctsStatusInformation&& _moved_from) NOEXCEPT
        {
            ::wmemcpy_s(this->OutputBuffer, OutputBufferSize + 1, _moved_from.OutputBuffer, OutputBufferSize + 1);
            _moved_from.reset_buffer();
        }

        virtual ~ctsStatusInformation() NOEXCEPT
        {
        }

        LPCWSTR print_legend(ctsConfig::StatusFormatting _format) NOEXCEPT
        {
            if (ctsConfig::StatusFormatting::Csv == _format) {
                return nullptr;
            } else {
                return this->format_legend();
            }
        }

        LPCWSTR print_header(ctsConfig::StatusFormatting _format) NOEXCEPT
        {
            return this->format_header(_format);
        }

        ///
        /// Expects to be called in a loop
        /// - returns nullptr if nothing left to print
        ///
        LPCWSTR print_status(ctsConfig::StatusFormatting _format, long long _current_time, bool _clear_status) NOEXCEPT
        {
            this->reset_buffer();
            if (this->format_data(_format, _current_time, _clear_status) != PrintingStatus::NoPrint) {
                return OutputBuffer;
            } else {
                return nullptr;
            }
        }

    protected:
        ///
        /// derived class is required to implement these three pure virtual function
        ///
        virtual PrintingStatus format_data(ctsConfig::StatusFormatting _format, long long _current_time, bool _clear_status) NOEXCEPT = 0;
        virtual LPCWSTR format_legend() NOEXCEPT = 0;
        virtual LPCWSTR format_header(ctsConfig::StatusFormatting _format) NOEXCEPT = 0;

        void left_justify_output(unsigned long _left_justified_offset, unsigned long _max_length, _In_ LPCWSTR _value) NOEXCEPT
        {
            ctl::ctFatalCondition(
                0 == _left_justified_offset,
                L"ctsStatusInformation was given a zero offset in left_justify_output : must be at least 1");
            ctl::ctFatalCondition(
                _left_justified_offset > OutputBufferSize,
                L"ctsStatusInformation will only print up to %u columns - an offset of %u was given",
                OutputBufferSize, _left_justified_offset);

            size_t value_length = ::wcslen(_value);
            ctl::ctFatalCondition(
                value_length > _max_length,
                L"ctsStatusInformation was given a string longer than the max value given (%u) -- '%s'",
                _max_length, _value);

            ::wmemcpy_s(
                OutputBuffer + _left_justified_offset - 1,
                OutputBufferSize - _left_justified_offset - 1,
                _value,
                value_length);
        }
        void right_justify_output(unsigned long _right_justified_offset, unsigned long _max_length, float _value) NOEXCEPT
        {
            static const unsigned long CoversionBufferLength = 16;
            wchar_t ConversionBuffer[CoversionBufferLength];

            ctl::ctFatalCondition(
                _right_justified_offset > OutputBufferSize,
                L"ctsStatusInformation will only print up to %u columns - an offset of %u was given",
                OutputBufferSize, _right_justified_offset);
            _Analysis_assume_(_right_justified_offset <= OutputBufferSize);

            ctl::ctFatalCondition(
                _max_length > CoversionBufferLength - 1, // minus one for the null terminator
                L"ctsStatusInformation will only print converted strings up to %u characters long - the number '%u' was given",
                CoversionBufferLength - 1, _max_length);
            _Analysis_assume_(_max_length <= CoversionBufferLength - 1);

            auto converted = ::_snwprintf_s(
                ConversionBuffer,
                CoversionBufferLength,
                _TRUNCATE,
                L"%.3f",
                _value);
            ctl::ctFatalCondition(
                -1 == converted,
                L"_snwprintf_s failed (value == %f), errno == %d",
                _value, errno);
            _Analysis_assume_(converted != -1);

            ::wmemcpy_s(
                OutputBuffer + (_right_justified_offset - converted),
                OutputBufferSize - (_right_justified_offset - converted),
                ConversionBuffer,
                converted);
        }
        void right_justify_output(unsigned long _right_justified_offset, unsigned long _max_length, unsigned long _value) NOEXCEPT
        {
            static const unsigned long CoversionBufferLength = 12;
            wchar_t ConversionBuffer[CoversionBufferLength];

            ctl::ctFatalCondition(
                _right_justified_offset > OutputBufferSize,
                L"ctsStatusInformation will only print up to %u columns - an offset of %u was given",
                OutputBufferSize, _right_justified_offset);
            _Analysis_assume_(_right_justified_offset <= OutputBufferSize);

            ctl::ctFatalCondition(
                _max_length > CoversionBufferLength - 1, // minus one for the null terminator
                L"ctsStatusInformation will only print converted strings up to %u characters long - the number '%u' was given",
                CoversionBufferLength - 1, _max_length);
            _Analysis_assume_(_max_length > CoversionBufferLength - 1);

            int converted = ::_snwprintf_s(
                ConversionBuffer,
                CoversionBufferLength,
                _TRUNCATE,
                L"%u",
                _value);
            ctl::ctFatalCondition(
                -1 == converted,
                L"_snwprintf_s failed (value == %u), errno == %d",
                _value, errno);
            _Analysis_assume_(converted != -1);

            ::wmemcpy_s(
                OutputBuffer + (_right_justified_offset - converted),
                OutputBufferSize - (_right_justified_offset - converted),
                ConversionBuffer,
                converted);
        }
        void right_justify_output(unsigned long _right_justified_offset, unsigned long _max_length, long long _value) NOEXCEPT
        {
            static const unsigned long CoversionBufferLength = 20;
            wchar_t ConversionBuffer[CoversionBufferLength];

            ctl::ctFatalCondition(
                _value < 0LL,
                L"ctsStatusInformation output was given a negative value to print (or greater than MAXLONGLONG): %llx",
                _value);
            _Analysis_assume_(_value >= 0LL);

            ctl::ctFatalCondition(
                _right_justified_offset > OutputBufferSize,
                L"ctsStatusInformation will only print up to %u columns - an offset of %u was given",
                OutputBufferSize, _right_justified_offset);
            _Analysis_assume_(_right_justified_offset <= OutputBufferSize);

            ctl::ctFatalCondition(
                _max_length > CoversionBufferLength - 1, // minus one for the null terminator
                L"ctsStatusInformation will only print converted strings up to %u characters long - the number '%u' was given",
                CoversionBufferLength - 1, _max_length);
            _Analysis_assume_(_max_length <= CoversionBufferLength - 1);

            int converted = ::_snwprintf_s(
                ConversionBuffer,
                CoversionBufferLength,
                _TRUNCATE,
                L"%lld",
                _value);
            ctl::ctFatalCondition(
                -1 == converted,
                L"_snwprintf_s failed (value == %lld), errno == %d",
                _value, errno);
            _Analysis_assume_(converted != -1);

            ::wmemcpy_s(
                OutputBuffer + (_right_justified_offset - converted),
                OutputBufferSize - (_right_justified_offset - converted),
                ConversionBuffer,
                converted);
        }

        void terminate_string(unsigned long _offset)
        {
            OutputBuffer[_offset] = L'\n';
            OutputBuffer[_offset + 1] = L'\0';
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Functions to write to the output buffer in CSV formatting
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        unsigned long append_csvoutput(unsigned long _offset, unsigned long _value_length, float _value, bool _add_comma = true) NOEXCEPT
        {
            auto converted = ::_snwprintf_s(
                OutputBuffer + _offset,
                OutputBufferSize - _offset,
                _add_comma ? _value_length + 1 : _value_length,
                _add_comma ? L"%.3f," : L"%.3f",
                _value);
            ctl::ctFatalCondition(
                -1 == converted,
                L"_snwprintf_s failed to convert this (%p) ctsUdpStatusInformation", this);
            return converted;
        }

        unsigned long append_csvoutput(unsigned long _offset, unsigned long _value_length, unsigned long _value, bool _add_comma = true) NOEXCEPT
        {
            errno_t error = ::_ui64tow_s(
                _value,
                OutputBuffer + _offset,
                OutputBufferSize - _offset,
                10);
            ctl::ctFatalCondition(
                error != 0,
                L"_ui64tow_s failed to convert this (%p) ctsUdpStatusInformation - %u", this, _value);

            // find how many characters were printed
            unsigned long converted = 0;
            wchar_t* output_reference = OutputBuffer + _offset;
            while (*output_reference != L'\0' && *output_reference != L' ') {
                ++converted;
                ++output_reference;
            }

            ctl::ctFatalCondition(
                converted > (OutputBufferSize - _offset),
                L"Counting the string built by _ui64tow_s overflowed - converted (%u) _offset (%u) : ctsUdpStatusInformation (%p)\n", converted, _offset, this);
            ctl::ctFatalCondition(
                converted > _value_length,
                L"Counting the string built by _ui64tow_s was greater than _value_length (%u) : ctsUdpStatusInformation (%p)\n", _value_length, this);

            if (_add_comma) {
                ++converted;
                *output_reference = L',';
                ++output_reference;
            }
            return converted;
        }

        unsigned long append_csvoutput(unsigned long _offset, unsigned long _value_length, long long _value, bool _add_comma = true) NOEXCEPT
        {
            errno_t error = ::_ui64tow_s(
                _value,
                OutputBuffer + _offset,
                OutputBufferSize - _offset,
                10);
            ctl::ctFatalCondition(
                error != 0,
                L"_ui64tow_s failed to convert this (%p) ctsUdpStatusInformation - %lld", this, _value);

            // find how many characters were printed
            unsigned long converted = 0;
            wchar_t* output_reference = OutputBuffer + _offset;
            while (*output_reference != L'\0' && *output_reference != L' ') {
                ++converted;
                ++output_reference;
            }

            ctl::ctFatalCondition(
                converted > (OutputBufferSize - _offset),
                L"Counting the string built by _ui64tow_s overflowed - converted (%u) _offset (%u) : ctsUdpStatusInformation (%p)\n", converted, _offset, this);
            ctl::ctFatalCondition(
                converted > _value_length,
                L"Counting the string built by _ui64tow_s was greater than _value_length (%u) : ctsUdpStatusInformation (%p)\n", _value_length, this);

            if (_add_comma) {
                ++converted;
                *output_reference = L',';
                ++output_reference;
            }
            return converted;
        }

        unsigned long append_csvoutput(unsigned long _offset, unsigned long _value_length, _In_ LPCWSTR _value, bool _add_comma = true) NOEXCEPT
        {
            auto converted = ::_snwprintf_s(
                OutputBuffer + _offset,
                OutputBufferSize - _offset,
                _add_comma ? _value_length + 1 : _value_length,
                _add_comma ? L"%s," : L"%s",
                _value);
            ctl::ctFatalCondition(
                -1 == converted,
                L"_snwprintf_s failed to convert this (%p) ctsUdpStatusInformation", this);
            return converted;
        }
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// All variables are updated with Interlocked* operations
    ///   as it's more important to remain responsive than to guarantee 
    ///   all information is reflected in the precise printed line
    /// - note that *no* information will be lost 
    ///   all data will be accounted for in either the current printed line
    ///   or in the next printed line
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsUdpStatusInformation : public ctsStatusInformation {
    public:
        ctsUdpStatusInformation() NOEXCEPT
        {
        }
        ~ctsUdpStatusInformation() NOEXCEPT
        {
        }

        ///
        /// Pure-Virtual functions required to be defined
        ///
        LPCWSTR format_legend() NOEXCEPT
        {
            return
                L"Legend:\n"
                L"* TimeSlice - (seconds) cumulative runtime\n"
                L"* Bits/Sec - bits streamed within the TimeSlice period\n"
                L"* Completed Frames - count of frames successfully processed within the TimeSlice\n"
                L"* Repeated Frames - count of frames received multiple times within the TimeSlice\n"
                L"* Attempted Retries - count of retries for missing frames within the TimeSlice\n"
                L"* Stream Errors - count of invalid frames or buffers within the TimeSlice\n"
                L"\n";
        }

        LPCWSTR format_header(ctsConfig::StatusFormatting _format) NOEXCEPT
        {
            if (ctsConfig::StatusFormatting::Csv == _format) {
                return
                    L"TimeSlice,Bits/Sec,Completed,Dropped,Repeated,Retries,Errors\n";

            } else {
                /// Formatted to fit on an 80-column command shell
                return
                    L" TimeSlice      Bits/Sec   Completed   Dropped   Repeated     Retries    Errors \n";
                ///   00000000.0..000000000000...000000000...0000000....0000000.....0000000...0000000        
                ///   1   5    0    5    0    5    0    5    0    5    0    5    0    5    0    5    0 
                ///           10        20        30        40        50        60        70        80
            }
        }

        PrintingStatus format_data(ctsConfig::StatusFormatting _format, long long _current_time, bool _clear_status) NOEXCEPT
        {
            ctsUdpStatistics udp_data(ctsConfig::Settings->UdpStatusDetails.snap_view(_clear_status));

            if (ctsConfig::StatusFormatting::Csv == _format) {
                unsigned long characters_written = 0;
                // converting milliseconds to seconds before printing
                characters_written += this->append_csvoutput(characters_written, TimeSliceLength, static_cast<float>(_current_time / 1000.0));
                // calculating # of bytes that were received between the previous format() and current call to format()
                long long time_elapsed = udp_data.end_time.get() - udp_data.start_time.get();
                characters_written += this->append_csvoutput(
                    characters_written,
                    BitsPerSecondLength,
                    (time_elapsed > 0LL) ? static_cast<long long>(udp_data.bits_received.get() * 1000LL / time_elapsed) : 0LL);

                characters_written += this->append_csvoutput(characters_written, CompetedFramesLength, udp_data.successful_frames.get());
                characters_written += this->append_csvoutput(characters_written, DroppedFramesLength, udp_data.dropped_frames.get());
                characters_written += this->append_csvoutput(characters_written, RepeatFramesLength, udp_data.duplicate_frames.get());
                characters_written += this->append_csvoutput(characters_written, RetryAttemptLength, udp_data.retry_attempts.get());
                characters_written += this->append_csvoutput(characters_written, ErrorFramesLength, udp_data.error_frames.get(), false); // no comma at the end
                this->terminate_string(characters_written);

            } else {
                // converting milliseconds to seconds before printing
                this->right_justify_output(TimeSliceOffset, TimeSliceLength, static_cast<float>(_current_time / 1000.0));
                // calculating # of bytes that were received between the previous format() and current call to format()
                long long time_elapsed = udp_data.end_time.get() - udp_data.start_time.get();
                this->right_justify_output(
                    BitsPerSecondOffset,
                    BitsPerSecondLength,
                    (time_elapsed > 0LL) ? static_cast<long long>(udp_data.bits_received.get() * 1000LL / time_elapsed) : 0LL);

                this->right_justify_output(CompetedFramesOffset, CompetedFramesLength, udp_data.successful_frames.get());
                this->right_justify_output(DroppedFramesOffset, DroppedFramesLength, udp_data.dropped_frames.get());
                this->right_justify_output(RepeatFramesOffset, RepeatFramesLength, udp_data.duplicate_frames.get());
                this->right_justify_output(RetryAttemptOffset, RetryAttemptLength, udp_data.retry_attempts.get());
                this->right_justify_output(ErrorFramesOffset, ErrorFramesLength, udp_data.error_frames.get());
                this->terminate_string(ErrorFramesOffset);
            }
            return PrintingStatus::PrintComplete;
        }


    private:
        // constant offsets for each numeric value to print
        static const unsigned long TimeSliceOffset = 10;
        static const unsigned long TimeSliceLength = 10;

        static const unsigned long BitsPerSecondOffset = 24;
        static const unsigned long BitsPerSecondLength = 12;

        static const unsigned long CompetedFramesOffset = 36;
        static const unsigned long CompetedFramesLength = 9;

        static const unsigned long DroppedFramesOffset = 46;
        static const unsigned long DroppedFramesLength = 7;

        static const unsigned long RepeatFramesOffset = 57;
        static const unsigned long RepeatFramesLength = 7;

        static const unsigned long RetryAttemptOffset = 69;
        static const unsigned long RetryAttemptLength = 7;

        static const unsigned long ErrorFramesOffset = 79;
        static const unsigned long ErrorFramesLength = 7;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Jitter is just a pass-through printing interface
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsJitterStatusInformation : public ctsStatusInformation {
    public:
        ctsJitterStatusInformation() NOEXCEPT : ctsStatusInformation()
        {
        }
        ~ctsJitterStatusInformation() NOEXCEPT
        {
        }

        ///
        /// Pure-Virtual functions required to be defined
        ///
        LPCWSTR format_legend(ctsConfig::StatusFormatting) NOEXCEPT
        {
            return nullptr;
        }

        LPCWSTR format_header(ctsConfig::StatusFormatting _format) NOEXCEPT
        {
            ctl::ctFatalCondition(
                ctsConfig::StatusFormatting::Csv != _format,
                L"ctsJitterStatusInformation only supports CSV output");
            return L"SequenceNumber,SenderQpc,SenderQpf,ReceiverQpc,ReceiverQpf\n";
        }

        PrintingStatus format_data(ctsConfig::StatusFormatting, long long, bool) NOEXCEPT
        {
            return PrintingStatus::NoPrint;
        }
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Print function for TCP connections
    /// - allows an option for 'detailed' status
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctsTcpStatusInformation : public ctsStatusInformation {
    public:
        ctsTcpStatusInformation() NOEXCEPT : ctsStatusInformation()
        {
        }
        ~ctsTcpStatusInformation() NOEXCEPT
        {
        }

        PrintingStatus format_data(ctsConfig::StatusFormatting _format, long long _current_time, bool _clear_status) NOEXCEPT
        {
            ctsTcpStatistics tcp_data(ctsConfig::Settings->TcpStatusDetails.snap_view(_clear_status));
            ctsConnectionStatistics connection_data(ctsConfig::Settings->ConnectionStatusDetails.snap_view(_clear_status));

            long long time_elapsed = tcp_data.end_time.get() - tcp_data.start_time.get();

            if (_format == ctsConfig::StatusFormatting::Csv) {
                unsigned long characters_written = 0;
                // converting milliseconds to seconds before printing
                characters_written += this->append_csvoutput(characters_written, TimeSliceLength, static_cast<float>(_current_time / 1000.0));

                // calculating # of bytes that were sent between the previous format() and current call to format()
                characters_written += this->append_csvoutput(
                    characters_written,
                    SendBytesPerSecondLength,
                    (time_elapsed > 0LL) ? static_cast<unsigned long>(tcp_data.bytes_sent.get() * 1000LL / time_elapsed) : 0LL);
                // calculating # of bytes that were received between the previous format() and current call to format()
                characters_written += this->append_csvoutput(
                    characters_written,
                    RecvBytesPerSecondLength,
                    (time_elapsed > 0LL) ? static_cast<unsigned long>(tcp_data.bytes_recv.get() * 1000LL / time_elapsed) : 0LL);

                characters_written += this->append_csvoutput(characters_written, CurrentTransactionsLength, connection_data.active_connection_count.get());
                characters_written += this->append_csvoutput(characters_written, CompletedTransactionsLength, connection_data.successful_completion_count.get());
                characters_written += this->append_csvoutput(characters_written, ConnectionErrorsLength, connection_data.connection_error_count.get());
                characters_written += this->append_csvoutput(characters_written, ProtocolErrorsLength, connection_data.protocol_error_count.get(), false); // no comma at the end
                this->terminate_string(characters_written);

            } else {
                // converting milliseconds to seconds before printing
                this->right_justify_output(TimeSliceOffset, TimeSliceLength, static_cast<float>(_current_time / 1000.0));

                // calculating # of bytes that were sent between the previous format() and current call to format()
                this->right_justify_output(
                    SendBytesPerSecondOffset,
                    SendBytesPerSecondLength,
                    (time_elapsed > 0LL) ? static_cast<unsigned long>(tcp_data.bytes_sent.get() * 1000LL / time_elapsed) : 0LL);
                // calculating # of bytes that were received between the previous format() and current call to format()
                this->right_justify_output(
                    RecvBytesPerSecondOffset,
                    RecvBytesPerSecondLength,
                    (time_elapsed > 0LL) ? static_cast<unsigned long>(tcp_data.bytes_recv.get() * 1000LL / time_elapsed) : 0LL);

                this->right_justify_output(CurrentTransactionsOffset, CurrentTransactionsLength, connection_data.active_connection_count.get());
                this->right_justify_output(CompletedTransactionsOffset, CompletedTransactionsLength, connection_data.successful_completion_count.get());
                this->right_justify_output(ConnectionErrorsOffset, ConnectionErrorsLength, connection_data.connection_error_count.get());
                this->right_justify_output(ProtocolErrorsOffset, ProtocolErrorsLength, connection_data.protocol_error_count.get());
                this->terminate_string(ProtocolErrorsOffset);
            }

            return PrintingStatus::PrintComplete;
        }

        LPCWSTR format_legend() NOEXCEPT
        {
            return
                L"Legend:\n"
                L"* TimeSlice - (seconds) cumulative runtime\n"
                L"* Send & Recv Rates - bytes/sec that were transferred within the TimeSlice period\n"
                L"* In-Flight - count of established connections transmitting IO pattern data\n"
                L"* Completed - cumulative count of successfully completed IO patterns\n"
                L"* Network Errors - cumulative count of failed IO patterns due to Winsock errors\n"
                L"* Data Errors - cumulative count of failed IO patterns due to data errors\n"
                L"\n";
        }

        LPCWSTR format_header(ctsConfig::StatusFormatting _format) NOEXCEPT
        {
            if (_format == ctsConfig::StatusFormatting::Csv) {
                return
                    L"TimeSlice,SendBps,RecvBps,In-Flight,Completed,NetError,DataError\n";

            } else {
                return
                    L" TimeSlice      SendBps     RecvBps   In-Flight  Completed  NetError  DataError \n";
                ///    00000000.0...0000000000..0000000000.....0000000....0000000...0000000....0000000.        
                ///    1   5    0    5    0    5    0    5    0    5    0    5    0    5    0    5    0 
                ///            10        20        30        40        50        60        70        80
            }
        }

    private:
        // constant offsets for each numeric value to print
        static const int TimeSliceOffset = 10;
        static const int TimeSliceLength = 10;

        static const int SendBytesPerSecondOffset = 23;
        static const int SendBytesPerSecondLength = 10;

        static const int RecvBytesPerSecondOffset = 35;
        static const int RecvBytesPerSecondLength = 10;

        static const int CurrentTransactionsOffset = 47;
        static const int CurrentTransactionsLength = 7;

        static const int CompletedTransactionsOffset = 58;
        static const int CompletedTransactionsLength = 7;

        static const int ConnectionErrorsOffset = 68;
        static const int ConnectionErrorsLength = 7;

        static const int ProtocolErrorsOffset = 79;
        static const int ProtocolErrorsLength = 7;

        static const int DetailedSentOffset = 23;
        static const int DetailedSentLength = 10;

        static const int DetailedRecvOffset = 35;
        static const int DetailedRecvLength = 10;

        static const int DetailedAddressOffset = 39;
        static const int DetailedAddressLength = 46;
    };


} // namespace
