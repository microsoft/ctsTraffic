/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <cwchar>
// os headers
#include <Windows.h>
// project headers
#include "ctsConfig.h"

namespace ctsTraffic
{
// Abstract base class for status - printing classes
class ctsStatusInformation
{
protected:
    enum class PrintingStatus : std::uint8_t
    {
        PrintComplete,
        NoPrint
    };

private:
    // expanded beyond 80 to handle very long IPv6 address strings
    // - buffer is expected to be protected by only a single caller at a time
    static constexpr uint32_t c_outputBufferSize = 128;
    // one more for the null terminator
    wchar_t m_outputBuffer[c_outputBufferSize + 1]{};

    void ResetBuffer() noexcept
    {
        // fill the output buffer with spaces and null terminate
        wmemset(m_outputBuffer, L' ', c_outputBufferSize);
        m_outputBuffer[c_outputBufferSize] = L'\0';
    }

public:
    ctsStatusInformation() noexcept = default;
    virtual ~ctsStatusInformation() noexcept = default;
    // base class is movable
    ctsStatusInformation(ctsStatusInformation&& movedFrom) noexcept
    {
        wmemcpy_s(m_outputBuffer, c_outputBufferSize + 1, movedFrom.m_outputBuffer, c_outputBufferSize + 1);
        movedFrom.ResetBuffer();
    }

    ctsStatusInformation(const ctsStatusInformation&) = delete;
    ctsStatusInformation& operator=(const ctsStatusInformation&) = delete;
    ctsStatusInformation& operator=(ctsStatusInformation&&) = delete;

    PCWSTR PrintLegend(const ctsConfig::StatusFormatting& format) noexcept
    {
        return ctsConfig::StatusFormatting::Csv == format ?
               nullptr :
               FormatLegend(format);
    }

    PCWSTR PrintHeader(const ctsConfig::StatusFormatting& format) noexcept
    {
        return FormatHeader(format);
    }

    //
    // Expects to be called in a loop
    // - returns nullptr if nothing left to print
    //
    PCWSTR PrintStatus(const ctsConfig::StatusFormatting& format, int64_t currentTime, bool clearStatus) noexcept
    {
        ResetBuffer();
        if (FormatData(format, currentTime, clearStatus) != PrintingStatus::NoPrint)
        {
            return m_outputBuffer;
        }
        return nullptr;
    }

    // 

protected:
    // derived classes are required to implement these three pure virtual function
    virtual PrintingStatus FormatData(const ctsConfig::StatusFormatting& format, int64_t currentTime, bool clearStatus) noexcept = 0;
    virtual PCWSTR FormatLegend(const ctsConfig::StatusFormatting& format) noexcept = 0;
    virtual PCWSTR FormatHeader(const ctsConfig::StatusFormatting& format) noexcept = 0;

    void LeftJustifyOutput(uint32_t leftJustifiedOffset, uint32_t maxLength, _In_ PCWSTR value) noexcept
    {
        FAIL_FAST_IF_MSG(
            0 == leftJustifiedOffset,
            "ctsStatusInformation was given a zero offset in left_justify_output : must be at least 1");
        FAIL_FAST_IF_MSG(
            leftJustifiedOffset > c_outputBufferSize,
            "ctsStatusInformation will only print up to %u columns - an offset of %u was given",
            c_outputBufferSize, leftJustifiedOffset);

        const size_t valueLength = wcslen(value);
        FAIL_FAST_IF_MSG(
            valueLength > maxLength,
            "ctsStatusInformation was given a string longer than the max value given (%u) -- '%ws'",
            maxLength, value);

        wmemcpy_s(
            m_outputBuffer + leftJustifiedOffset - 1,
            c_outputBufferSize - leftJustifiedOffset - 1,
            value,
            valueLength);
    }

    void RightJustifyOutput(uint32_t rightJustifiedOffset, uint32_t maxLength, float value) noexcept
    {
        constexpr uint32_t conversionBufferLength = 16;
        wchar_t conversionBuffer[conversionBufferLength]{};

        FAIL_FAST_IF_MSG(
            rightJustifiedOffset > c_outputBufferSize,
            "ctsStatusInformation will only print up to %u columns - an offset of %u was given",
            c_outputBufferSize, rightJustifiedOffset);
        _Analysis_assume_(rightJustifiedOffset <= c_outputBufferSize);

        FAIL_FAST_IF_MSG(
            maxLength > conversionBufferLength - 1, // minus one for the null terminator
            "ctsStatusInformation will only print converted strings up to %u characters long - the number '%u' was given",
            conversionBufferLength - 1, maxLength);
        _Analysis_assume_(maxLength <= conversionBufferLength - 1);

        const auto converted = _snwprintf_s(
            conversionBuffer,
            conversionBufferLength,
            L"%.3f",
            value);
        FAIL_FAST_IF(-1 == converted);
        _Analysis_assume_(converted != -1);

        wmemcpy_s(
            m_outputBuffer + (rightJustifiedOffset - converted),
            c_outputBufferSize - (rightJustifiedOffset - converted),
            conversionBuffer,
            converted);
    }

    void RightJustifyOutput(uint32_t rightJustifiedOffset, uint32_t maxLength, uint32_t value) noexcept
    {
        constexpr uint32_t conversionBufferLength = 12;
        wchar_t conversionBuffer[conversionBufferLength]{};

        FAIL_FAST_IF_MSG(
            rightJustifiedOffset > c_outputBufferSize,
            "ctsStatusInformation will only print up to %u columns - an offset of %u was given",
            c_outputBufferSize, rightJustifiedOffset);
        _Analysis_assume_(rightJustifiedOffset <= c_outputBufferSize);

        FAIL_FAST_IF_MSG(
            maxLength > conversionBufferLength - 1, // minus one for the null terminator
            "ctsStatusInformation will only print converted strings up to %u characters long - the number '%u' was given",
            conversionBufferLength - 1, maxLength);
        _Analysis_assume_(maxLength > conversionBufferLength - 1);

        const auto converted = _snwprintf_s(
            conversionBuffer,
            conversionBufferLength,
            L"%u",
            value);
        FAIL_FAST_IF(-1 == converted);
        _Analysis_assume_(converted != -1);

        wmemcpy_s(
            m_outputBuffer + (rightJustifiedOffset - converted),
            c_outputBufferSize - (rightJustifiedOffset - converted),
            conversionBuffer,
            converted);
    }

    void RightJustifyOutput(uint32_t rightJustifiedOffset, uint32_t maxLength, int64_t value) noexcept
    {
        constexpr uint32_t conversionBufferLength = 20;
        wchar_t conversionBuffer[conversionBufferLength]{};

        FAIL_FAST_IF_MSG(
            value < 0LL,
            "ctsStatusInformation output was given a negative value to print (or greater than MAXLONGLONG): %llx",
            value);
        _Analysis_assume_(value >= 0LL);

        FAIL_FAST_IF_MSG(
            rightJustifiedOffset > c_outputBufferSize,
            "ctsStatusInformation will only print up to %u columns - an offset of %u was given",
            c_outputBufferSize, rightJustifiedOffset);
        _Analysis_assume_(rightJustifiedOffset <= c_outputBufferSize);

        FAIL_FAST_IF_MSG(
            maxLength > conversionBufferLength - 1, // minus one for the null terminator
            "ctsStatusInformation will only print converted strings up to %u characters long - the number '%u' was given",
            conversionBufferLength - 1, maxLength);
        _Analysis_assume_(maxLength <= conversionBufferLength - 1);

        const auto converted = _snwprintf_s(
            conversionBuffer,
            conversionBufferLength,
            L"%lld",
            value);
        FAIL_FAST_IF(-1 == converted);
        _Analysis_assume_(converted != -1);

        wmemcpy_s(
            m_outputBuffer + (rightJustifiedOffset - converted),
            c_outputBufferSize - (rightJustifiedOffset - converted),
            conversionBuffer,
            converted);
    }

    constexpr void TerminateString(uint32_t offset) noexcept
    {
        m_outputBuffer[offset] = L'\n';
        m_outputBuffer[offset + 1] = L'\0';
    }

    constexpr void TerminateFileString(uint32_t offset) noexcept
    {
        m_outputBuffer[offset] = L'\r';
        m_outputBuffer[offset + 1] = L'\n';
        m_outputBuffer[offset + 2] = L'\0';
    }

    //
    // Functions to write to the output buffer in CSV formatting
    //
    uint32_t AppendCsvOutput(uint32_t offset, uint32_t valueLength, float value, bool addComma = true) noexcept
    {
        const auto converted = _snwprintf_s(
            m_outputBuffer + offset,
            c_outputBufferSize - offset,
            addComma ? valueLength + 1 : valueLength,
            addComma ? L"%.3f," : L"%.3f",
            value);
        FAIL_FAST_IF(-1 == converted);
        return converted;
    }

    uint32_t AppendCsvOutput(uint32_t offset, uint32_t valueLength, uint32_t value, bool addComma = true) noexcept
    {
        const errno_t error = _ui64tow_s(
            value,
            m_outputBuffer + offset,
            c_outputBufferSize - offset,
            10);
        FAIL_FAST_IF_MSG(
            error != 0,
            "_ui64tow_s failed to convert this (%p) ctsUdpStatusInformation - %u", this, value);

        // find how many characters were printed
        uint32_t converted = 0;
        wchar_t* outputReference = m_outputBuffer + offset;
        while (*outputReference != L'\0' && *outputReference != L' ')
        {
            ++converted;
            ++outputReference;
        }

        FAIL_FAST_IF_MSG(
            converted > c_outputBufferSize - offset,
            "Counting the string built by _ui64tow_s overflowed - converted (%u) _offset (%u) : ctsUdpStatusInformation (%p)\n", converted, offset, this);
        FAIL_FAST_IF_MSG(
            converted > valueLength,
            "Counting the string built by _ui64tow_s was greater than _value_length (%u) : ctsUdpStatusInformation (%p)\n", valueLength, this);

        if (addComma)
        {
            ++converted;
            *outputReference = L',';
        }
        return converted;
    }

    uint32_t AppendCsvOutput(uint32_t offset, uint32_t valueLength, int64_t value, bool addComma = true) noexcept
    {
        const errno_t error = _ui64tow_s(
            value,
            m_outputBuffer + offset,
            c_outputBufferSize - offset,
            10);
        FAIL_FAST_IF_MSG(
            error != 0,
            "_ui64tow_s failed to convert this (%p) ctsUdpStatusInformation - %lld", this, value);

        // find how many characters were printed
        uint32_t converted = 0;
        wchar_t* outputReference = m_outputBuffer + offset;
        while (*outputReference != L'\0' && *outputReference != L' ')
        {
            ++converted;
            ++outputReference;
        }

        FAIL_FAST_IF_MSG(
            converted > c_outputBufferSize - offset,
            "Counting the string built by _ui64tow_s overflowed - converted (%u) _offset (%u) : ctsUdpStatusInformation (%p)\n", converted, offset, this);
        FAIL_FAST_IF_MSG(
            converted > valueLength,
            "Counting the string built by _ui64tow_s was greater than _value_length (%u) : ctsUdpStatusInformation (%p)\n", valueLength, this);

        if (addComma)
        {
            ++converted;
            *outputReference = L',';
        }
        return converted;
    }

    uint32_t AppendCsvOutput(uint32_t offset, uint32_t valueLength, _In_ PCWSTR value, bool addComma = true) noexcept
    {
        const auto converted = _snwprintf_s(
            m_outputBuffer + offset,
            c_outputBufferSize - offset,
            addComma ? valueLength + 1 : valueLength,
            addComma ? L"%ws," : L"%ws",
            value);
        FAIL_FAST_IF(-1 == converted);
        return converted;
    }
};

//
// All variables are updated with Interlocked* operations
//   as it's more important to remain responsive than to guarantee 
//   all information is reflected in the precise printed line
// - note that *no* information will be lost 
//   all data will be accounted for in either the current printed line
//   or in the next printed line
//
class ctsUdpStatusInformation final : public ctsStatusInformation
{
public:
    ctsUdpStatusInformation() noexcept = default;
    ~ctsUdpStatusInformation() noexcept override = default;
    ctsUdpStatusInformation(const ctsUdpStatusInformation&) = delete;
    ctsUdpStatusInformation(ctsUdpStatusInformation&&) = default;
    ctsUdpStatusInformation& operator=(const ctsUdpStatusInformation&) = delete;
    ctsUdpStatusInformation& operator=(ctsUdpStatusInformation&&) = delete;

    //
    // Pure-Virtual functions required to be defined
    //
    PCWSTR FormatLegend(const ctsConfig::StatusFormatting& format) noexcept override
    {
        if (ctsConfig::StatusFormatting::ConsoleOutput == format)
        {
            return
                L"Legend:\n"
                L"* TimeSlice - (seconds) cumulative runtime\n"
                L"* Streams - count of current number of UDP streams\n"
                L"* Bits/Sec - bits streamed within the TimeSlice period\n"
                L"* Completed Frames - count of frames successfully processed within the TimeSlice\n"
                L"* Dropped Frames - count of frames that were never seen within the TimeSlice\n"
                L"* Repeated Frames - count of frames received multiple times within the TimeSlice\n"
                L"* Stream Errors - count of invalid frames or buffers within the TimeSlice\n"
                L"\n";
        }
        return
            L"Legend:\r\n"
            L"* TimeSlice - (seconds) cumulative runtime\r\n"
            L"* Streams - count of current number of UDP streams\r\n"
            L"* Bits/Sec - bits streamed within the TimeSlice period\r\n"
            L"* Completed Frames - count of frames successfully processed within the TimeSlice\r\n"
            L"* Dropped Frames - count of frames that were never seen within the TimeSlice\r\n"
            L"* Repeated Frames - count of frames received multiple times within the TimeSlice\r\n"
            L"* Stream Errors - count of invalid frames or buffers within the TimeSlice\r\n"
            L"\r\n";
    }

    PCWSTR FormatHeader(const ctsConfig::StatusFormatting& format) noexcept override
    {
        if (ctsConfig::StatusFormatting::Csv == format)
        {
            return
                L"TimeSlice,Bits/Sec,Streams,Completed,Dropped,Repeated,Errors\r\n";
        }

        if (ctsConfig::StatusFormatting::ConsoleOutput == format)
        {
            // Formatted to fit on an 80-column command shell
            return
                L" TimeSlice       Bits/Sec    Streams   Completed   Dropped   Repeated    Errors \n";
            // 00000000.0...000000000000...00000000...000000000...0000000...00000000...0000000.        
            // 1   5    0    5    0    5    0    5    0    5    0    5    0    5    0    5    0 
            //         10        20        30        40        50        60        70        80
        }

        return
            L" TimeSlice       Bits/Sec    Streams   Completed   Dropped   Repeated    Errors \r\n";
    }

    PrintingStatus FormatData(const ctsConfig::StatusFormatting& format, int64_t currentTime, bool clearStatus) noexcept override
    {
        const ctsUdpStatistics udpData(ctsConfig::g_configSettings->UdpStatusDetails.SnapView(clearStatus));
        const ctsConnectionStatistics connectionData(ctsConfig::g_configSettings->ConnectionStatusDetails.SnapView(clearStatus));
        // can call GetValueNoLock on both Statistics objects since they are copies

        if (ctsConfig::StatusFormatting::Csv == format)
        {
            uint32_t charactersWritten = 0;
            // converting milliseconds to seconds before printing
            charactersWritten += AppendCsvOutput(charactersWritten, c_timeSliceLength, static_cast<float>(currentTime) / 1000.0f);
            // calculating # of bytes that were received between the previous format() and current call to format()
            const int64_t timeElapsed = udpData.m_endTime.GetValueNoLock() - udpData.m_startTime.GetValueNoLock();
            charactersWritten += AppendCsvOutput(
                charactersWritten,
                c_bitsPerSecondLength,
                timeElapsed > 0LL ? udpData.m_bitsReceived.GetValueNoLock() * 1000LL / timeElapsed : 0LL);

            charactersWritten += AppendCsvOutput(charactersWritten, c_currentStreamsLength, connectionData.m_activeConnectionCount.GetValueNoLock());
            charactersWritten += AppendCsvOutput(charactersWritten, c_competedFramesLength, udpData.m_successfulFrames.GetValueNoLock());
            charactersWritten += AppendCsvOutput(charactersWritten, c_droppedFramesLength, udpData.m_droppedFrames.GetValueNoLock());
            charactersWritten += AppendCsvOutput(charactersWritten, c_duplicatedFramesLength, udpData.m_duplicateFrames.GetValueNoLock());
            charactersWritten += AppendCsvOutput(charactersWritten, c_errorFramesLength, udpData.m_errorFrames.GetValueNoLock(), false); // no comma at the end
            TerminateFileString(charactersWritten);
        }
        else
        {
            // converting milliseconds to seconds before printing
            RightJustifyOutput(c_timeSliceOffset, c_timeSliceLength, static_cast<float>(currentTime) / 1000.0f);
            // calculating # of bytes that were received between the previous format() and current call to format()
            const int64_t timeElapsed = udpData.m_endTime.GetValueNoLock() - udpData.m_startTime.GetValueNoLock();
            RightJustifyOutput(
                c_bitsPerSecondOffset,
                c_bitsPerSecondLength,
                timeElapsed > 0LL ? udpData.m_bitsReceived.GetValueNoLock() * 1000LL / timeElapsed : 0LL);

            RightJustifyOutput(c_currentStreamsOffset, c_currentStreamsLength, connectionData.m_activeConnectionCount.GetValueNoLock());
            RightJustifyOutput(c_competedFramesOffset, c_competedFramesLength, udpData.m_successfulFrames.GetValueNoLock());
            RightJustifyOutput(c_droppedFramesOffset, c_droppedFramesLength, udpData.m_droppedFrames.GetValueNoLock());
            RightJustifyOutput(c_duplicatedFramesOffset, c_duplicatedFramesLength, udpData.m_duplicateFrames.GetValueNoLock());
            RightJustifyOutput(c_errorFramesOffset, c_errorFramesLength, udpData.m_errorFrames.GetValueNoLock());
            if (format == ctsConfig::StatusFormatting::ConsoleOutput)
            {
                TerminateString(c_errorFramesOffset);
            }
            else
            {
                TerminateFileString(c_errorFramesOffset);
            }
        }
        return PrintingStatus::PrintComplete;
    }


private:
    // constant offsets for each numeric value to print
    static constexpr uint32_t c_timeSliceOffset = 10;
    static constexpr uint32_t c_timeSliceLength = 10;

    static constexpr uint32_t c_bitsPerSecondOffset = 25;
    static constexpr uint32_t c_bitsPerSecondLength = 12;

    static constexpr uint32_t c_currentStreamsOffset = 36;
    static constexpr uint32_t c_currentStreamsLength = 8;

    static constexpr uint32_t c_competedFramesOffset = 48;
    static constexpr uint32_t c_competedFramesLength = 9;

    static constexpr uint32_t c_droppedFramesOffset = 58;
    static constexpr uint32_t c_droppedFramesLength = 7;

    static constexpr uint32_t c_duplicatedFramesOffset = 69;
    static constexpr uint32_t c_duplicatedFramesLength = 7;

    static constexpr uint32_t c_errorFramesOffset = 79;
    static constexpr uint32_t c_errorFramesLength = 7;
};

//
// Print function for TCP connections
// - allows an option for 'detailed' status
//
class ctsTcpStatusInformation final : public ctsStatusInformation
{
public:
    ctsTcpStatusInformation() noexcept = default;
    ~ctsTcpStatusInformation() noexcept override = default;
    ctsTcpStatusInformation(const ctsTcpStatusInformation&) = delete;
    ctsTcpStatusInformation& operator=(const ctsTcpStatusInformation&) = delete;
    ctsTcpStatusInformation(ctsTcpStatusInformation&&) = delete;
    ctsTcpStatusInformation& operator=(ctsTcpStatusInformation&&) = delete;

    PrintingStatus FormatData(const ctsConfig::StatusFormatting& format, int64_t currentTime, bool clearStatus) noexcept override
    {
        const ctsTcpStatistics tcpData(ctsConfig::g_configSettings->TcpStatusDetails.SnapView(clearStatus));
        const ctsConnectionStatistics connectionData(ctsConfig::g_configSettings->ConnectionStatusDetails.SnapView(clearStatus));
        // can call GetValueNoLock on both Statistics objects since they are copies

        const int64_t timeElapsed = tcpData.m_endTime.GetValueNoLock() - tcpData.m_startTime.GetValueNoLock();

        if (format == ctsConfig::StatusFormatting::Csv)
        {
            uint32_t charactersWritten = 0;
            // converting milliseconds to seconds before printing
            charactersWritten += AppendCsvOutput(charactersWritten, c_timeSliceLength, static_cast<float>(currentTime) / 1000.0f);

            // calculating # of bytes that were sent between the previous format() and current call to format()
            charactersWritten += AppendCsvOutput(
                charactersWritten,
                c_sendBytesPerSecondLength,
                timeElapsed > 0LL ? tcpData.m_bytesSent.GetValueNoLock() * 1000LL / timeElapsed : 0LL);
            // calculating # of bytes that were received between the previous format() and current call to format()
            charactersWritten += AppendCsvOutput(
                charactersWritten,
                c_recvBytesPerSecondLength,
                timeElapsed > 0LL ? tcpData.m_bytesRecv.GetValueNoLock() * 1000LL / timeElapsed : 0LL);

            charactersWritten += AppendCsvOutput(charactersWritten, c_currentTransactionsLength, connectionData.m_activeConnectionCount.GetValueNoLock());
            charactersWritten += AppendCsvOutput(charactersWritten, c_completedTransactionsLength, connectionData.m_successfulCompletionCount.GetValueNoLock());
            charactersWritten += AppendCsvOutput(charactersWritten, c_connectionErrorsLength, connectionData.m_connectionErrorCount.GetValueNoLock());
            charactersWritten += AppendCsvOutput(charactersWritten, c_protocolErrorsLength, connectionData.m_protocolErrorCount.GetValueNoLock(), false); // no comma at the end
            TerminateFileString(charactersWritten);
        }
        else
        {
            // converting milliseconds to seconds before printing
            RightJustifyOutput(c_timeSliceOffset, c_timeSliceLength, static_cast<float>(currentTime) / 1000.0f);

            // calculating # of bytes that were sent between the previous format() and current call to format()
            RightJustifyOutput(
                c_sendBytesPerSecondOffset,
                c_sendBytesPerSecondLength,
                timeElapsed > 0LL ? tcpData.m_bytesSent.GetValueNoLock() * 1000LL / timeElapsed : 0LL);
            // calculating # of bytes that were received between the previous format() and current call to format()
            RightJustifyOutput(
                c_recvBytesPerSecondOffset,
                c_recvBytesPerSecondLength,
                timeElapsed > 0LL ? tcpData.m_bytesRecv.GetValueNoLock() * 1000LL / timeElapsed : 0LL);

            RightJustifyOutput(c_currentTransactionsOffset, c_currentTransactionsLength, connectionData.m_activeConnectionCount.GetValueNoLock());
            RightJustifyOutput(c_completedTransactionsOffset, c_completedTransactionsLength, connectionData.m_successfulCompletionCount.GetValueNoLock());
            RightJustifyOutput(c_connectionErrorsOffset, c_connectionErrorsLength, connectionData.m_connectionErrorCount.GetValueNoLock());
            RightJustifyOutput(c_protocolErrorsOffset, c_protocolErrorsLength, connectionData.m_protocolErrorCount.GetValueNoLock());
            if (format == ctsConfig::StatusFormatting::ConsoleOutput)
            {
                TerminateString(c_protocolErrorsOffset);
            }
            else
            {
                TerminateFileString(c_protocolErrorsOffset);
            }
        }

        return PrintingStatus::PrintComplete;
    }

    PCWSTR FormatLegend(const ctsConfig::StatusFormatting& format) noexcept override
    {
        if (ctsConfig::StatusFormatting::ConsoleOutput == format)
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

        return
            L"Legend:\r\n"
            L"* TimeSlice - (seconds) cumulative runtime\r\n"
            L"* Send & Recv Rates - bytes/sec that were transferred within the TimeSlice period\r\n"
            L"* In-Flight - count of established connections transmitting IO pattern data\r\n"
            L"* Completed - cumulative count of successfully completed IO patterns\r\n"
            L"* Network Errors - cumulative count of failed IO patterns due to Winsock errors\r\n"
            L"* Data Errors - cumulative count of failed IO patterns due to data errors\r\n"
            L"\r\n";
    }

    PCWSTR FormatHeader(const ctsConfig::StatusFormatting& format) noexcept override
    {
        if (format == ctsConfig::StatusFormatting::Csv)
        {
            return
                L"TimeSlice,SendBps,RecvBps,In-Flight,Completed,NetError,DataError\r\n";
        }

        if (format == ctsConfig::StatusFormatting::ConsoleOutput)
        {
            return
                L" TimeSlice      SendBps      RecvBps  In-Flight  Completed  NetError  DataError \n";
            //    00000000.0..00000000000..00000000000....0000000....0000000...0000000....0000000.        
            //    1   5    0    5    0    5    0    5    0    5    0    5    0    5    0    5    0 
            //            10        20        30        40        50        60        70        80
        }

        return L" TimeSlice      SendBps      RecvBps  In-Flight  Completed  NetError  DataError \r\n";
    }

private:
    // constant offsets for each numeric value to print
    static constexpr uint32_t c_timeSliceOffset = 10;
    static constexpr uint32_t c_timeSliceLength = 10;

    static constexpr uint32_t c_sendBytesPerSecondOffset = 23;
    static constexpr uint32_t c_sendBytesPerSecondLength = 11;

    static constexpr uint32_t c_recvBytesPerSecondOffset = 36;
    static constexpr uint32_t c_recvBytesPerSecondLength = 11;

    static constexpr uint32_t c_currentTransactionsOffset = 47;
    static constexpr uint32_t c_currentTransactionsLength = 7;

    static constexpr uint32_t c_completedTransactionsOffset = 58;
    static constexpr uint32_t c_completedTransactionsLength = 7;

    static constexpr uint32_t c_connectionErrorsOffset = 68;
    static constexpr uint32_t c_connectionErrorsLength = 7;

    static constexpr uint32_t c_protocolErrorsOffset = 79;
    static constexpr uint32_t c_protocolErrorsLength = 7;

    static constexpr uint32_t c_detailedSentOffset = 23;
    static constexpr uint32_t c_detailedSentLength = 10;

    static constexpr uint32_t c_detailedRecvOffset = 35;
    static constexpr uint32_t c_detailedRecvLength = 10;

    static constexpr uint32_t c_detailedAddressOffset = 39;
    static constexpr uint32_t c_detailedAddressLength = 46;
};
} // namespace
