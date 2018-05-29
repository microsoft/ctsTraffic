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
#include <set>
// os headers
#include <Windows.h>
// Winsock2 is needed for IPHelper headers
// ReSharper disable once CppUnusedIncludeDirective
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <Iphlpapi.h>
#include <Tcpestats.h>

// ctl headers
#include <ctString.hpp>
#include <ctSockaddr.hpp>
#include <ctThreadPoolTimer.hpp>

namespace ctsPerf {

namespace details {

    static const unsigned long InvalidLongEstatsValue = 0xffffffff;
    static const unsigned long long InvalidLongLongEstatsValue = 0xffffffffffffffff;

    inline
    bool IsRodValueValid(_In_ LPCWSTR name, ULONG t) noexcept
    {
        if (t == InvalidLongEstatsValue)
        {
            return false;
        }
        if (t > 0x10000000)
        {
#ifdef _TESTING_ESTATS_VALUES
            wprintf(L"\t** %ws : %lu\n", name, t);
#else
            UNREFERENCED_PARAMETER(name);
#endif
            return false;
        }
        return true;
    }
    inline
    bool IsRodValueValid(_In_ LPCWSTR name, ULONG64 t) noexcept
    {
        if (t == InvalidLongLongEstatsValue)
        {
            return false;
        }
        if (t > 0x1000000000000000)
        {
#ifdef _TESTING_ESTATS_VALUES
            wprintf(L"\t** %ws : %llu\n", name, t);
#else
            UNREFERENCED_PARAMETER(name);
#endif
            return false;
        }
        return true;
    }

    template <TCP_ESTATS_TYPE TcpType>
    struct EstatsTypeConverter {};

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsSynOpts> {
        typedef void* read_write_type;
        typedef PTCP_ESTATS_SYN_OPTS_ROS_v0 read_only_static_type;
        typedef void* read_only_dynamic_type;
    };

    template<>
    struct EstatsTypeConverter<TcpConnectionEstatsData> {
        typedef PTCP_ESTATS_DATA_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef PTCP_ESTATS_DATA_ROD_v0 read_only_dynamic_type;
    };

    template<>
    struct EstatsTypeConverter<TcpConnectionEstatsSndCong> {
        typedef PTCP_ESTATS_SND_CONG_RW_v0 read_write_type;
        typedef PTCP_ESTATS_SND_CONG_ROS_v0 read_only_static_type;
        typedef PTCP_ESTATS_SND_CONG_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsPath> {
        typedef PTCP_ESTATS_PATH_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef PTCP_ESTATS_PATH_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsSendBuff> {
        typedef PTCP_ESTATS_SEND_BUFF_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef PTCP_ESTATS_SEND_BUFF_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsRec> {
        typedef PTCP_ESTATS_REC_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef PTCP_ESTATS_REC_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsObsRec> {
        typedef PTCP_ESTATS_OBS_REC_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef PTCP_ESTATS_OBS_REC_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsBandwidth> {
        typedef PTCP_ESTATS_BANDWIDTH_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef PTCP_ESTATS_BANDWIDTH_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsFineRtt> {
        typedef PTCP_ESTATS_FINE_RTT_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef PTCP_ESTATS_FINE_RTT_ROD_v0 read_only_dynamic_type;
    };

    template <TCP_ESTATS_TYPE TcpType>
    void SetEstats(const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_write_type pRw)  // NOLINT
    {
        const auto err = ::SetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            reinterpret_cast<PUCHAR>(pRw), 0, static_cast<ULONG>(sizeof(*pRw)),
            0);
        if (err != 0) {
            throw ctl::ctException(err, L"SetPerTcpConnectionEStats", false);
        }
    }

    template <TCP_ESTATS_TYPE TcpType>
    ULONG GetReadOnlyStaticEstats(const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_static_type pRos)  // NOLINT
    {
        return ::GetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            nullptr, 0, 0, // read-write information
            reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
            nullptr, 0, 0); // read-only dynamic information
    }

    template <TCP_ESTATS_TYPE TcpType>
    ULONG GetReadOnlyDynamicEstats(const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_dynamic_type pRod)  // NOLINT
    {
        return ::GetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            nullptr, 0, 0, // read-write information
            nullptr, 0, 0, // read-only static information
            reinterpret_cast<PUCHAR>(pRod), 0, static_cast<ULONG>(sizeof(*pRod))); // read-only dynamic information
    }

    template <TCP_ESTATS_TYPE TcpType>
    void SetEstats(const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_write_type pRw)  // NOLINT
    {
        const auto err = ::SetPerTcp6ConnectionEStats(
            tcpRow,
            TcpType,
            reinterpret_cast<PUCHAR>(pRw), 0, static_cast<ULONG>(sizeof(*pRw)),
            0);
        if (err != 0) {
            throw ctl::ctException(err, L"SetPerTcp6ConnectionEStats", false);
        }
    }

    template <TCP_ESTATS_TYPE TcpType>
    ULONG GetReadOnlyStaticEstats(const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_static_type pRos)  // NOLINT
    {
        return ::GetPerTcp6ConnectionEStats(
            tcpRow,
            TcpType,
            nullptr, 0, 0, // read-write information
            reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
            nullptr, 0, 0); // read-only dynamic information
    }

    template <TCP_ESTATS_TYPE TcpType>
    ULONG GetReadOnlyDynamicEstats(const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_dynamic_type pRod)  // NOLINT
    {
        return ::GetPerTcp6ConnectionEStats(
            tcpRow,
            TcpType,
            nullptr, 0, 0, // read-write information
            nullptr, 0, 0, // read-only static information
            reinterpret_cast<PUCHAR>(pRod), 0, static_cast<ULONG>(sizeof(*pRod))); // read-only dynamic information
    }

    // the root template type that each ESTATS_TYPE will specialize for
    template <TCP_ESTATS_TYPE TcpType>
    class EstatsDataTracking {
        EstatsDataTracking() = default;
        ~EstatsDataTracking() = default;
        EstatsDataTracking(const EstatsDataTracking&) = delete;
        EstatsDataTracking& operator=(const EstatsDataTracking&) = delete;
    public:

        static LPCWSTR PrintHeader() = delete;

        void PrintData() const = delete;

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const = delete;

        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr) = delete;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsSynOpts> {
    public:
        static LPCWSTR PrintHeader()
        {
            return L"Mss-Received,Mss-Sent";
        }
        std::wstring PrintData() const
        {
            return L"," + std::to_wstring(MssRcvd) + L"," + std::to_wstring(MssSent);
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW) const
        {
            // always on
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&)
        {
            if (MssRcvd == 0) {
                TCP_ESTATS_SYN_OPTS_ROS_v0 Ros;
                FillMemory(&Ros, sizeof Ros, -1);
                if (0 == GetReadOnlyStaticEstats<TcpConnectionEstatsSynOpts>(tcpRow, &Ros)) {

                    if (IsRodValueValid(L"TcpConnectionEstatsSynOpts - MssRcvd", Ros.MssRcvd)) {
                        MssRcvd = Ros.MssRcvd;
                    }
                    if (IsRodValueValid(L"TcpConnectionEstatsSynOpts - MssSent", Ros.MssSent)) {
                        MssSent = Ros.MssSent;
                    }
                }
            }
        }

    private:
        ULONG MssRcvd = 0;
        ULONG MssSent = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsData> {
    public:
        static LPCWSTR PrintHeader()
        {
            return L"Bytes-In,Bytes-Out";
        }
        std::wstring PrintData() const
        {
            return L"," + std::to_wstring(DataBytesIn) + L"," + std::to_wstring(DataBytesOut);
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_DATA_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsData>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&)
        {
            TCP_ESTATS_DATA_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsData>(tcpRow, &Rod)) {

                if (IsRodValueValid(L"TcpConnectionEstatsData - DataBytesIn", Rod.DataBytesIn)) {
                    DataBytesIn = Rod.DataBytesIn;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsData - DataBytesOut", Rod.DataBytesOut)) {
                    DataBytesOut = Rod.DataBytesOut;
                }
            }
        }

    private:
        ULONG64 DataBytesIn = 0;
        ULONG64 DataBytesOut = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsSndCong> {
    public:
        static LPCWSTR PrintHeader()
        {
#ifdef _TESTING_ESTATS_VALUES
            return L"CongWin(mean),CongWin(stddev),"
                L"XIntoReceiverLimited,XIntoSenderLimited,XIntoCongestionLimited,"
                L"BytesSentRecvLimited,BytesSentSenderLimited,BytesSentCongLimited, [xValidValues,xInvalidValues] ";
#else
            return L"CongWin(mean),CongWin(stddev),"
                L"XIntoReceiverLimited,XIntoSenderLimited,XIntoCongestionLimited,"
                L"BytesSentRecvLimited,BytesSentSenderLimited,BytesSentCongLimited";
#endif
        }
        std::wstring PrintData() const
        {
#ifdef _TESTING_ESTATS_VALUES
            return
                ctsPerf::ctsWriteDetails::PrintMeanStdDev(conjestionWindows) +
                ctl::ctString::format_string(
                    L",%lu,%lu,%lu,%Iu,%Iu,%Iu, [%lu,%lu] ",
                    transitionsIntoReceiverLimited,
                    transitionsIntoSenderLimited,
                    transitionsIntoCongestionLimited,
                    bytesSentInReceiverLimited,
                    bytesSentInSenderLimited,
                    bytesSentInCongestionLimited,
                    validValues,
                    invalidValues);
#else
            return
                ctsPerf::ctsWriteDetails::PrintMeanStdDev(conjestionWindows) +
                ctl::ctString::format_string(
                    L",%lu,%lu,%lu,%Iu,%Iu,%Iu",
                    transitionsIntoReceiverLimited,
                    transitionsIntoSenderLimited,
                    transitionsIntoCongestionLimited,
                    bytesSentInReceiverLimited,
                    bytesSentInSenderLimited,
                    bytesSentInCongestionLimited);
#endif
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_SND_CONG_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsSndCong>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr)
        {
            TCP_ESTATS_SND_CONG_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsSndCong>(tcpRow, &Rod)) {

#ifdef _TESTING_ESTATS_VALUES
                if ((Rod.CurCwnd > 0x10000000 && Rod.CurCwnd != UninitializedUlong) ||
                    Rod.SndLimBytesRwin > 0x10000000 ||
                    Rod.SndLimBytesSnd > 0x10000000 ||
                    Rod.SndLimBytesCwnd > 0x10000000 ||
                    Rod.SndLimTransRwin > 0x10000000 ||
                    Rod.SndLimTransSnd > 0x10000000 ||
                    Rod.SndLimTransCwnd > 0x10000000)
                {
                    WCHAR local_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)localAddr.writeCompleteAddress(local_address);
                    WCHAR remote_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)remoteAddr.writeCompleteAddress(remote_address);

                    ++invalidValues;
                    printf(
                        "[%ws : %ws] Bad TcpConnectionEstatsSndCong (TCP_ESTATS_SND_CONG_ROD_v0): "
                        "CurCwnd (%lX) "
                        "SndLimBytesRwin (%IX) "
                        "SndLimBytesSnd (%IX) "
                        "SndLimBytesCwnd (%IX) "
                        "SndLimTransRwin (%lX) "
                        "SndLimTransSnd (%lX) "
                        "SndLimTransCwnd (%lX)\n",
                        local_address,
                        remote_address,
                        Rod.CurCwnd,
                        Rod.SndLimBytesRwin,
                        Rod.SndLimBytesSnd,
                        Rod.SndLimBytesCwnd,
                        Rod.SndLimTransRwin,
                        Rod.SndLimTransSnd,
                        Rod.SndLimTransCwnd);
                } else {
                    ++validValues;
                }
#else
                UNREFERENCED_PARAMETER(localAddr);
                UNREFERENCED_PARAMETER(remoteAddr);
#endif

                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - CurCwnd", Rod.CurCwnd)) {
                    conjestionWindows.push_back(Rod.CurCwnd);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimBytesRwin", Rod.SndLimBytesRwin)) {
                    bytesSentInReceiverLimited = Rod.SndLimBytesRwin;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimBytesSnd", Rod.SndLimBytesSnd)) {
                    bytesSentInSenderLimited = Rod.SndLimBytesSnd;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimBytesCwnd", Rod.SndLimBytesCwnd)) {
                    bytesSentInCongestionLimited = Rod.SndLimBytesCwnd;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimTransRwin", Rod.SndLimTransRwin)) {
                    transitionsIntoReceiverLimited = Rod.SndLimTransRwin;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimTransSnd", Rod.SndLimTransSnd)) {
                    transitionsIntoSenderLimited = Rod.SndLimTransSnd;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimTransCwnd", Rod.SndLimTransCwnd)) {
                    transitionsIntoCongestionLimited = Rod.SndLimTransCwnd;
                }
            }
        }

    private:
        std::vector<ULONG> conjestionWindows;

        SIZE_T bytesSentInReceiverLimited = 0;
        SIZE_T bytesSentInSenderLimited = 0;
        SIZE_T bytesSentInCongestionLimited = 0;

        ULONG transitionsIntoReceiverLimited = 0;
        ULONG transitionsIntoSenderLimited = 0;
        ULONG transitionsIntoCongestionLimited = 0;

#ifdef _TESTING_ESTATS_VALUES
        ULONG validValues = 0;
        ULONG invalidValues = 0;
#endif
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsPath> {
    public:
        static LPCWSTR PrintHeader()
        {
#ifdef _TESTING_ESTATS_VALUES
            return L"BytesRetrans,DupeAcks,SelectiveAcks,CongSignals,MaxSegSize,"
                L"RetransTimer(mean),RetransTimer(stddev),"
                L"RTT(mean),Rtt(stddev), [xValidValues,xInvalidValues] ";
#else
            return L"BytesRetrans,DupeAcks,SelectiveAcks,CongSignals,MaxSegSize,"
                L"RetransTimer(mean),RetransTimer(stddev),"
                L"RTT(mean),Rtt(stddev)";
#endif
        }
        std::wstring PrintData() const
        {
#ifdef _TESTING_ESTATS_VALUES
            return ctl::ctString::format_string(
                L",%lu,%lu,%lu,%lu,%lu",
                bytesRetrans,
                dupAcksRcvd,
                sacksRcvd,
                congestionSignals,
                maxSegmentSize) +
                ctsWriteDetails::PrintMeanStdDev(retransmitTimer) +
                ctsWriteDetails::PrintMeanStdDev(roundTripTime) +
                ctl::ctString::format_string(
                    L" [%lu,%lu] ",
                    validValues,
                    invalidValues);
#else
            return ctl::ctString::format_string(
                L",%lu,%lu,%lu,%lu,%lu",
                bytesRetrans,
                dupAcksRcvd,
                sacksRcvd,
                congestionSignals,
                maxSegmentSize) +
                ctsWriteDetails::PrintMeanStdDev(retransmitTimer) +
                ctsWriteDetails::PrintMeanStdDev(roundTripTime);
#endif
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_PATH_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsPath>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr)
        {
            TCP_ESTATS_PATH_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsPath>(tcpRow, &Rod)) {

#ifdef _TESTING_ESTATS_VALUES
                if ((Rod.CurRto > 0x10000000 && Rod.CurRto != UninitializedUlong) ||
                    (Rod.SmoothedRtt > 0x10000000 && Rod.SmoothedRtt != UninitializedUlong) ||
                    (Rod.BytesRetrans > 0x10000000 && Rod.BytesRetrans != UninitializedUlong) ||
                    (Rod.DupAcksIn > 0x10000000 && Rod.DupAcksIn != UninitializedUlong) ||
                    (Rod.SacksRcvd > 0x10000000 && Rod.SacksRcvd != UninitializedUlong) ||
                    (Rod.CongSignals > 0x10000000 && Rod.CongSignals != UninitializedUlong) ||
                    (Rod.CurMss > 0x10000000 && Rod.CurMss != UninitializedUlong))
                {
                    WCHAR local_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)localAddr.writeCompleteAddress(local_address);
                    WCHAR remote_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)remoteAddr.writeCompleteAddress(remote_address);

                    ++invalidValues;
                    printf(
                        "[%ws : %ws] Bad TcpConnectionEstatsPath (TCP_ESTATS_PATH_ROD_v0): "
                        "CurRto (%lX) "
                        "SmoothedRtt (%lX) "
                        "BytesRetrans (%lX) "
                        "DupAcksIn (%lX) "
                        "SacksRcvd (%lX) "
                        "CongSignals (%lX) "
                        "CurMss (%lX)\n",
                        local_address,
                        remote_address,
                        Rod.CurRto,
                        Rod.SmoothedRtt,
                        Rod.BytesRetrans,
                        Rod.DupAcksIn,
                        Rod.SacksRcvd,
                        Rod.CongSignals,
                        Rod.CurMss);
                } else {
                    ++validValues;
                }
#else
                UNREFERENCED_PARAMETER(localAddr);
                UNREFERENCED_PARAMETER(remoteAddr);
#endif

                if (IsRodValueValid(L"TcpConnectionEstatsPath - CurRto", Rod.CurRto)) {
                    retransmitTimer.push_back(Rod.CurRto);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - SmoothedRtt", Rod.SmoothedRtt)) {
                    roundTripTime.push_back(Rod.SmoothedRtt);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - BytesRetrans", Rod.BytesRetrans)) {
                    bytesRetrans = Rod.BytesRetrans;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - DupAcksIn", Rod.DupAcksIn)) {
                    dupAcksRcvd = Rod.DupAcksIn;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - SacksRcvd", Rod.SacksRcvd)) {
                    sacksRcvd = Rod.SacksRcvd;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - CongSignals", Rod.CongSignals)) {
                    congestionSignals = Rod.CongSignals;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - CurMss", Rod.CurMss)) {
                    maxSegmentSize = Rod.CurMss;
                }
            }
        }

    private:
        std::vector<ULONG> retransmitTimer;
        std::vector<ULONG> roundTripTime;
        ULONG bytesRetrans = 0;
        ULONG dupAcksRcvd = 0;
        ULONG sacksRcvd = 0;
        ULONG congestionSignals = 0;
        ULONG maxSegmentSize = 0;

#ifdef _TESTING_ESTATS_VALUES
        ULONG validValues = 0;
        ULONG invalidValues = 0;
#endif
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsRec> {
    public:
        static LPCWSTR PrintHeader()
        {
#ifdef _TESTING_ESTATS_VALUES
            return L"LocalRecvWin(min),LocalRecvWin(max),LocalRecvWin(calculated-min),LocalRecvWin(calculated-max),LocalRecvWin(calculated-mean),LocalRecvWin(calculated-stddev), [xValidValues,xInvalidValues] ";
#else
            return L"LocalRecvWin(min),LocalRecvWin(max),LocalRecvWin(calculated-min),LocalRecvWin(calculated-max),LocalRecvWin(calculated-mean),LocalRecvWin(calculated-stddev)";
#endif
        }
        std::wstring PrintData() const
        {
            std::wstring formattedString(L",");
            formattedString += (minReceiveWindow == InvalidLongEstatsValue) ?
                L"-1," :
                ctl::ctString::format_string(L"%lu,", minReceiveWindow);

            formattedString += (maxReceiveWindow == InvalidLongEstatsValue) ?
                L"-1" :
                ctl::ctString::format_string(L"%lu,", maxReceiveWindow);

            ULONG calculatedMin = InvalidLongEstatsValue;
            ULONG calculatedMax = InvalidLongEstatsValue;
            for (const auto& value : receiveWindow)
            {
                if (calculatedMin == InvalidLongEstatsValue) {
                    calculatedMin = value;
                } else if (value < calculatedMin) {
                    calculatedMin = value;

                } if (calculatedMax == InvalidLongEstatsValue) {
                    calculatedMax = value;
                } else if (value > calculatedMax) {
                    calculatedMax = value;
                }
            }

            formattedString += (calculatedMin == InvalidLongEstatsValue) ?
                L"-1," :
                ctl::ctString::format_string(L"%lu,", calculatedMin);

            formattedString += (calculatedMax == InvalidLongEstatsValue) ?
                L"-1," :
                ctl::ctString::format_string(L"%lu", calculatedMax);

#ifdef _TESTING_ESTATS_VALUES
            return
                formattedString +
                ctsWriteDetails::PrintMeanStdDev(receiveWindow) +
                ctl::ctString::format_string(
                    L" [%lu,%lu] ",
                    validValues,
                    invalidValues);
#else
            return
                formattedString +
                ctsWriteDetails::PrintMeanStdDev(receiveWindow);
#endif
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_REC_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsRec>(tcpRow, &Rw);
        }

        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr)
        {
            TCP_ESTATS_REC_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsRec>(tcpRow, &Rod)) {

#ifdef _TESTING_ESTATS_VALUES
                if ((Rod.CurRwinSent > 0x10000000 && Rod.CurRwinSent != UninitializedUlong) ||
                    (Rod.MinRwinSent > 0x10000000 && Rod.MinRwinSent != UninitializedUlong) ||
                    (Rod.MaxRwinSent > 0x10000000 && Rod.MaxRwinSent != UninitializedUlong) ||
                    (Rod.MinRwinSent != UninitializedUlong && Rod.MinRwinSent > Rod.MaxRwinSent && Rod.MaxRwinSent > 0))
                {
                    WCHAR local_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)localAddr.writeCompleteAddress(local_address);
                    WCHAR remote_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)remoteAddr.writeCompleteAddress(remote_address);

                    ++invalidValues;
                    printf(
                        "[%ws : %ws] Bad TcpConnectionEstatsRec (TCP_ESTATS_REC_ROD_v0): "
                        "CurRwinSent (%lX) "
                        "MinRwinSent (%lX) "
                        "MaxRwinSent (%lX)\n",
                        local_address,
                        remote_address,
                        Rod.CurRwinSent,
                        Rod.MinRwinSent,
                        Rod.MaxRwinSent);
                } else {
                    ++validValues;
                }
#else
                UNREFERENCED_PARAMETER(localAddr);
                UNREFERENCED_PARAMETER(remoteAddr);
#endif

                if (IsRodValueValid(L"TcpConnectionEstatsRec - CurRwinSent", Rod.CurRwinSent)) {
                    receiveWindow.push_back(Rod.CurRwinSent);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsRec - MinRwinSent", Rod.MinRwinSent)) {
                    minReceiveWindow = Rod.MinRwinSent;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsRec - MaxRwinSent", Rod.MaxRwinSent)) {
                    maxReceiveWindow = Rod.MaxRwinSent;
                }
            }
        }

    private:
        std::vector<ULONG> receiveWindow;
        ULONG minReceiveWindow = 0;
        ULONG maxReceiveWindow = 0;

#ifdef _TESTING_ESTATS_VALUES
        ULONG validValues = 0;
        ULONG invalidValues = 0;
#endif
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsObsRec> {
    public:
        static LPCWSTR PrintHeader()
        {
#ifdef _TESTING_ESTATS_VALUES
            return L"RemoteRecvWin(min),RemoteRecvWin(max),RemoteRecvWin(calculated-min),RemoteRecvWin(calculated-max),RemoteRecvWin(calculated-mean),RemoteRecvWin(calculated-stddev), [xValidValues,xInvalidValues] ";
#else
            return L"RemoteRecvWin(min),RemoteRecvWin(max),RemoteRecvWin(calculated-min),RemoteRecvWin(calculated-max),RemoteRecvWin(calculated-mean),RemoteRecvWin(calculated-stddev)";
#endif
        }
        std::wstring PrintData() const
        {
            std::wstring formattedString(L",");
            formattedString += (minReceiveWindow == InvalidLongEstatsValue) ?
                L"-1," :
                ctl::ctString::format_string(L"%lu,", minReceiveWindow);

            formattedString += (maxReceiveWindow == InvalidLongEstatsValue) ?
                L"-1," :
                ctl::ctString::format_string(L"%lu,", maxReceiveWindow);

            ULONG calculatedMin = InvalidLongEstatsValue;
            ULONG calculatedMax = InvalidLongEstatsValue;
            for (const auto& value : receiveWindow)
            {
                if (calculatedMin == InvalidLongEstatsValue) {
                    calculatedMin = value;
                } else if (value < calculatedMin) {
                    calculatedMin = value;
                }

                if (calculatedMax == InvalidLongEstatsValue) {
                    calculatedMax = value;
                } else if (value > calculatedMax) {
                    calculatedMax = value;
                }
            }

            formattedString += (calculatedMin == InvalidLongEstatsValue) ?
                L"-1," :
                ctl::ctString::format_string(L"%lu,", calculatedMin);

            formattedString += (calculatedMax == InvalidLongEstatsValue) ?
                L"-1," :
                ctl::ctString::format_string(L"%lu", calculatedMax);

#ifdef _TESTING_ESTATS_VALUES
            return
                formattedString +
                ctsWriteDetails::PrintMeanStdDev(receiveWindow) +
                ctl::ctString::format_string(
                    L" [%lu,%lu] ",
                    validValues,
                    invalidValues);
#else

            return
                formattedString + 
                ctsWriteDetails::PrintMeanStdDev(receiveWindow);
#endif
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_OBS_REC_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsObsRec>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr)
        {
            TCP_ESTATS_OBS_REC_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsObsRec>(tcpRow, &Rod)) {

#ifdef _TESTING_ESTATS_VALUES
                if ((Rod.CurRwinRcvd > 0x10000000 && Rod.CurRwinRcvd != UninitializedUlong) ||
                    (Rod.MinRwinRcvd > 0x10000000 && Rod.MinRwinRcvd != UninitializedUlong) ||
                    (Rod.MaxRwinRcvd > 0x10000000 && Rod.MaxRwinRcvd != UninitializedUlong) ||
                    (Rod.MinRwinRcvd != UninitializedUlong && Rod.MinRwinRcvd > Rod.MaxRwinRcvd && Rod.MaxRwinRcvd > 0))
                {
                    WCHAR local_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)localAddr.writeCompleteAddress(local_address);
                    WCHAR remote_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)remoteAddr.writeCompleteAddress(remote_address);

                    ++invalidValues;
                    printf(
                        "[%ws : %ws] Bad TcpConnectionEstatsObsRec (TCP_ESTATS_OBS_REC_ROD_v0): "
                        "CurRwinRcvd (%lX) "
                        "MinRwinRcvd (%lX) "
                        "MaxRwinRcvd (%lX)\n",
                        local_address,
                        remote_address,
                        Rod.CurRwinRcvd,
                        Rod.MinRwinRcvd,
                        Rod.MaxRwinRcvd);
                }
                else {
                    ++validValues;
                }
#else
                UNREFERENCED_PARAMETER(localAddr);
                UNREFERENCED_PARAMETER(remoteAddr);
#endif

                if (IsRodValueValid(L"TcpConnectionEstatsObsRec - CurRwinRcvd", Rod.CurRwinRcvd)) {
                    receiveWindow.push_back(Rod.CurRwinRcvd);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsObsRec - MinRwinRcvd", Rod.MinRwinRcvd)) {
                    minReceiveWindow = Rod.MinRwinRcvd;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsObsRec - MaxRwinRcvd", Rod.MaxRwinRcvd)) {
                    maxReceiveWindow = Rod.MaxRwinRcvd;
                }
            }
        }

    private:
        std::vector<ULONG> receiveWindow;
        ULONG minReceiveWindow = 0;
        ULONG maxReceiveWindow = 0;

#ifdef _TESTING_ESTATS_VALUES
        ULONG validValues = 0;
        ULONG invalidValues = 0;
#endif
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsBandwidth> {
    public:
        static LPCWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData() const
        {
            return L"";
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_BANDWIDTH_RW_v0 Rw;
            Rw.EnableCollectionInbound = TcpBoolOptEnabled;
            Rw.EnableCollectionOutbound = TcpBoolOptEnabled;
            SetEstats<TcpConnectionEstatsBandwidth>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&)
        {
            TCP_ESTATS_BANDWIDTH_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsBandwidth>(tcpRow, &Rod)) {
                // store data from this instance
            }
        }
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsFineRtt> {
    public:
        static LPCWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData() const
        {
            return L"";
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_FINE_RTT_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsFineRtt>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&)
        {
            TCP_ESTATS_FINE_RTT_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsFineRtt>(tcpRow, &Rod)) {
                // store data from this instance
            }
        }
    };

    template <TCP_ESTATS_TYPE TcpType>
    class EstatsDataPoint {
    public:
        static LPCWSTR PrintAddressHeader() noexcept
        {
            return L"LocalAddress,RemoteAddress";
        }
        static LPCWSTR PrintHeader()
        {
            return EstatsDataTracking<TcpType>::PrintHeader();
        }

        EstatsDataPoint(ctl::ctSockaddr local_addr, ctl::ctSockaddr remote_addr) noexcept :
            localAddr(std::move(local_addr)),
            remoteAddr(std::move(remote_addr))
        {
        }

        explicit EstatsDataPoint(const PMIB_TCPROW pTcpRow) noexcept :  // NOLINT
            localAddr(AF_INET),
            remoteAddr(AF_INET)
        {
            localAddr.setAddress(
                reinterpret_cast<const PIN_ADDR>(&pTcpRow->dwLocalAddr));
            localAddr.setPort(
                static_cast<unsigned short>(pTcpRow->dwLocalPort),
                ctl::ByteOrder::NetworkOrder);

            remoteAddr.setAddress(
                reinterpret_cast<const PIN_ADDR>(&pTcpRow->dwRemoteAddr));
            remoteAddr.setPort(
                static_cast<unsigned short>(pTcpRow->dwRemotePort),
                ctl::ByteOrder::NetworkOrder);
        }

        explicit EstatsDataPoint(const PMIB_TCP6ROW pTcpRow) noexcept :  // NOLINT
            localAddr(AF_INET6),
            remoteAddr(AF_INET6)
        {
            localAddr.setAddress(&pTcpRow->LocalAddr);
            localAddr.setPort(
                static_cast<unsigned short>(pTcpRow->dwLocalPort),
                ctl::ByteOrder::NetworkOrder);

            remoteAddr.setAddress(&pTcpRow->RemoteAddr);
            remoteAddr.setPort(
                static_cast<unsigned short>(pTcpRow->dwRemotePort),
                ctl::ByteOrder::NetworkOrder);
        }

        ~EstatsDataPoint() = default;
        EstatsDataPoint(const EstatsDataPoint&) = delete;
        EstatsDataPoint& operator=(const EstatsDataPoint&) = delete;
        EstatsDataPoint(EstatsDataPoint&&) = delete;
        EstatsDataPoint& operator=(EstatsDataPoint&&) = delete;

        bool operator< (const EstatsDataPoint<TcpType>& rhs) const noexcept
        {
            if (localAddr < rhs.localAddr) {
                return true;
            }
            if (localAddr == rhs.localAddr &&
                remoteAddr < rhs.remoteAddr) {
                return true;
            }
            return false;
        }
        bool operator==(const EstatsDataPoint<TcpType>& rhs) const noexcept
        {
            return (localAddr == rhs.localAddr) && 
                   (remoteAddr == rhs.remoteAddr);
        }

        std::wstring PrintAddresses() const
        {
            WCHAR local_string[ctl::IP_STRING_MAX_LENGTH];
            (void)localAddr.writeCompleteAddress(local_string);
            WCHAR remote_string[ctl::IP_STRING_MAX_LENGTH];
            (void)remoteAddr.writeCompleteAddress(remote_string);

            return ctl::ctString::format_string(
                L"%ws,%ws",
                local_string,
                remote_string);
        }
        std::wstring PrintData() const
        {
            return data.PrintData();
        }

        template <typename T>
        void StartTracking(T tcpRow) const
        {
            data.StartTracking(tcpRow);
        }

        template <typename T>
        void UpdateData(T tcpRow, ULONG currentCounter) const
        {
            latestCounter = currentCounter;
            data.UpdateData(tcpRow, localAddr, remoteAddr);
        }

        ctl::ctSockaddr LocalAddr() const noexcept
        {
            return localAddr;
        }
        ctl::ctSockaddr RemoteAddr() const noexcept
        {
            return remoteAddr;
        }

        ULONG LastestCounter() const noexcept
        {
            return latestCounter;
        }

    private:
        ctl::ctSockaddr localAddr;
        ctl::ctSockaddr remoteAddr;
        // the tracking object must be mutable because EstatsDataPoint instances
        // are stored in a std::set container, and access to objects in a std::set
        // must be const (since you are not allowed to modify std::set objects in-place)
        mutable EstatsDataTracking<TcpType> data;
        mutable ULONG latestCounter = 0;
    };
} // namespace

class ctsEstats
{
public:
    ctsEstats() :
        pathInfoWriter(L"EstatsPathInfo.csv"),
        receiveWindowWriter(L"EstatsReceiveWindow.csv"),
        senderCongestionWriter(L"EstatsSenderCongestion.csv"),
        tcpTable(StartingTableSize)
    {
    }
    ctsEstats(const ctsEstats&) = delete;
    ctsEstats& operator=(const ctsEstats&) = delete;
    ctsEstats(ctsEstats&&) = delete;
    ctsEstats& operator=(ctsEstats&&) = delete;

    ~ctsEstats()
    {
        timer.stop_all_timers();

        try {
            for (const auto& entry : pathInfoData) {
                pathInfoWriter.write_row(
                    entry.PrintAddresses() +
                    entry.PrintData());
            }

            for (const auto& entry : localReceiveWindowData) {
                const details::EstatsDataPoint<TcpConnectionEstatsObsRec> matchingData(
                    entry.LocalAddr(),
                    entry.RemoteAddr());

                const auto foundEntry = remoteReceiveWindowData.find(matchingData);
                if (foundEntry != remoteReceiveWindowData.end()) {
                    receiveWindowWriter.write_row(
                        entry.PrintAddresses() +
                        entry.PrintData() +
                        foundEntry->PrintData());
                }
            }

            for (const auto& entry : senderCongestionData) {
                const details::EstatsDataPoint<TcpConnectionEstatsData> matchingData(
                    entry.LocalAddr(),
                    entry.RemoteAddr());

                const auto foundEntry = byteTrackingData.find(matchingData);
                if (foundEntry != byteTrackingData.end()) {
                    senderCongestionWriter.write_row(
                        entry.PrintAddresses() +
                        entry.PrintData() +
                        foundEntry->PrintData());
                }
            }
        }
        catch (const std::exception& e) {
            wprintf(L"~Estats exception: %ws\n", ctl::ctString::format_exception(e).c_str());
        }
    }

    bool start() noexcept
    {
        // ReSharper disable once CppInitializedValueIsAlwaysRewritten
        auto started = false;
        try {
            pathInfoWriter.create_file(
                std::wstring(details::EstatsDataPoint<TcpConnectionEstatsPath>::PrintAddressHeader()) +
                L"," + details::EstatsDataPoint<TcpConnectionEstatsPath>::PrintHeader());
            receiveWindowWriter.create_file(
                std::wstring(details::EstatsDataPoint<TcpConnectionEstatsRec>::PrintAddressHeader()) +
                L"," + details::EstatsDataPoint<TcpConnectionEstatsRec>::PrintHeader() +
                L"," + details::EstatsDataPoint<TcpConnectionEstatsObsRec>::PrintHeader());
            senderCongestionWriter.create_file(
                std::wstring(details::EstatsDataPoint<TcpConnectionEstatsSndCong>::PrintAddressHeader()) +
                L"," + details::EstatsDataPoint<TcpConnectionEstatsSndCong>::PrintHeader() +
                L"," + details::EstatsDataPoint<TcpConnectionEstatsData>::PrintHeader());

            started = UpdateEstats();
        }
        catch (const std::exception& e) {
            wprintf(L"ctsEstats::Start exception: %ws\n", ctl::ctString::format_exception(e).c_str());
            started = false;
        }

        if (!started) {
            timer.stop_all_timers();
        }

        return started;
    }

private:
    ctl::ctThreadpoolTimer timer;

    std::set<details::EstatsDataPoint<TcpConnectionEstatsSynOpts>> synOptsData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsData>> byteTrackingData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsPath>> pathInfoData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsRec>> localReceiveWindowData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsObsRec>> remoteReceiveWindowData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsSndCong>> senderCongestionData;

    ctsWriteDetails pathInfoWriter;
    ctsWriteDetails receiveWindowWriter;
    ctsWriteDetails senderCongestionWriter;

    // since updates are always serialized on a timer, just reuse the same buffer
    static const ULONG StartingTableSize = 4096;
    std::vector<char> tcpTable;
    ULONG tableCounter = 0;

    bool UpdateEstats()
    {
        bool accessDenied = false;
        ++tableCounter;
        try {
            // IPv4
            RefreshIPv4Data();
            const auto pIpv4TcpTable = reinterpret_cast<PMIB_TCPTABLE>(&tcpTable[0]);
            for (unsigned count = 0; count < pIpv4TcpTable->dwNumEntries; ++count)
            {
                const auto tableEntry = &pIpv4TcpTable->table[count];
                if (tableEntry->dwState == MIB_TCP_STATE_LISTEN ||
                    tableEntry->dwState == MIB_TCP_STATE_TIME_WAIT ||
                    tableEntry->dwState == MIB_TCP_STATE_DELETE_TCB) {
                    continue;
                }

                try {
                    UpdateDataPoints(synOptsData, tableEntry);
                    UpdateDataPoints(byteTrackingData, tableEntry);
                    UpdateDataPoints(pathInfoData, tableEntry);
                    UpdateDataPoints(localReceiveWindowData, tableEntry);
                    UpdateDataPoints(remoteReceiveWindowData, tableEntry);
                    UpdateDataPoints(senderCongestionData, tableEntry);
                }
                catch (const ctl::ctException& e) {
                    if (e.why() == ERROR_ACCESS_DENIED) {
                        accessDenied = true;
                        throw;
                    }
                }
            }

            // IPv6
            RefreshIPv6Data();
            const auto pIpv6TcpTable = reinterpret_cast<PMIB_TCP6TABLE>(&tcpTable[0]);
            for (unsigned count = 0; count < pIpv6TcpTable->dwNumEntries; ++count)
            {
                const auto tableEntry = &pIpv6TcpTable->table[count];
                if (tableEntry->State == MIB_TCP_STATE_LISTEN ||
                    tableEntry->State == MIB_TCP_STATE_TIME_WAIT ||
                    tableEntry->State == MIB_TCP_STATE_DELETE_TCB) {
                    continue;
                }

                try {
                    UpdateDataPoints(synOptsData, tableEntry);
                    UpdateDataPoints(byteTrackingData, tableEntry);
                    UpdateDataPoints(pathInfoData, tableEntry);
                    UpdateDataPoints(localReceiveWindowData, tableEntry);
                    UpdateDataPoints(remoteReceiveWindowData, tableEntry);
                    UpdateDataPoints(senderCongestionData, tableEntry);
                }
                catch (const ctl::ctException& e) {
                    if (e.why() == ERROR_ACCESS_DENIED) {
                        accessDenied = true;
                        throw;
                    }
                }
            }

            RemoveStaleDataPoints();
        }
        catch (const std::exception& e) {
            wprintf(L"ctsEstats::UpdateEstats exception: %ws\n", ctl::ctString::format_exception(e).c_str());
        }

        if (!accessDenied) {
            // schedule timer from this moment
            timer.schedule_singleton([this]() { UpdateEstats(); }, 1000);
        }

        return !accessDenied;
    }

    void RefreshIPv4Data()
    {
        tcpTable.resize(tcpTable.capacity());
        DWORD table_size = static_cast<DWORD>(tcpTable.size());
        ZeroMemory(&tcpTable[0], table_size);

        ULONG error = ::GetTcpTable(
            reinterpret_cast<PMIB_TCPTABLE>(&tcpTable[0]),
            &table_size,
            FALSE); // no need to sort them
        if (ERROR_INSUFFICIENT_BUFFER == error) {
            tcpTable.resize(table_size);
            error = ::GetTcpTable(
                reinterpret_cast<PMIB_TCPTABLE>(&tcpTable[0]),
                &table_size,
                FALSE); // no need to sort them
        }
        if (error != ERROR_SUCCESS) {
            throw ctl::ctException(error, L"GetTcpTable", L"ctsEstats", false);
        }
    }
    void RefreshIPv6Data()
    {
        tcpTable.resize(tcpTable.capacity());
        DWORD table_size = static_cast<DWORD>(tcpTable.size());
        ZeroMemory(&tcpTable[0], table_size);

        ULONG error = ::GetTcp6Table(
            reinterpret_cast<PMIB_TCP6TABLE>(&tcpTable[0]),
            &table_size,
            FALSE); // no need to sort them
        if (ERROR_INSUFFICIENT_BUFFER == error) {
            tcpTable.resize(table_size);
            error = ::GetTcp6Table(
                reinterpret_cast<PMIB_TCP6TABLE>(&tcpTable[0]),
                &table_size,
                FALSE); // no need to sort them
        }
        if (error != ERROR_SUCCESS) {
            throw ctl::ctException(error, L"GetTcp6Table", L"ctsEstats", false);
        }
    }

    template <TCP_ESTATS_TYPE TcpType, typename MIBTYPE>
    void UpdateDataPoints(std::set<details::EstatsDataPoint<TcpType>>& data, MIBTYPE tableEntry)
    {
        auto emplaceResults = data.emplace(tableEntry);
        // first == iterator inserted
        // second == bool if inserted
        if (emplaceResults.second)
        {
            emplaceResults.first->StartTracking(tableEntry);
        }
        emplaceResults.first->UpdateData(tableEntry, tableCounter);
    }

    void RemoveStaleDataPoints()
    {
        // walk the set of synOptsData. If an address wasn't found to have been updated
        // with the latest data, then we'll remove that tuple (local address + remote address)
        // from all the data sets and finish printing their rows
        auto foundInstance = std::find_if(
            std::begin(synOptsData),
            std::end(synOptsData),
            [&](const details::EstatsDataPoint<TcpConnectionEstatsSynOpts>& dataPoint)
        {
            return dataPoint.LastestCounter() != tableCounter;
        });

        while (foundInstance != std::end(synOptsData))
        {
            const ctl::ctSockaddr localAddr(foundInstance->LocalAddr());
            const ctl::ctSockaddr remoteAddr(foundInstance->RemoteAddr());

            const auto synOptsInstance = foundInstance;
            const auto byteTrackingInstance = byteTrackingData.find(
                details::EstatsDataPoint<TcpConnectionEstatsData>(localAddr, remoteAddr));
            const auto fByteTrackingInstanceFound = byteTrackingInstance != byteTrackingData.end();

            const auto pathInfoInstance = pathInfoData.find(
                details::EstatsDataPoint<TcpConnectionEstatsPath>(localAddr, remoteAddr));
            const auto fPathInfoInstanceFound = pathInfoInstance != pathInfoData.end();

            const auto localReceiveWindowInstance = localReceiveWindowData.find(
                details::EstatsDataPoint<TcpConnectionEstatsRec>(localAddr, remoteAddr));
            const auto fLocalReceiveWindowInstanceFound = localReceiveWindowInstance != localReceiveWindowData.end();

            const auto remoteReceiveWindowInstance = remoteReceiveWindowData.find(
                details::EstatsDataPoint<TcpConnectionEstatsObsRec>(localAddr, remoteAddr));
            const auto fRemoteReceiveWindowInstanceFound = remoteReceiveWindowInstance != remoteReceiveWindowData.end();

            const auto senderCongestionInstance = senderCongestionData.find(
                details::EstatsDataPoint<TcpConnectionEstatsSndCong>(localAddr, remoteAddr));
            const auto fSenderCongestionInstanceFound = senderCongestionInstance != senderCongestionData.end();

            if (fPathInfoInstanceFound) {
                pathInfoWriter.write_row(
                    pathInfoInstance->PrintAddresses() +
                    pathInfoInstance->PrintData());
            }

            if (fLocalReceiveWindowInstanceFound && fRemoteReceiveWindowInstanceFound) {
                receiveWindowWriter.write_row(
                    localReceiveWindowInstance->PrintAddresses() +
                    localReceiveWindowInstance->PrintData() +
                    remoteReceiveWindowInstance->PrintData());
            }
            
            if (fSenderCongestionInstanceFound && fByteTrackingInstanceFound) {
                senderCongestionWriter.write_row(
                    senderCongestionInstance->PrintAddresses() +
                    senderCongestionInstance->PrintData() +
                    byteTrackingInstance->PrintData());
            }

            synOptsData.erase(synOptsInstance);
            if (fByteTrackingInstanceFound) {
                byteTrackingData.erase(byteTrackingInstance);
            }
            if (fLocalReceiveWindowInstanceFound) {
                localReceiveWindowData.erase(localReceiveWindowInstance);
            }
            if (fRemoteReceiveWindowInstanceFound) {
                remoteReceiveWindowData.erase(remoteReceiveWindowInstance);
            }
            if (fSenderCongestionInstanceFound) {
                senderCongestionData.erase(senderCongestionInstance);
            }

            // update the while loop variable
            foundInstance = std::find_if(
                std::begin(synOptsData),
                std::end(synOptsData),
                [&](const details::EstatsDataPoint<TcpConnectionEstatsSynOpts>& dataPoint)
            {
                return dataPoint.LastestCounter() != tableCounter;
            });
        } // while loop
    }
};
} // namespace
