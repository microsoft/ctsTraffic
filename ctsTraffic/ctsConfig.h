/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/
#pragma once
// ReSharper disable CppInconsistentNaming
// ReSharper disable CppClangTidyCppcoreguidelinesMacroUsage
// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments

// cpp headers
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>
// os headers
#include <Windows.h>
// ctl headers
#include <ctTimer.hpp>
#include <ctSockaddr.hpp>
//
// ** NOTE ** cannot include local project cts headers to avoid circular references
// - with the exception of ctsStatistics.hpp
// - this header *can* be included here because it does not include any cts* headers
//
#include "ctsStatistics.hpp"

namespace ctsTraffic
{
    //
    // Forward declaring ctsSocket project headers cannot be included due to circular references
    //
    // In the ctsTraffic namespace, typedef for all function types
    // - function pointers, functors, lambdas, etc.
    //
    class ctsSocket;
    using ctsSocketFunction = std::function<void (std::weak_ptr<ctsSocket>)>;

    namespace ctsConfig
    {
        //
        // Declaring enum types in the ctsConfig namespace
        // - to be referenced by ctsConfig functions
        //
        enum class ExitProcessType : LONG
        {
            Running,
            Normal,
            Rude
        };

        enum class ProtocolType
        {
            NoProtocolSet,
            TCP,
            UDP
        };

        enum class TcpShutdownType
        {
            NoShutdownOptionSet,
            GracefulShutdown,
            HardShutdown,
            Random
        };

        enum class IoPatternType
        {
            NoIoSet,
            Push,
            Pull,
            PushPull,
            Duplex,
            MediaStream
        };

        enum class StatusFormatting
        {
            NoFormattingSet,
            ClearText,
            Csv,
            ConsoleOutput
        };

        // cannot be an enum class and have the below operator overloads work correctly
        enum OptionType
        {
            NoOptionSet = 0x0000,
            LoopbackFastPath = 0x0001,
            KeepAlive = 0x0002,
            NonBlockingIo = 0x0004,
            HandleInlineIocp = 0x0008,
            ReuseUnicastPort = 0x0010,
            SetRecvBuf = 0x0020,
            SetSendBuf = 0x0040,
            EnableCircularQueueing = 0x0080,
            MsgWaitAll = 0x0100,
            PortScalability = 0x0200
            // next enum  = 0x0400
        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        //
        // custom operators for the OptionType enum (since it's to be used as a bitmask)
        //
        ////////////////////////////////////////////////////////////////////////////////////////////////////

        // OR
        inline OptionType operator|(const OptionType& lhs, const OptionType& rhs) noexcept
        {
            return static_cast<OptionType>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
        }

        inline OptionType& operator|=(OptionType& lhs, const OptionType& rhs) noexcept
        {
            lhs = lhs | rhs;
            return lhs;
        }

        // AND
        inline OptionType operator&(const OptionType& lhs, const OptionType& rhs) noexcept
        {
            return static_cast<OptionType>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
        }

        inline OptionType& operator&=(OptionType& lhs, const OptionType& rhs) noexcept
        {
            lhs = lhs & rhs;
            return lhs;
        }

        // XOR
        inline OptionType operator^(const OptionType& lhs, const OptionType& rhs) noexcept
        {
            return static_cast<OptionType>(static_cast<uint32_t>(lhs) ^ static_cast<uint32_t>(rhs));
        }

        inline OptionType& operator^=(OptionType& lhs, const OptionType& rhs) noexcept
        {
            lhs = lhs ^ rhs;
            return lhs;
        }

        // NOT
        inline OptionType operator~(const OptionType& lhs) noexcept
        {
            return static_cast<OptionType>(~static_cast<uint32_t>(lhs));
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        //
        // Members within the ctsConfig namespace that can be accessed anywhere within ctsTraffic
        //
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        bool Startup(int argc, _In_reads_(argc) const wchar_t** argv);
        void Shutdown(ExitProcessType type) noexcept;

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

        void PrintLegend() noexcept;

        struct JitterFrameEntry
        {
            uint32_t m_bytesReceived = 0UL;
            int64_t m_sequenceNumber = 0LL;
            int64_t m_senderQpc = 0LL;
            int64_t m_senderQpf = 0LL;
            int64_t m_receiverQpc = 0LL;
            int64_t m_receiverQpf = 0LL;
            double m_estimatedTimeInFlightMs = 0;
        };

        void PrintJitterUpdate(const JitterFrameEntry& currentFrame, const JitterFrameEntry& previousFrame) noexcept;

        void __cdecl PrintSummary(_In_ _Printf_format_string_ PCWSTR text, ...) noexcept;
        void PrintStatusUpdate() noexcept;
        void PrintErrorInfo(_In_ _Printf_format_string_ PCWSTR text, ...) noexcept;
        void PrintErrorIfFailed(_In_ PCSTR what, uint32_t why) noexcept;
        // Override will always print to console regardless of settings (important if can't even start)
        void PrintErrorInfoOverride(_In_ PCWSTR text) noexcept;

        // Putting PrintDebugInfo as a macro to avoid running any code for debug printing if not necessary
#define PRINT_DEBUG_INFO(fmt, ...)                                            \
        do                                                                    \
        {                                                                     \
            if (!::ctsTraffic::ctsConfig::ShutdownCalled()) {                 \
                if (6 == ::ctsTraffic::ctsConfig::ConsoleVerbosity()) {       \
                    try { ::wprintf_s(fmt, ##__VA_ARGS__);  }  catch (...) {} \
                }                                                             \
            }                                                                 \
        }                                                                     \
        while ((void)0, 0)

        constexpr DWORD Win32FromHresult(HRESULT hr) noexcept
        {
            if (HRESULT_SEVERITY(hr) == SEVERITY_ERROR && HRESULT_FACILITY(hr) == FACILITY_WIN32)
            {
                return HRESULT_CODE(hr);
            }
            return hr;
        }

        DWORD PrintThrownException() noexcept;
        void PrintException(DWORD why, _In_ PCWSTR what, _In_ PCWSTR where) noexcept;
        // Override will always print to console regardless of settings (important if can't even start)
        void PrintExceptionOverride(_In_ PCSTR exceptionText) noexcept;

        void PrintNewConnection(const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr) noexcept;

        void PrintConnectionResults(uint32_t error) noexcept;
        void PrintConnectionResults(
            const ctl::ctSockaddr& localAddr,
            const ctl::ctSockaddr& remoteAddr,
            uint32_t error,
            const ctsTcpStatistics& stats) noexcept;
        void PrintConnectionResults(
            const ctl::ctSockaddr& localAddr,
            const ctl::ctSockaddr& remoteAddr,
            uint32_t error,
            const ctsUdpStatistics& stats) noexcept;

        void PrintTcpDetails(
            const ctl::ctSockaddr& localAddr,
            const ctl::ctSockaddr& remoteAddr,
            SOCKET socket,
            const ctsTcpStatistics& stats) noexcept;
        constexpr void PrintTcpDetails(
            const ctl::ctSockaddr&,
            const ctl::ctSockaddr&,
            SOCKET,
            const ctsUdpStatistics&) noexcept
        {
            // must implement ctsUdpStatistics as a no-op for the caller's template to compile
        }

        // Get* functions
        int64_t GetTcpBytesPerSecond() noexcept;
        uint32_t GetMaxBufferSize() noexcept;
        uint32_t GetMinBufferSize() noexcept;
        uint32_t GetBufferSize() noexcept;
        uint64_t GetTransferSize() noexcept;

        float GetStatusTimeStamp() noexcept;

        int32_t GetListenBacklog() noexcept;
        bool IsListening() noexcept;

        TcpShutdownType GetShutdownType() noexcept;

        // Set* functions
        int32_t SetPreBindOptions(SOCKET socket, const ctl::ctSockaddr& localAddress) noexcept;
        int32_t SetPreConnectOptions(SOCKET) noexcept;

        // for the MediaStream pattern
        struct MediaStreamSettings
        {
            // set by ctsConfig from command-line arguments
            int64_t BitsPerSecond = 0;
            uint32_t FramesPerSecond = 0;
            uint32_t BufferDepthSeconds = 0;
            uint32_t StreamLengthSeconds = 0;
            // internally calculated
            uint32_t FrameSizeBytes = 0;
            uint32_t StreamLengthFrames = 0;
            uint32_t BufferedFrames = 0;

            uint64_t CalculateTransferSize()
            {
                FAIL_FAST_IF_MSG(
                    0LL == BitsPerSecond,
                    "BitsPerSecond cannot be set to zero");
                FAIL_FAST_IF_MSG(
                    0 == FramesPerSecond,
                    "FramesPerSecond cannot be set to zero");
                FAIL_FAST_IF_MSG(
                    0 == StreamLengthSeconds,
                    "StreamLengthSeconds cannot be set to zero");
                FAIL_FAST_IF_MSG(
                    BitsPerSecond % 8LL != 0LL,
                    "The BitsPerSecond value (%lld) must be evenly divisible by 8", BitsPerSecond);

                // number of frames to keep buffered - only relevant on the client
                if (!IsListening())
                {
                    FAIL_FAST_IF_MSG(
                        0 == BufferDepthSeconds,
                        "BufferDepthSeconds cannot be set to zero");

                    BufferedFrames = BufferDepthSeconds * FramesPerSecond;
                    if (BufferedFrames < BufferDepthSeconds || BufferedFrames < FramesPerSecond)
                    {
                        throw std::invalid_argument(
                            "The total buffered frames exceed the maximum allowed : review -BufferDepth and -FrameRate");
                    }
                }

                const auto totalStreamLengthFrames = StreamLengthSeconds * FramesPerSecond;
                if (totalStreamLengthFrames > MAXULONG32)
                {
                    throw std::invalid_argument(
                        "The total stream length in frame-count exceeds the maximum allowed to be streamed (2^32)");
                }

                // convert rate to bytes / second -> calculate the total # of bytes
                auto totalStreamLengthBytes = BitsPerSecond / 8ULL * StreamLengthSeconds;

                // guarantee that the total stream length aligns evenly with total_frames
                if (totalStreamLengthBytes % totalStreamLengthFrames != 0)
                {
                    totalStreamLengthBytes -= totalStreamLengthBytes % totalStreamLengthFrames;
                }

                const auto totalFrameSizeBytes = totalStreamLengthBytes / totalStreamLengthFrames;
                if (totalFrameSizeBytes > MAXULONG32)
                {
                    throw std::invalid_argument(
                        "The frame size in bytes exceeds the maximum allowed to be streamed (2^32)");
                }

                FrameSizeBytes = static_cast<uint32_t>(totalFrameSizeBytes);
                if (FrameSizeBytes < 40)
                {
                    throw std::invalid_argument("The frame size is too small - it must be at least 40 bytes");
                }
                StreamLengthFrames = static_cast<uint32_t>(totalStreamLengthFrames);

                // guarantee frame alignment
                FAIL_FAST_IF_MSG(
                    static_cast<uint64_t>(FrameSizeBytes) * static_cast<uint64_t>(StreamLengthFrames) !=
                    totalStreamLengthBytes,
                    "FrameSizeBytes (%u) * StreamLengthFrames (%u) != TotalStreamLength (%llx)",
                    FrameSizeBytes, StreamLengthFrames, totalStreamLengthBytes);

                return totalStreamLengthBytes;
            }
        };

        const MediaStreamSettings& GetMediaStream() noexcept;

        struct ctsConfigSettings
        {
            // dynamically initialize status details with current qpc
            ctsConfigSettings() noexcept :
                ConnectionStatusDetails(ctl::ctTimer::snap_qpc_as_msec())
            {
            }

            ~ctsConfigSettings() noexcept = default;
            // non-copyable
            ctsConfigSettings(const ctsConfigSettings&) = delete;
            ctsConfigSettings& operator=(const ctsConfigSettings&) = delete;
            ctsConfigSettings(ctsConfigSettings&&) = delete;
            ctsConfigSettings& operator=(ctsConfigSettings&&) = delete;

            HANDLE CtrlCHandle = nullptr;
            PTP_CALLBACK_ENVIRON pTpEnvironment = nullptr;

            ctsSocketFunction CreateFunction;
            ctsSocketFunction ConnectFunction;
            ctsSocketFunction AcceptFunction;
            ctsSocketFunction IoFunction;
            ctsSocketFunction ClosingFunction; // optional

            ProtocolType Protocol = ProtocolType::NoProtocolSet;
            TcpShutdownType TcpShutdown = TcpShutdownType::NoShutdownOptionSet;
            IoPatternType IoPattern = IoPatternType::NoIoSet;
            OptionType Options = NoOptionSet;

            uint32_t SocketFlags = 0;

            uint64_t Iterations = 0;
            uint64_t ServerExitLimit = 0;
            uint32_t AcceptLimit = 0;
            uint32_t ConnectionLimit = 0;
            uint32_t ConnectionThrottleLimit = 0;

            std::vector<ctl::ctSockaddr> ListenAddresses{};
            std::vector<ctl::ctSockaddr> TargetAddresses{};
            std::vector<ctl::ctSockaddr> BindAddresses{};
            std::vector<std::wstring> TargetAddressStrings{};

            // stats for status updates and summaries
            ctsConnectionStatistics ConnectionStatusDetails;
            ctsTcpStatistics TcpStatusDetails;
            ctsUdpStatistics UdpStatusDetails;

            uint32_t StatusUpdateFrequencyMilliseconds = 0;

            int64_t TcpBytesPerSecondPeriod = 100LL;
            int64_t StartTimeMilliseconds = 0;

            uint32_t TimeLimit = 0;
            uint32_t PauseAtEnd = 0;
            uint32_t PrePostRecvs = 0;
            uint32_t PrePostSends = 0;
            uint32_t RecvBufValue = 0;
            uint32_t SendBufValue = 0;
            uint32_t KeepAliveValue = 0;

            uint32_t PushBytes = 0;
            uint32_t PullBytes = 0;

            std::optional<uint32_t> BurstCount;
            std::optional<uint32_t> BurstDelay;
            std::optional<uint32_t> CpuGroupId;

            uint32_t OutgoingIfIndex = 0;

            uint16_t LocalPortLow = 0;
            uint16_t LocalPortHigh = 0;
            uint16_t Port = 0;

            bool UseSharedBuffer = false;
            bool ShouldVerifyBuffers = false;

            static constexpr DWORD c_CriticalSectionSpinlock = 200ul;
        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Settings is defined in ctsConfig.cpp
        /// - it's made available to all consumers of ctsConfig.h through extern
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        extern ctsConfigSettings* g_configSettings;

        SOCKET CreateSocket(int af, int type, int protocol, DWORD dwFlags);
        bool ShutdownCalled() noexcept;
        uint32_t ConsoleVerbosity() noexcept;
    } // namespace ctsConfig
} // namespace ctsTraffic
