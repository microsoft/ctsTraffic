/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <stdio.h> 
#include <vector>
#include <memory>
#include <functional>
#include <exception>

// OS headers
#include <windows.h>

// ctl headers
#include <ctString.hpp>
#include <ctTimer.hpp>
#include <ctSockaddr.hpp>
#include <ctLocks.hpp>

//
// ** NOTE ** cannot include any local project cts headers - to avoid circular references
// - with the one exception of ctsSafeInt.hpp (it's careful not to include any cts* headers)
//
#include "ctsSafeInt.hpp"

namespace ctsTraffic {
    ///
    /// Forward declaring ctsSocket project headers cannot be included due to circular references
    ///
    /// In the ctsTraffic namespace, typedef for all function types
    /// - function pointers, functors, lambdas, etc.
    ///
    class ctsSocket;
    typedef std::function<void(std::weak_ptr<ctsSocket>)> ctsSocketFunction;

    template <typename T> struct ctsMemoryGuard {
    private:
        // not allowing assignment operator - must be explicit
        ctsMemoryGuard& operator=(const ctsMemoryGuard& _in) throw();
        T current_value;
        T previous_value;

    public:
        ctsMemoryGuard() throw() :
            current_value(0),
            previous_value(0)
        {
        }
        explicit ctsMemoryGuard(T _initial_value) throw() :
            current_value(_initial_value),
            previous_value(_initial_value)
        {
        }
        explicit ctsMemoryGuard(const ctsMemoryGuard& _in) throw() :
            current_value(ctl::ctMemoryGuardRead(&_in.current_value)),
            previous_value(ctl::ctMemoryGuardRead(&_in.previous_value))
        {
        }

        T get() const throw()
        {
            return ctl::ctMemoryGuardRead(&current_value);
        }
        //
        // Safely writes to the current value, returning the *prior* value
        //
        T set(T _new_value) throw()
        {
            return ctl::ctMemoryGuardWrite(&current_value, _new_value);
        }
        T set_conditionally(T _new_value, T _if_equals) throw()
        {
            return ctl::ctMemoryGuardWriteConditionally(&current_value, _new_value, _if_equals);
        }
        //
        // Adds 1 to the current value, returning the new value
        //
        T increment() throw()
        {
            return ctl::ctMemoryGuardIncrement(&current_value);
        }
        //
        // Subtracts 1 from the current value, returning the new value
        //
        T decrement() throw()
        {
            return ctl::ctMemoryGuardDecrement(&current_value);
        }
        //
        // Adds the [in] value to the current value, returning the original value
        //
        T add(T _value) throw()
        {
            return ctl::ctMemoryGuardAdd(&current_value, _value);
        }
        //
        // Subtracts the [in] value from the current value, returning the original value
        //
        T subtract(T _value) throw()
        {
            return ctl::ctMemoryGuardAdd(&current_value, _value);
        }
        //
        // Get / Sets a new value to the 'previous' value, returning the prior 'previous' value
        //
        T get_prior_value() throw()
        {
            return ctl::ctMemoryGuardRead(&previous_value);
        }
        T set_prior_value(T _new_value) throw()
        {
            return ctl::ctMemoryGuardWrite(&previous_value, _new_value);
        }
        //
        // Updates the previous value with the current value
        // - returning the difference (current_value - previous_value)
        //
        T snap_value_difference() throw()
        {
            T capture_current_value = ctl::ctMemoryGuardRead(&current_value);
            T capture_prior_value = ctl::ctMemoryGuardWrite(&previous_value, capture_current_value);
            return capture_current_value - capture_prior_value;
        }
        //
        // Returns the difference (current_value - previous_value)
        // - without modifying either value
        //
        T read_value_difference() const throw()
        {
            T capture_current_value = ctl::ctMemoryGuardRead(&current_value);
            T capture_prior_value = ctl::ctMemoryGuardRead(&previous_value);
            return capture_current_value - capture_prior_value;
        }
    };

    struct ctsConnectionHistoritcStatistics {
        ctsMemoryGuard<long long> total_time;
        ctsMemoryGuard<long long> active_connections;
        ctsMemoryGuard<long long> successful_connections;
        ctsMemoryGuard<long long> connection_errors;
        ctsMemoryGuard<long long> protocol_errors;
    };
    struct ctsConnectionStatistics {
    private:
        // not implementing the assignment operator
        // only implemeting the copy c'tor (due to maintaining memory barriers)
        ctsConnectionStatistics& operator=(const ctsConnectionStatistics& _in);

    public:
        ctsMemoryGuard<long long> start_time;
        ctsMemoryGuard<long long> end_time;
        ctsMemoryGuard<long long> active_connection_count;
        ctsMemoryGuard<long long> successful_completion_count;
        ctsMemoryGuard<long long> connection_error_count;
        ctsMemoryGuard<long long> protocol_error_count;

        ctsConnectionStatistics(long long _start_time = ctl::ctTimer::snap_qpc_msec()) throw() :
            start_time(_start_time),
            end_time(0LL),
            active_connection_count(0LL),
            successful_completion_count(0LL),
            connection_error_count(0LL),
            protocol_error_count(0LL)
        {
        }
        //
        // implementing the copy c'tor with memory barriers in place
        //
        ctsConnectionStatistics(const ctsConnectionStatistics& _in) throw() :
            start_time(_in.start_time),
            end_time(_in.end_time),
            active_connection_count(_in.active_connection_count),
            successful_completion_count(_in.successful_completion_count),
            connection_error_count(_in.connection_error_count),
            protocol_error_count(_in.protocol_error_count)
        {
        }
        //
        // snap_view() will return a statistics object capturing the current values
        // - resetting only the start_time value if the _In_ bool is true
        // - not resetting the other values even when _clear_settings == true since
        //   connection values in status messages always display the aggregate values
        //   (not displaying only changes in connection settings over each time slice)
        //
        ctsConnectionStatistics snap_view(bool _clear_settings) throw()
        {
            long long current_time = ctl::ctTimer::snap_qpc_msec();
            long long prior_time_read = (_clear_settings) ?
                this->start_time.set_prior_value(current_time) :
                this->start_time.get_prior_value();

            ctsConnectionStatistics return_stats(prior_time_read);
            return_stats.end_time.set(current_time);

            return_stats.active_connection_count.set(this->active_connection_count.get());
            return_stats.successful_completion_count.set(this->successful_completion_count.get());
            return_stats.connection_error_count.set(this->connection_error_count.get());
            return_stats.protocol_error_count.set(this->protocol_error_count.get());

            return return_stats;
        }
    };

    struct ctsUdpHistoricStatistics {
        ctsMemoryGuard<long long> total_time;
        ctsMemoryGuard<long long> bits_received;
        ctsMemoryGuard<long long> successful_frames;
        ctsMemoryGuard<long long> retry_attempts;
        ctsMemoryGuard<long long> dropped_frames;
        ctsMemoryGuard<long long> duplicate_frames;
        ctsMemoryGuard<long long> error_frames;
    };

    struct ctsUdpStatistics {
    private:
        ctsUdpStatistics& operator=(const ctsUdpStatistics& _in);

    public:
        ctsMemoryGuard<long long> start_time;
        ctsMemoryGuard<long long> end_time;
        ctsMemoryGuard<long long> bits_received;
        ctsMemoryGuard<long long> successful_frames;
        ctsMemoryGuard<long long> retry_attempts;
        ctsMemoryGuard<long long> dropped_frames;
        ctsMemoryGuard<long long> duplicate_frames;
        ctsMemoryGuard<long long> error_frames;

        ctsUdpStatistics(long long _start_time = ctl::ctTimer::snap_qpc_msec()) throw() :
            start_time(_start_time),
            end_time(0LL),
            bits_received(0LL),
            successful_frames(0LL),
            retry_attempts(0LL),
            dropped_frames(0LL),
            duplicate_frames(0LL),
            error_frames(0LL)
        {
        }
        //
        // implementing the copy c'tor with memory barriers in place
        //
        ctsUdpStatistics(const ctsUdpStatistics& _in) throw() :
            start_time(_in.start_time),
            end_time(_in.end_time),
            bits_received(_in.bits_received),
            successful_frames(_in.successful_frames),
            retry_attempts(_in.retry_attempts),
            dropped_frames(_in.dropped_frames),
            duplicate_frames(_in.duplicate_frames),
            error_frames(_in.error_frames)
        {
        }
        //
        // snap-view will set the returned start time == last read time to capture the delta
        //
        ctsUdpStatistics snap_view(bool _clear_settings) throw()
        {
            long long current_time = ctl::ctTimer::snap_qpc_msec();
            long long prior_time_read = (_clear_settings) ?
                this->start_time.set_prior_value(current_time) :
                this->start_time.get_prior_value();

            ctsUdpStatistics return_stats(prior_time_read);
            return_stats.end_time.set(current_time);

            if (_clear_settings) {
                return_stats.bits_received.set(this->bits_received.snap_value_difference());
                return_stats.successful_frames.set(this->successful_frames.snap_value_difference());
                return_stats.retry_attempts.set(this->retry_attempts.snap_value_difference());
                return_stats.dropped_frames.set(this->dropped_frames.snap_value_difference());
                return_stats.duplicate_frames.set(this->duplicate_frames.snap_value_difference());
                return_stats.error_frames.set(this->duplicate_frames.snap_value_difference());

            } else {
                return_stats.bits_received.set(this->bits_received.read_value_difference());
                return_stats.successful_frames.set(this->successful_frames.read_value_difference());
                return_stats.retry_attempts.set(this->retry_attempts.read_value_difference());
                return_stats.dropped_frames.set(this->dropped_frames.read_value_difference());
                return_stats.duplicate_frames.set(this->duplicate_frames.read_value_difference());
                return_stats.error_frames.set(this->duplicate_frames.read_value_difference());
            }

            return return_stats;
        }
    };

    struct ctsTcpHistoricStatistics {
        ctsMemoryGuard<long long> total_time;
        ctsMemoryGuard<long long> bytes_sent;
        ctsMemoryGuard<long long> bytes_recv;
    };

    struct ctsTcpStatistics {
    private:
        ctsTcpStatistics operator=(const ctsTcpStatistics& _in) throw();

    public:
        ctsMemoryGuard<long long> start_time;
        ctsMemoryGuard<long long> end_time;
        ctsMemoryGuard<long long> bytes_sent;
        ctsMemoryGuard<long long> bytes_recv;

        ctsTcpStatistics(long long _current_time = ctl::ctTimer::snap_qpc_msec()) throw() :
            start_time(_current_time),
            end_time(0LL),
            bytes_sent(0LL),
            bytes_recv(0LL)
        {
        }
        //
        // implementing the copy c'tor with memory barriers in place
        //
        ctsTcpStatistics(const ctsTcpStatistics& _in) throw() :
            start_time(_in.start_time),
            end_time(_in.end_time),
            bytes_sent(_in.bytes_sent),
            bytes_recv(_in.bytes_recv)
        {
        }
        //
        // snap-view will set the returned start time == last read time to capture the delta
        // - and end time == current time
        //
        ctsTcpStatistics snap_view(bool _clear_settings) throw()
        {
            long long current_time = ctl::ctTimer::snap_qpc_msec();
            long long prior_time_read = (_clear_settings) ?
                this->start_time.set_prior_value(current_time) :
                this->start_time.get_prior_value();

            ctsTcpStatistics return_stats(prior_time_read);
            return_stats.end_time.set(current_time);

            if (_clear_settings) {
                return_stats.bytes_sent.set(this->bytes_sent.snap_value_difference());
                return_stats.bytes_recv.set(this->bytes_recv.snap_value_difference());

            } else {
                return_stats.bytes_sent.set(this->bytes_sent.read_value_difference());
                return_stats.bytes_recv.set(this->bytes_recv.read_value_difference());
            }

            return return_stats;
        }
    };

    namespace ctsConfig {

        ///
        /// Declaring enum types in the ctsConfig namespace
        /// - to be referenced by ctsConfig functions
        ///
        enum ProtocolType {
            NoProtocolSet,
            TCP,
            UDP,
            RAW,
            Multicast
        };

        enum IoPatternType {
            NoIOSet,
            Push,
            Pull,
            PushPull,
            Duplex,
            MediaStream
        };

        enum OptionType {
            NoOptionSet = 0x0000,
            LOOPBACK_FAST_PATH = 0x0001,
            PHONE_SUBAPP_DATA = 0x0002,
            KEEPALIVE = 0x0004,
            NON_BLOCKING_IO = 0x0008,
            HANDLE_INLINE_IOCP = 0x0010,
            MAX_RECV_BUF = 0x0020
            // val8 = 0x0040
            // val9 = 0x0080
        };

        enum StatusFormatting {
            NoFormattingSet,
            WttLog,
            ClearText,
            Csv
        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// custom operators for the OptionType enum (since it's an to be used as a bitmask)
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////

        /// OR
        inline
        OptionType operator| (OptionType& _lhs, OptionType _rhs)
        {
            return OptionType(static_cast<unsigned long>(_lhs) | static_cast<unsigned long>(_rhs));
        }
        inline
        OptionType& operator|= (OptionType& _lhs, OptionType _rhs)
        {
            _lhs = _lhs | _rhs;
            return _lhs;
        }

        /// AND
        inline
        OptionType operator& (OptionType _lhs, OptionType _rhs)
        {
            return OptionType(static_cast<unsigned long>(_lhs) & static_cast<unsigned long>(_rhs));
        }
        inline
        OptionType& operator&= (OptionType& _lhs, OptionType _rhs)
        {
            _lhs = _lhs & _rhs;
            return _lhs;
        }

        /// XOR
        inline
        OptionType operator^ (OptionType _lhs, OptionType _rhs)
        {
            return OptionType(static_cast<unsigned long>(_lhs) ^ static_cast<unsigned long>(_rhs));
        }
        inline
        OptionType& operator^= (OptionType& _lhs, OptionType _rhs)
        {
            _lhs = _lhs ^ _rhs;
            return _lhs;
        }

        /// NOT
        inline
        OptionType operator~ (OptionType _lhs)
        {
            return OptionType(~static_cast<unsigned long>(_lhs));
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Members within the ctsConfig namespace that can be accessed anywhere within ctsTraffic
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        bool Startup(_In_ int argc, _In_reads_(argc) wchar_t** argv);
        void Shutdown();

        enum PrintUsageOption {
            Default,
            Tcp,
            Udp,
            Logging,
            Advanced
        };
        void PrintUsage(PrintUsageOption option = Default);

        void PrintLegend();
        void PrintSettings();
        void PrintDebugIfFailed(_In_ LPCWSTR _what, unsigned long _why, _In_ LPCWSTR _where) throw();
        void PrintErrorIfFailed(_In_ LPCWSTR _what, unsigned long _why) throw();
        /// *Override will always print to console regardless of settings (important if can't even start)
        void PrintExceptionOverride(const std::exception& e) throw();
        void PrintException(const std::exception& e) throw();
        void PrintStatusUpdate() throw();
        void PrintJitterUpdate(long long _sequence_number, long long _sender_qpc, long long _sender_qpf, long long _recevier_qpc, long long _receiver_qpf) throw();

        /// *Override will always print to console regardless of settings (important if can't even start)
        void PrintErrorInfoOverride(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) throw();
        void PrintErrorInfo(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) throw();
        void PrintNewConnection(const ctl::ctSockaddr& _remote_addr) throw();
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsTcpStatistics& _stats) throw();
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsUdpStatistics& _stats) throw();
        void PrintDebug(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) throw();
        void PrintSummary(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) throw();

        // Get* functions
        ctsSignedLongLong   GetTcpBytesPerSecond() throw();
        ctsUnsignedLong     GetMaxBufferSize() throw();
        ctsUnsignedLong     GetBufferSize() throw();
        ctsUnsignedLongLong GetTransferSize() throw();

        float GetStatusTimeStamp() throw();

        int  GetListenBacklog() throw();
        bool IsListening() throw();

        void UpdateGlobalStats(const ctsTcpStatistics&) throw();
        void UpdateGlobalStats(const ctsUdpStatistics&) throw();

        // Set* functions
        int SetPreBindOptions(SOCKET _s, const ctl::ctSockaddr& _local_address);
        int SetPreConnectOptions(SOCKET _s);

        // for the MediaStream pattern
        struct MediaStreamSettings {
            enum StreamCodecValues {
                NoResends,
                ResendOnce
            };

            MediaStreamSettings() throw()
            : BitsPerSecond(0LL),
              FramesPerSecond(0UL),
              BufferDepthSeconds(0UL),
              StreamLengthSeconds(0UL),
              StreamCodec(NoResends),
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
            StreamCodecValues StreamCodec;
            // internally calculated
            ctsUnsignedLong FrameSizeBytes;
            ctsUnsignedLong StreamLengthFrames;
            ctsUnsignedLong BufferedFrames;
        };
        const MediaStreamSettings& GetMediaStream();

        struct ctsConfigSettings {
            ctsConfigSettings()
            : CtrlCHandle(NULL),
              PTPEnvironment(nullptr),
              CreateFunction(nullptr),
              ConnectFunction(nullptr),
              AcceptFunction(nullptr),
              IoFunction(nullptr),
              Protocol(ProtocolType::NoProtocolSet),
              IoPattern(IoPatternType::NoIOSet),
              Options(OptionType::NoOptionSet),
              SocketFlags(0UL),
              Port(0),
              Iterations(0ULL),
              AcceptLimit(0UL),
              ConnectionLimit(0UL),
              ConnectionThrottleLimit(0UL),
              ServerExitLimit(0ULL),
              ListenAddresses(),
              TargetAddresses(),
              BindAddresses(),
              ConnectionStatusDetails(),
              TcpStatusDetails(),
              UdpStatusDetails(),
              HistoricConnectionDetails(),
              HistoricTcpDetails(),
              HistoricUdpDetails(),
              StatusUpdateFrequencyMilliseconds(0UL),
              TcpBytesPerSecondPeriod(100LL),
              StartTimeMilliseconds(0LL),
              TimeLimit(0UL),
              PrePostRecvs(0UL),
              UseSharedBuffer(false),
              ShouldVerifyBuffers(false),
              LocalPortLow(0),
              LocalPortHigh(0),
              PushBytes(0UL),
              PullBytes(0UL)
            {
            }

            HANDLE CtrlCHandle;
            PTP_CALLBACK_ENVIRON PTPEnvironment;

            ctsSocketFunction CreateFunction;
            ctsSocketFunction ConnectFunction;
            ctsSocketFunction AcceptFunction;
            ctsSocketFunction IoFunction;

            ProtocolType  Protocol;
            IoPatternType IoPattern;
            OptionType    Options;

            DWORD SocketFlags;
            unsigned short Port;

            ctsUnsignedLongLong Iterations;
            ctsUnsignedLong AcceptLimit;
            ctsUnsignedLong ConnectionLimit;
            ctsUnsignedLong ConnectionThrottleLimit;
            ctsUnsignedLongLong ServerExitLimit;

            std::vector<ctl::ctSockaddr> ListenAddresses;
            std::vector<ctl::ctSockaddr> TargetAddresses;
            std::vector<ctl::ctSockaddr> BindAddresses;

            // stats used only for status updates
            ctsConnectionStatistics ConnectionStatusDetails;
            ctsTcpStatistics TcpStatusDetails;
            ctsUdpStatistics UdpStatusDetails;
            // stats for global tracking
            ctsConnectionHistoritcStatistics HistoricConnectionDetails;
            ctsTcpHistoricStatistics HistoricTcpDetails;
            ctsUdpHistoricStatistics HistoricUdpDetails;

            ctsUnsignedLong StatusUpdateFrequencyMilliseconds;

            ctsUnsignedLongLong TcpBytesPerSecondPeriod;

            ctsSignedLongLong StartTimeMilliseconds;

            ctsUnsignedLong TimeLimit;
            ctsUnsignedLong PrePostRecvs;

            bool UseSharedBuffer;
            bool ShouldVerifyBuffers;

            USHORT LocalPortLow;
            USHORT LocalPortHigh;

            ctsUnsignedLong PushBytes;
            ctsUnsignedLong PullBytes;

            // non-copyable
            ctsConfigSettings(const ctsConfigSettings&) = delete;
            ctsConfigSettings& operator=(const ctsConfigSettings&) = delete;
        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Settings is defined in ctsConfig.cpp
        /// - it's made available to all consumers of ctsConfig.h through extern
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        extern ctsConfigSettings* Settings;

    } // namespace ctsConfig
} // namespace ctsTraffic
