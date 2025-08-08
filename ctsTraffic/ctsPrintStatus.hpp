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
		// Buffer is expected to be protected by only a single caller at a time
		static constexpr uint32_t c_outputBufferSize = 1024;
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
			return format == ctsConfig::StatusFormatting::Csv ?
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

		static constexpr uint32_t ConversionBufferLength = 32; // buffer large enough to print any type we support
		static auto PrintToBuffer(wchar_t(&conversionBuffer)[ConversionBufferLength], uint32_t value) noexcept
		{
			return _snwprintf_s(
				conversionBuffer,
				ConversionBufferLength,
				L"%lu",
				value);
		}
		static auto PrintToBuffer(wchar_t(&conversionBuffer)[ConversionBufferLength], int64_t value) noexcept
		{
			return _snwprintf_s(
				conversionBuffer,
				ConversionBufferLength,
				L"%lld",
				value);
		}
		static auto PrintToBuffer(wchar_t(&conversionBuffer)[ConversionBufferLength], float value) noexcept
		{
			return _snwprintf_s(
				conversionBuffer,
				ConversionBufferLength,
				L"%.3f",
				value);
		}

		template <typename T>
		static auto PrintToBufferWithExponentOf6(wchar_t(&conversionBuffer)[ConversionBufferLength], T value) noexcept
		{
			// if we can't express the value in the given maxLength
			// we'll convert it to units of millions
			double float_value = 0.0 + value;
			float_value /= 0.0 + 1000000;

			return _snwprintf_s(
				conversionBuffer,
				ConversionBufferLength,
				L"%.1fx^6",
				float_value);
		}
		template <typename T>
		static auto PrintToBufferWithExponentOf9(wchar_t(&conversionBuffer)[ConversionBufferLength], T value) noexcept
		{
			// if we can't express the value in the given maxLength
			// we'll convert it to units of millions
			double float_value = 0.0 + value;
			float_value /= 0.0 + 1000000000;

			return _snwprintf_s(
				conversionBuffer,
				ConversionBufferLength,
				L"%.1fx^9",
				float_value);
		}
		template <typename T>
		static auto PrintToBufferWithExponentOf12(wchar_t(&conversionBuffer)[ConversionBufferLength], T value) noexcept
		{
			// if we can't express the value in the given maxLength
			// we'll convert it to units of millions
			double float_value = 0.0 + value;
			float_value /= 0.0 + 1000000000000;

			return _snwprintf_s(
				conversionBuffer,
				ConversionBufferLength,
				L"%.1fx^12",
				float_value);
		}

	    template <typename T>
		void RightJustifyOutput(uint32_t rightJustifiedOffset, uint32_t maxLength, T value) noexcept
		{
			wchar_t conversionBuffer[ConversionBufferLength]{};

			FAIL_FAST_IF_MSG(
				rightJustifiedOffset > c_outputBufferSize,
				"ctsStatusInformation will only print up to %u columns - an offset of %u was given",
				c_outputBufferSize, rightJustifiedOffset);
			_Analysis_assume_(rightJustifiedOffset <= c_outputBufferSize);

			FAIL_FAST_IF_MSG(
				maxLength > ConversionBufferLength - 1, // minus one for the null terminator
				"ctsStatusInformation will only print converted strings up to %u characters long - the number '%u' was given",
				ConversionBufferLength - 1, maxLength);
			_Analysis_assume_(maxLength <= ConversionBufferLength - 1);

			auto converted = PrintToBuffer(conversionBuffer, value);
			if (converted < 0)
			{
				// something went wrong, we can't print the value
				return;
			}

			if (static_cast<uint32_t>(converted) > maxLength)
			{
				converted = PrintToBufferWithExponentOf6(conversionBuffer, value);
				if (converted < 0)
				{
					// something went wrong, we can't print the value
					return;
				}
			}
			if (static_cast<uint32_t>(converted) > maxLength)
			{
				converted = PrintToBufferWithExponentOf9(conversionBuffer, value);
				if (converted < 0)
				{
					// something went wrong, we can't print the value
					return;
				}
			}
			if (static_cast<uint32_t>(converted) > maxLength)
			{
				converted = PrintToBufferWithExponentOf12(conversionBuffer, value);
				if (converted < 0)
				{
					// something went wrong, we can't print the value
					return;
				}
			}
			if (static_cast<uint32_t>(converted) > maxLength)
			{
				// if it still can't fit, just write "9.99+" to indicate a value larger than can be printed
				converted = _snwprintf_s(
					conversionBuffer,
					ConversionBufferLength,
					L"9+++T");
			}

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

			return (-1 == converted) ? 0 : converted;
		}

		template <typename T>
		uint32_t AppendCsvOutput(uint32_t offset, T value, bool addComma = true) noexcept
		{
			const auto writableBufferSizeCharacters = c_outputBufferSize - offset;
			const auto writableBufferSizeBytes = writableBufferSizeCharacters * sizeof(wchar_t);

			if (writableBufferSizeCharacters < 2)
			{
				// no buffer left to write to
				return 0;
			}

			uint32_t convertedCharacterCount = 0;
			wchar_t* outputBufferPointer = m_outputBuffer + offset;

			memset(outputBufferPointer, 0, writableBufferSizeBytes);
			const auto error = _ui64tow_s(value, outputBufferPointer, writableBufferSizeCharacters, 10);
			if (error != 0)
			{
				// something failed - and the only failure path is the buffer was not large enough for the number
				// so we will write "9999999+" to the buffer to indicate a value larger than can be printed
				// where we write (bufferSize - 2) '9' characters to allow room for the '+' and possible comma
				for (uint32_t i = 0; i < writableBufferSizeCharacters - 2; ++i)
				{
					*outputBufferPointer = L'9';
					++outputBufferPointer;
					++convertedCharacterCount;
				}
				if (convertedCharacterCount < writableBufferSizeCharacters)
				{
					*outputBufferPointer = L'+';
					++convertedCharacterCount;
				}
			}
			else
			{
				// count how many characters were printed
				while (convertedCharacterCount < writableBufferSizeCharacters && *outputBufferPointer != L'\0' && *outputBufferPointer != L' ')
				{
					++convertedCharacterCount;
					++outputBufferPointer;
				}
			}

			if (convertedCharacterCount < writableBufferSizeCharacters && addComma)
			{
				++convertedCharacterCount;
				*outputBufferPointer = L',';
			}

			return convertedCharacterCount;
		}
	};

	class ctsUdpStatusInformation final : public ctsStatusInformation
	{
	public:
		ctsUdpStatusInformation() noexcept = default;
		~ctsUdpStatusInformation() noexcept override = default;
		ctsUdpStatusInformation(const ctsUdpStatusInformation&) = delete;
		ctsUdpStatusInformation(ctsUdpStatusInformation&&) = default;
		ctsUdpStatusInformation& operator=(const ctsUdpStatusInformation&) = delete;
		ctsUdpStatusInformation& operator=(ctsUdpStatusInformation&&) = delete;

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

			if (ctsConfig::StatusFormatting::Csv == format)
			{
				uint32_t charactersWritten = 0;
				// converting milliseconds to seconds before printing
				charactersWritten += AppendCsvOutput(charactersWritten, c_timeSliceLength, static_cast<float>(currentTime) / 1000.0f);
				// calculating # of bytes that were received between the previous format() and current call to format()
				const int64_t timeElapsed = udpData.m_endTime.GetValue() - udpData.m_startTime.GetValue();
				charactersWritten += AppendCsvOutput(
					charactersWritten,
					timeElapsed > 0LL ? udpData.m_bitsReceived.GetValue() * 1000LL / timeElapsed : 0LL);

				charactersWritten += AppendCsvOutput(charactersWritten, connectionData.m_activeConnectionCount.GetValue());
				charactersWritten += AppendCsvOutput(charactersWritten, udpData.m_successfulFrames.GetValue());
				charactersWritten += AppendCsvOutput(charactersWritten, udpData.m_droppedFrames.GetValue());
				charactersWritten += AppendCsvOutput(charactersWritten, udpData.m_duplicateFrames.GetValue());
				charactersWritten += AppendCsvOutput(charactersWritten, udpData.m_errorFrames.GetValue(), false); // no comma at the end
				TerminateFileString(charactersWritten);
			}
			else
			{
				// converting milliseconds to seconds before printing
				RightJustifyOutput(c_timeSliceOffset, c_timeSliceLength, static_cast<float>(currentTime) / 1000.0f);
				// calculating # of bytes that were received between the previous format() and current call to format()
				const int64_t timeElapsed = udpData.m_endTime.GetValue() - udpData.m_startTime.GetValue();
				RightJustifyOutput(
					c_bitsPerSecondOffset,
					c_bitsPerSecondLength,
					timeElapsed > 0LL ? udpData.m_bitsReceived.GetValue() * 1000LL / timeElapsed : 0LL);

				RightJustifyOutput(c_currentStreamsOffset, c_currentStreamsLength, connectionData.m_activeConnectionCount.GetValue());
				RightJustifyOutput(c_competedFramesOffset, c_competedFramesLength, udpData.m_successfulFrames.GetValue());
				RightJustifyOutput(c_droppedFramesOffset, c_droppedFramesLength, udpData.m_droppedFrames.GetValue());
				RightJustifyOutput(c_duplicatedFramesOffset, c_duplicatedFramesLength, udpData.m_duplicateFrames.GetValue());
				RightJustifyOutput(c_errorFramesOffset, c_errorFramesLength, udpData.m_errorFrames.GetValue());
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
			const int64_t timeElapsed = tcpData.m_endTime.GetValue() - tcpData.m_startTime.GetValue();

			if (format == ctsConfig::StatusFormatting::Csv)
			{
				uint32_t charactersWritten = 0;
				// converting milliseconds to seconds before printing
				charactersWritten += AppendCsvOutput(charactersWritten, c_timeSliceLength, static_cast<float>(currentTime) / 1000.0f);

				// calculating # of bytes that were sent between the previous format() and current call to format()
				charactersWritten += AppendCsvOutput(
					charactersWritten,
					timeElapsed > 0LL ? tcpData.m_bytesSent.GetValue() * 1000LL / timeElapsed : 0LL);
				// calculating # of bytes that were received between the previous format() and current call to format()
				charactersWritten += AppendCsvOutput(
					charactersWritten,
					timeElapsed > 0LL ? tcpData.m_bytesRecv.GetValue() * 1000LL / timeElapsed : 0LL);

				charactersWritten += AppendCsvOutput(charactersWritten, connectionData.m_activeConnectionCount.GetValue());
				charactersWritten += AppendCsvOutput(charactersWritten, connectionData.m_successfulCompletionCount.GetValue());
				charactersWritten += AppendCsvOutput(charactersWritten, connectionData.m_connectionErrorCount.GetValue());
				charactersWritten += AppendCsvOutput(charactersWritten, connectionData.m_protocolErrorCount.GetValue(), false); // no comma at the end
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
					timeElapsed > 0LL ? tcpData.m_bytesSent.GetValue() * 1000LL / timeElapsed : 0LL);
				// calculating # of bytes that were received between the previous format() and current call to format()
				RightJustifyOutput(
					c_recvBytesPerSecondOffset,
					c_recvBytesPerSecondLength,
					timeElapsed > 0LL ? tcpData.m_bytesRecv.GetValue() * 1000LL / timeElapsed : 0LL);

				RightJustifyOutput(c_currentTransactionsOffset, c_currentTransactionsLength, connectionData.m_activeConnectionCount.GetValue());
				RightJustifyOutput(c_completedTransactionsOffset, c_completedTransactionsLength, connectionData.m_successfulCompletionCount.GetValue());
				RightJustifyOutput(c_connectionErrorsOffset, c_connectionErrorsLength, connectionData.m_connectionErrorCount.GetValue());
				RightJustifyOutput(c_protocolErrorsOffset, c_protocolErrorsLength, connectionData.m_protocolErrorCount.GetValue());
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
