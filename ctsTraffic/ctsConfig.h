/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <vector>
#include <functional>
// os headers
#include <windows.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctTimer.hpp>
#include <ctSockaddr.hpp>
//
// ** NOTE ** cannot include local project cts headers to avoid circular references
// - with the below exceptions : these do not include any cts* headers
//   -- ctsSafeInt.hpp
//   -- ctsStatistics.hpp
//
#include "ctsSafeInt.hpp"
#include "ctsStatistics.hpp"


namespace ctsTraffic {
    ///
    /// Forward declaring ctsSocket project headers cannot be included due to circular references
    ///
    /// In the ctsTraffic namespace, typedef for all function types
    /// - function pointers, functors, lambdas, etc.
    ///
    class ctsSocket;
    typedef std::function<void(std::weak_ptr<ctsSocket>)> ctsSocketFunction;

    namespace ctsConfig {

        ///
        /// Declaring enum types in the ctsConfig namespace
        /// - to be referenced by ctsConfig functions
        ///
        enum class ProtocolType {
            NoProtocolSet,
            TCP,
            UDP
        };

        enum class TcpShutdownType
        {
            NoShutdownOptionSet,
            ServerSideShutdown,
            GracefulShutdown,
            HardShutdown
        };

        enum class IoPatternType
        {
            NoIOSet,
            Push,
            Pull,
            PushPull,
            Duplex,
            MediaStream
        };

        enum class StatusFormatting
        {
            NoFormattingSet,
            WttLog,
            ClearText,
            Csv,
            ConsoleOutput
        };

        enum OptionType
        {
            NoOptionSet = 0x0000,
            LOOPBACK_FAST_PATH = 0x0001,
            KEEPALIVE = 0x0002,
            NON_BLOCKING_IO = 0x0004,
            HANDLE_INLINE_IOCP = 0x0008,
            REUSE_UNICAST_PORT = 0x0010,
            SET_RECV_BUF = 0x0020,
            SET_SEND_BUF = 0x0040,
            // next enum  = 0x0200
        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// custom operators for the OptionType enum (since it's an to be used as a bitmask)
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////

        /// OR
        inline
        OptionType operator| (OptionType& lhs, OptionType rhs)
        {
            return OptionType(static_cast<unsigned long>(lhs) | static_cast<unsigned long>(rhs));
        }
        inline
        OptionType& operator|= (OptionType& lhs, OptionType rhs)
        {
            lhs = lhs | rhs;
            return lhs;
        }

        /// AND
        inline
        OptionType operator& (OptionType lhs, OptionType rhs)
        {
            return OptionType(static_cast<unsigned long>(lhs) & static_cast<unsigned long>(rhs));
        }
        inline
        OptionType& operator&= (OptionType& lhs, OptionType rhs)
        {
            lhs = lhs & rhs;
            return lhs;
        }

        /// XOR
        inline
        OptionType operator^ (OptionType lhs, OptionType rhs)
        {
            return OptionType(static_cast<unsigned long>(lhs) ^ static_cast<unsigned long>(rhs));
        }
        inline
        OptionType& operator^= (OptionType& lhs, OptionType rhs)
        {
            lhs = lhs ^ rhs;
            return lhs;
        }

        /// NOT
        inline
        OptionType operator~ (OptionType lhs)
        {
            return OptionType(~static_cast<unsigned long>(lhs));
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Members within the ctsConfig namespace that can be accessed anywhere within ctsTraffic
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        bool Startup(_In_ int argc, _In_reads_(argc) const wchar_t** argv);
        void Shutdown() NOEXCEPT;

        enum class PrintUsageOption
        {
            Default,
            Tcp,
            Udp,
            Logging,
            Advanced
        };
        void PrintUsage(PrintUsageOption option = PrintUsageOption::Default);
        void PrintSettings();

        void PrintLegend() NOEXCEPT;

        struct JitterFrameEntry {
            long long sequence_number = 0LL;
            long long sender_qpc = 0LL;
            long long sender_qpf = 0LL;
            long long receiver_qpc = 0LL;
            long long receiver_qpf = 0LL;
            unsigned long received = 0UL;
        };
        void PrintJitterUpdate(const JitterFrameEntry& current_frame, const JitterFrameEntry& previous_frame, const JitterFrameEntry& first_frame) NOEXCEPT;

        void PrintStatusUpdate() NOEXCEPT;
        void __cdecl PrintSummary(_In_z_ _Printf_format_string_ LPCWSTR text, ...) NOEXCEPT;

        // Putting PrintDebugInfo as a macro to avoid running any code for debug printing if not necessary
#define PrintDebugInfo(fmt, ...)                                        \
        {                                                               \
            if (!::ctsTraffic::ctsConfig::ShutdownCalled()) {           \
                switch (::ctsTraffic::ctsConfig::ConsoleVerbosity()) {  \
                    case 6:                                             \
                        ::wprintf_s(fmt, ##__VA_ARGS__);                \
                        break;                                          \
                }                                                       \
            }                                                           \
        }

        void PrintErrorIfFailed(_In_ LPCWSTR what, unsigned long why) NOEXCEPT;
        void __cdecl PrintErrorInfo(_In_z_ _Printf_format_string_ LPCWSTR text, ...) NOEXCEPT;
        // Override will always print to console regardless of settings (important if can't even start)
        void __cdecl PrintErrorInfoOverride(_In_z_ _Printf_format_string_ LPCWSTR text, ...) NOEXCEPT;

        void PrintException(const std::exception& e) NOEXCEPT;
        // Override will always print to console regardless of settings (important if can't even start)
        void PrintExceptionOverride(const std::exception& e) NOEXCEPT;

        void PrintNewConnection(const ctl::ctSockaddr& local_addr, const ctl::ctSockaddr& remote_addr) NOEXCEPT;
        void PrintConnectionResults(const ctl::ctSockaddr& local_addr, const ctl::ctSockaddr& remote_addr, unsigned long error, const ctsTcpStatistics& stats) NOEXCEPT;
        void PrintConnectionResults(const ctl::ctSockaddr& local_addr, const ctl::ctSockaddr& remote_addr, unsigned long error, const ctsUdpStatistics& stats) NOEXCEPT;
        void PrintConnectionResults(const ctl::ctSockaddr& local_addr, const ctl::ctSockaddr& remote_addr, unsigned long error) NOEXCEPT;

        // Get* functions
        ctsSignedLongLong   GetTcpBytesPerSecond() NOEXCEPT;
        ctsUnsignedLong     GetMaxBufferSize() NOEXCEPT;
        ctsUnsignedLong     GetBufferSize() NOEXCEPT;
        ctsUnsignedLongLong GetTransferSize() NOEXCEPT;

        float GetStatusTimeStamp() NOEXCEPT;

        int  GetListenBacklog() NOEXCEPT;
        bool IsListening() NOEXCEPT;

        // Set* functions
        int SetPreBindOptions(SOCKET s, const ctl::ctSockaddr& local_address);
        int SetPreConnectOptions(SOCKET s);

        // for the MediaStream pattern
        struct MediaStreamSettings {

            MediaStreamSettings() NOEXCEPT
            : BitsPerSecond(0LL),
              FramesPerSecond(0UL),
              BufferDepthSeconds(0UL),
              StreamLengthSeconds(0UL),
              FrameSizeBytes(0UL),
              StreamLengthFrames(0UL),
              BufferedFrames(0UL)
            {
            }

            ctsUnsignedLongLong CalculateTransferSize()
            {
                ctl::ctFatalCondition(
                    0LL == BitsPerSecond,
                    L"BitsPerSecond cannot be set to zero");
                ctl::ctFatalCondition(
                    0 == FramesPerSecond,
                    L"FramesPerSecond cannot be set to zero");
                ctl::ctFatalCondition(
                    0 == StreamLengthSeconds,
                    L"StreamLengthSeconds cannot be set to zero");
                ctl::ctFatalCondition(
                    BitsPerSecond % 8LL != 0LL,
                    L"The BitsPerSecond value (%lld) must be evenly divisible by 8", static_cast<long long>(BitsPerSecond));

                // number of frames to keep buffered - only relevant on the client
                if (!IsListening()) {
                    ctl::ctFatalCondition(
                        0 == BufferDepthSeconds,
                        L"BufferDepthSeconds cannot be set to zero");
                    BufferedFrames = BufferDepthSeconds * FramesPerSecond;
                    if (BufferedFrames < BufferDepthSeconds || BufferedFrames < FramesPerSecond) {
                        throw std::invalid_argument("The total buffered frames exceed the maximum allowed : review -BufferDepth and -FrameRate");
                    }
                }

                ctsUnsignedLongLong total_stream_length_frames = StreamLengthSeconds * FramesPerSecond;
                if (total_stream_length_frames > MAXULONG32) {
                    throw std::invalid_argument("The total stream length in frame-count exceeds the maximum allowed to be streamed (2^32)");
                }

                // convert rate to bytes / second -> calculate the total # of bytes
                ctsUnsignedLongLong total_stream_length_bytes = static_cast<unsigned long long>((static_cast<long long>(BitsPerSecond) / 8ULL) * static_cast<unsigned long>(StreamLengthSeconds));

                // guarantee that the total stream length aligns evenly with total_frames
                if (total_stream_length_bytes % total_stream_length_frames != 0) {
                    total_stream_length_bytes -= total_stream_length_bytes % total_stream_length_frames;
                }

                ctsUnsignedLongLong total_frame_size_bytes = total_stream_length_bytes / total_stream_length_frames;
                if (total_frame_size_bytes > MAXULONG32) {
                    throw std::invalid_argument("The frame size in bytes exceeds the maximum allowed to be streamed (2^32)");
                }

                FrameSizeBytes = static_cast<unsigned long>(total_frame_size_bytes);
                if (FrameSizeBytes < 40) {
                    throw std::invalid_argument("The frame size is too small - it must be at least 40 bytes");
                }
                StreamLengthFrames = static_cast<unsigned long>(total_stream_length_frames);

                // guarantee frame alignment
                ctl::ctFatalCondition(
                    static_cast<unsigned long long>(FrameSizeBytes) * static_cast<unsigned long long>(StreamLengthFrames) != total_stream_length_bytes,
                    L"FrameSizeBytes (%u) * StreamLengthFrames (%u) != TotalStreamLength (%llx)",
                    static_cast<unsigned long>(FrameSizeBytes), static_cast<unsigned long>(StreamLengthFrames), static_cast<unsigned long long>(total_stream_length_bytes));

                return total_stream_length_bytes;
            }

            // set by ctsConfig from command-line arguments
            ctsSignedLongLong BitsPerSecond;
            ctsUnsignedLong FramesPerSecond;
            ctsUnsignedLong BufferDepthSeconds;
            ctsUnsignedLong StreamLengthSeconds;
            // internally calculated
            ctsUnsignedLong FrameSizeBytes;
            ctsUnsignedLong StreamLengthFrames;
            ctsUnsignedLong BufferedFrames;
        };
        const MediaStreamSettings& GetMediaStream() NOEXCEPT;

        struct ctsConfigSettings {
            ctsConfigSettings() NOEXCEPT :
                // dynamically initialize status details with current qpc
                ConnectionStatusDetails(ctl::ctTimer::snap_qpc_as_msec())
            {
            }

            // non-copyable
            ctsConfigSettings(const ctsConfigSettings&) = delete;
            ctsConfigSettings& operator=(const ctsConfigSettings&) = delete;

            HANDLE CtrlCHandle = NULL;
            PTP_CALLBACK_ENVIRON PTPEnvironment = nullptr;

            ctsSocketFunction CreateFunction = nullptr;
            ctsSocketFunction ConnectFunction = nullptr;
            ctsSocketFunction AcceptFunction = nullptr;
            ctsSocketFunction IoFunction = nullptr;
            ctsSocketFunction ClosingFunction = nullptr; // optional

            ProtocolType    Protocol = ProtocolType::NoProtocolSet;
            TcpShutdownType TcpShutdown = TcpShutdownType::NoShutdownOptionSet;
            IoPatternType   IoPattern = IoPatternType::NoIOSet;
            OptionType      Options = OptionType::NoOptionSet;

            DWORD SocketFlags = 0;
            WORD  Port = 0;

            ctsUnsignedLongLong Iterations = 0;
            ctsUnsignedLong AcceptLimit = 0;
            ctsUnsignedLong ConnectionLimit = 0;
            ctsUnsignedLong ConnectionThrottleLimit = 0;
            ctsUnsignedLongLong ServerExitLimit = 0;

            std::vector<ctl::ctSockaddr> ListenAddresses;
            std::vector<ctl::ctSockaddr> TargetAddresses;
            std::vector<ctl::ctSockaddr> BindAddresses;

            // stats for status updates and summaries
            ctsConnectionStatistics ConnectionStatusDetails;
            ctsTcpStatistics TcpStatusDetails;
            ctsUdpStatistics UdpStatusDetails;

            ctsUnsignedLong StatusUpdateFrequencyMilliseconds = 0;

            ctsSignedLongLong TcpBytesPerSecondPeriod = 100LL;
            ctsSignedLongLong StartTimeMilliseconds = 0;

            ctsUnsignedLong TimeLimit = 0;
            ctsUnsignedLong PrePostRecvs = 0;
            ctsUnsignedLong PrePostSends = 0;
            ctsUnsignedLong RecvBufValue = 0;
            ctsUnsignedLong SendBufValue = 0;

            bool UseSharedBuffer = false;
            bool ShouldVerifyBuffers = false;


            USHORT LocalPortLow = 0;
            USHORT LocalPortHigh = 0;

            ctsUnsignedLong PushBytes = 0;
            ctsUnsignedLong PullBytes = 0;
        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Settings is defined in ctsConfig.cpp
        /// - it's made available to all consumers of ctsConfig.h through extern
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        extern ctsConfigSettings* Settings;

        SOCKET CreateSocket(int af, int type, int protocol, DWORD dwFlags);
        bool ShutdownCalled() NOEXCEPT;
        unsigned long ConsoleVerbosity() NOEXCEPT;
    } // namespace ctsConfig
} // namespace ctsTraffic
