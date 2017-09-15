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
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <Iphlpapi.h>
#include <Tcpestats.h>

// ctl headers
#include <ctMath.hpp>
#include <ctString.hpp>
#include <ctSockaddr.hpp>
#include <ctThreadPoolTimer.hpp>

namespace ctsPerf {

namespace details {
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

    template <typename TCP_ESTATS_TYPE TcpType>
    inline void SetEstats(_In_ const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_write_type pRw)
    {
        ULONG err = ::SetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            reinterpret_cast<PUCHAR>(pRw), 0, static_cast<ULONG>(sizeof(*pRw)),
            0);
        if (err != 0) {
            throw ctl::ctException(err, L"SetPerTcpConnectionEStats", false);
        }
    }

    template <typename TCP_ESTATS_TYPE TcpType>
    inline ULONG GetReadOnlyStaticEstats(_In_ const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_static_type pRos)
    {
        return ::GetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            nullptr, 0, 0, // read-write information
            reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
            nullptr, 0, 0); // read-only dynamic information
    }

    template <typename TCP_ESTATS_TYPE TcpType>
    inline ULONG GetReadOnlyDynamicEstats(_In_ const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_dynamic_type pRod)
    {
        return ::GetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            nullptr, 0, 0, // read-write information
            nullptr, 0, 0, // read-only static information
            reinterpret_cast<PUCHAR>(pRod), 0, static_cast<ULONG>(sizeof(*pRod))); // read-only dynamic information
    }

    template <typename TCP_ESTATS_TYPE TcpType>
    inline void SetEstats(_In_ const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_write_type pRw)
    {
        ULONG err = ::SetPerTcp6ConnectionEStats(
            tcpRow,
            TcpType,
            reinterpret_cast<PUCHAR>(pRw), 0, static_cast<ULONG>(sizeof(*pRw)),
            0);
        if (err != 0) {
            throw ctl::ctException(err, L"SetPerTcp6ConnectionEStats", false);
        }
    }

    template <typename TCP_ESTATS_TYPE TcpType>
    inline ULONG GetReadOnlyStaticEstats(_In_ const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_static_type pRos)
    {
        return ::GetPerTcp6ConnectionEStats(
            tcpRow,
            TcpType,
            nullptr, 0, 0, // read-write information
            reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
            nullptr, 0, 0); // read-only dynamic information
    }

    template <typename TCP_ESTATS_TYPE TcpType>
    inline ULONG GetReadOnlyDynamicEstats(_In_ const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_dynamic_type pRod)
    {
        return ::GetPerTcp6ConnectionEStats(
            tcpRow,
            TcpType,
            nullptr, 0, 0, // read-write information
            nullptr, 0, 0, // read-only static information
            reinterpret_cast<PUCHAR>(pRod), 0, static_cast<ULONG>(sizeof(*pRod))); // read-only dynamic information
    }

    template <typename TCP_ESTATS_TYPE TcpType>
    class EstatsDataTracking {
        EstatsDataTracking() = default;
        ~EstatsDataTracking() = default;

        static LPWSTR PrintHeader();
        void PrintData() const;

        template <typename PTCPROW>
        void StartTracking(_In_ const PTCPROW tcpRow) const;

        template <typename PTCPROW>
        void UpdateData(_In_ const PTCPROW tcpRow);
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsSynOpts> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"Mss-Received,Mss-Sent";
        }
        std::wstring PrintData() const
        {
            return L"," + std::to_wstring(MssRcvd) + L"," + std::to_wstring(MssSent);
        }

        template <typename PTCPROW>
        void StartTracking(_In_ const PTCPROW) const
        {
            return; // always on
        }
        template <typename PTCPROW>
        void UpdateData(_In_ const PTCPROW tcpRow)
        {
            if (MssRcvd == 0) {
                TCP_ESTATS_SYN_OPTS_ROS_v0 Ros;
                ZeroMemory(&Ros, sizeof(Ros));
                if (0 == GetReadOnlyStaticEstats<TcpConnectionEstatsSynOpts>(tcpRow, &Ros)) {
                    MssRcvd = Ros.MssRcvd;
                    MssSent = Ros.MssSent;
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
        static LPWSTR PrintHeader()
        {
            return L"Bytes-In,Bytes-Out";
        }
        std::wstring PrintData() const
        {
            return L"," + std::to_wstring(bytesIn) + L"," + std::to_wstring(bytesOut);
        }

        template <typename PTCPROW>
        void StartTracking(_In_ const PTCPROW tcpRow) const
        {
            TCP_ESTATS_DATA_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsData>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(_In_ const PTCPROW tcpRow)
        {
            TCP_ESTATS_DATA_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsData>(tcpRow, &Rod)) {
                bytesIn = Rod.DataBytesIn;
                bytesOut = Rod.DataBytesOut;
            }
        }

    private:
        ULONG64 bytesIn = 0;
        ULONG64 bytesOut = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsSndCong> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"CongWin(mean),CongWin(stddev),"
                L"XIntoReceiverLimited,XIntoSenderLimited,XIntoCongestionLimited,"
                L"BytesSentRecvLimited,BytesSentSenderLimited,BytesSentCongLimited";
        }
        std::wstring PrintData() const
        {
            return
                ctsPerf::ctsWriteDetails::PrintMeanStdDev(conjectionWindows) +
                ctl::ctString::format_string(
                    L",%lu,%lu,%lu,%Iu,%Iu,%Iu",
                    transitionsIntoReceiverLimited,
                    transitionsIntoSenderLimited,
                    transitionsIntoCongestionLimited,
                    bytesSentInReceiverLimited,
                    bytesSentInSenderLimited,
                    bytesSentInCongestionLimited);
        }

        template <typename PTCPROW>
        void StartTracking(_In_ const PTCPROW tcpRow) const
        {
            TCP_ESTATS_SND_CONG_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsSndCong>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(_In_ const PTCPROW tcpRow)
        {
            TCP_ESTATS_SND_CONG_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsSndCong>(tcpRow, &Rod)) {
                conjectionWindows.push_back(Rod.CurCwnd);
                bytesSentInReceiverLimited = Rod.SndLimBytesRwin;
                bytesSentInSenderLimited = Rod.SndLimBytesSnd;
                bytesSentInCongestionLimited = Rod.SndLimBytesCwnd;
                transitionsIntoReceiverLimited = Rod.SndLimTransRwin;
                transitionsIntoSenderLimited = Rod.SndLimTransSnd;
                transitionsIntoCongestionLimited = Rod.SndLimTransCwnd;
            }
        }

    private:
        std::vector<ULONG> conjectionWindows;

        SIZE_T bytesSentInReceiverLimited = 0;
        SIZE_T bytesSentInSenderLimited = 0;
        SIZE_T bytesSentInCongestionLimited = 0;

        ULONG transitionsIntoReceiverLimited = 0;
        ULONG transitionsIntoSenderLimited = 0;
        ULONG transitionsIntoCongestionLimited = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsPath> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"BytesRetrans,DupeAcks,SelectiveAcks,CongSignals,MaxSegSize,"
                L"RetransTimer(mean),RetransTimer(stddev),"
                L"RTT(mean),Rtt(stddev)";
        }
        std::wstring PrintData() const
        {
            return ctl::ctString::format_string(
                L",%lu,%lu,%lu,%lu,%lu",
                bytesRetrans,
                dupAcksRcvd,
                sacksRcvd,
                congestionSignals,
                maxSegmentSize) +
            ctsPerf::ctsWriteDetails::PrintMeanStdDev(retransmitTimer) +
            ctsPerf::ctsWriteDetails::PrintMeanStdDev(roundTripTime);
        }

        template <typename PTCPROW>
        void StartTracking(_In_ const PTCPROW tcpRow) const
        {
            TCP_ESTATS_PATH_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsPath>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(_In_ const PTCPROW tcpRow)
        {
            TCP_ESTATS_PATH_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsPath>(tcpRow, &Rod)) {
                retransmitTimer.push_back(Rod.CurRto);
                roundTripTime.push_back(Rod.SmoothedRtt);
                bytesRetrans = Rod.BytesRetrans;
                dupAcksRcvd = Rod.DupAcksIn;
                sacksRcvd = Rod.SacksRcvd;
                congestionSignals = Rod.CongSignals;
                maxSegmentSize = Rod.CurMss;
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
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsRec> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"LocalRecvWin(min),LocalRecvWin(max),LocalRecvWin(mean),LocalRecvWin(stddev)";
        }
        std::wstring PrintData() const
        {
            // casting min and max to signed since -1 is a valid value
            return
                ctl::ctString::format_string(
                    L",%ld,%ld",
                    static_cast<long>(minReceiveWindow),
                    static_cast<long>(maxReceiveWindow)) +
                ctsPerf::ctsWriteDetails::PrintMeanStdDev(receiveWindow);
        }

        template <typename PTCPROW>
        void StartTracking(_In_ const PTCPROW tcpRow) const
        {
            TCP_ESTATS_REC_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsRec>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(_In_ const PTCPROW tcpRow)
        {
            TCP_ESTATS_REC_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsRec>(tcpRow, &Rod)) {
                receiveWindow.push_back(Rod.CurRwinSent);
                minReceiveWindow = Rod.MinRwinSent;
                maxReceiveWindow = Rod.MaxRwinSent;
            }
        }

    private:
        std::vector<ULONG> receiveWindow;
        ULONG minReceiveWindow = 0;
        ULONG maxReceiveWindow = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsObsRec> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"RemoteRecvWin(min),RemoteRecvWin(max),RemoteRecvWin(mean),RemoteRecvWin(stddev)";
        }
        std::wstring PrintData() const
        {
            // casting min and max to signed since -1 is a valid value
            return
                ctl::ctString::format_string(
                    L",%ld,%ld",
                    static_cast<long>(minReceiveWindow),
                    static_cast<long>(maxReceiveWindow)) +
                ctsPerf::ctsWriteDetails::PrintMeanStdDev(receiveWindow);
        }

        template <typename PTCPROW>
        void StartTracking(_In_ const PTCPROW tcpRow) const
        {
            TCP_ESTATS_OBS_REC_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsObsRec>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(_In_ const PTCPROW tcpRow)
        {
            TCP_ESTATS_OBS_REC_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsObsRec>(tcpRow, &Rod)) {
                receiveWindow.push_back(Rod.CurRwinRcvd);
                minReceiveWindow = Rod.MinRwinRcvd;
                maxReceiveWindow = Rod.MaxRwinRcvd;
            }
        }

    private:
        std::vector<ULONG> receiveWindow;
        ULONG minReceiveWindow = 0;
        ULONG maxReceiveWindow = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsBandwidth> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData() const
        {
            return L"";
        }

        template <typename PTCPROW>
        void StartTracking(_In_ const PTCPROW tcpRow) const
        {
            TCP_ESTATS_BANDWIDTH_RW_v0 Rw;
            Rw.EnableCollectionInbound = TcpBoolOptEnabled;
            Rw.EnableCollectionOutbound = TcpBoolOptEnabled;
            SetEstats<TcpConnectionEstatsBandwidth>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(_In_ const PTCPROW tcpRow)
        {
            TCP_ESTATS_BANDWIDTH_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsBandwidth>(tcpRow, &Rod)) {
                // store data from this instance
            }
        }

    private:

    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsFineRtt> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData() const
        {
            return L"";
        }

        template <typename PTCPROW>
        void StartTracking(_In_ const PTCPROW tcpRow) const
        {
            TCP_ESTATS_FINE_RTT_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetEstats<TcpConnectionEstatsFineRtt>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(_In_ const PTCPROW tcpRow)
        {
            TCP_ESTATS_FINE_RTT_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsFineRtt>(tcpRow, &Rod)) {
                // store data from this instance
            }
        }

    private:
    
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

        EstatsDataPoint(const ctl::ctSockaddr& local_addr, const ctl::ctSockaddr& remote_addr) noexcept :
            localAddr(local_addr),
            remoteAddr(remote_addr)
        {
        }
        EstatsDataPoint(_In_ const PMIB_TCPROW pTcpRow) noexcept :
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
        EstatsDataPoint(_In_ const PMIB_TCP6ROW pTcpRow) noexcept :
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

        EstatsDataPoint(const EstatsDataPoint&) = delete;
        EstatsDataPoint& operator=(const EstatsDataPoint&) = delete;

        bool operator< (const details::EstatsDataPoint<TcpType>& rhs) const noexcept
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
            localAddr.writeCompleteAddress(local_string);
            WCHAR remote_string[ctl::IP_STRING_MAX_LENGTH];
            remoteAddr.writeCompleteAddress(remote_string);

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
            data.UpdateData(tcpRow);
        }

        void StartWriter(_In_ LPCWSTR filename)
        {
            writer.reset(filename);
        }

        ctl::ctSockaddr LocalAddr() const noexcept
        {
            return localAddr;
        }
        ctl::ctSockaddr RemoteAddr() const noexcept
        {
            return remoteAddr;
        }

        const ULONG LastestCounter() const noexcept
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
                details::EstatsDataPoint<TcpConnectionEstatsObsRec> matchingData(
                    entry.LocalAddr(),
                    entry.RemoteAddr());

                auto foundEntry = remoteReceiveWindowData.find(matchingData);
                if (foundEntry != remoteReceiveWindowData.end()) {
                    receiveWindowWriter.write_row(
                        entry.PrintAddresses() +
                        entry.PrintData() +
                        foundEntry->PrintData());
                }
            }

            for (const auto& entry : senderCongestionData) {
                details::EstatsDataPoint<TcpConnectionEstatsData> matchingData(
                    entry.LocalAddr(),
                    entry.RemoteAddr());

                auto foundEntry = byteTrackingData.find(matchingData);
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

    bool Start() noexcept
    {
        bool started = false;
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
            PMIB_TCPTABLE pIpv4TcpTable = reinterpret_cast<PMIB_TCPTABLE>(&tcpTable[0]);
            for (unsigned count = 0; count < pIpv4TcpTable->dwNumEntries; ++count)
            {
                const auto tableEntry = &pIpv4TcpTable->table[count];
                if (tableEntry->dwState == MIB_TCP_STATE_LISTEN) {
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
            PMIB_TCP6TABLE pIpv6TcpTable = reinterpret_cast<PMIB_TCP6TABLE>(&tcpTable[0]);
            for (unsigned count = 0; count < pIpv6TcpTable->dwNumEntries; ++count)
            {
                const auto tableEntry = &pIpv6TcpTable->table[count];
                if (tableEntry->State == MIB_TCP_STATE_LISTEN) {
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
            ctl::ctSockaddr localAddr(foundInstance->LocalAddr());
            ctl::ctSockaddr remoteAddr(foundInstance->RemoteAddr());

            const auto synOptsInstance = foundInstance;
            const auto byteTrackingInstance = byteTrackingData.find(
                details::EstatsDataPoint<TcpConnectionEstatsData>(localAddr, remoteAddr));
            bool byteTrackingInstanceFound = byteTrackingInstance != byteTrackingData.end();

            const auto pathInfoInstance = pathInfoData.find(
                details::EstatsDataPoint<TcpConnectionEstatsPath>(localAddr, remoteAddr));
            bool pathInfoInstanceFound = pathInfoInstance != pathInfoData.end();

            const auto localReceiveWindowInstance = localReceiveWindowData.find(
                details::EstatsDataPoint<TcpConnectionEstatsRec>(localAddr, remoteAddr));
            bool localReceiveWindowInstanceFound = localReceiveWindowInstance != localReceiveWindowData.end();

            const auto remoteReceiveWindowInstance = remoteReceiveWindowData.find(
                details::EstatsDataPoint<TcpConnectionEstatsObsRec>(localAddr, remoteAddr));
            bool remoteReceiveWindowInstanceFound = remoteReceiveWindowInstance != remoteReceiveWindowData.end();

            const auto senderCongestionInstance = senderCongestionData.find(
                details::EstatsDataPoint<TcpConnectionEstatsSndCong>(localAddr, remoteAddr));
            bool senderCongestionInstanceFound = senderCongestionInstance != senderCongestionData.end();

            if (pathInfoInstanceFound) {
                pathInfoWriter.write_row(
                    pathInfoInstance->PrintAddresses() +
                    pathInfoInstance->PrintData());
            }

            if (localReceiveWindowInstanceFound && remoteReceiveWindowInstanceFound) {
                receiveWindowWriter.write_row(
                    localReceiveWindowInstance->PrintAddresses() +
                    localReceiveWindowInstance->PrintData() +
                    remoteReceiveWindowInstance->PrintData());
            }
            
            if (senderCongestionInstanceFound && byteTrackingInstanceFound) {
                senderCongestionWriter.write_row(
                    senderCongestionInstance->PrintAddresses() +
                    senderCongestionInstance->PrintData() +
                    byteTrackingInstance->PrintData());
            }

            synOptsData.erase(synOptsInstance);
            if (byteTrackingInstanceFound) {
                byteTrackingData.erase(byteTrackingInstance);
            }
            if (localReceiveWindowInstanceFound) {
                localReceiveWindowData.erase(localReceiveWindowInstance);
            }
            if (remoteReceiveWindowInstanceFound) {
                remoteReceiveWindowData.erase(remoteReceiveWindowInstance);
            }
            if (senderCongestionInstanceFound) {
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
