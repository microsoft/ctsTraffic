/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <vector>
#include <set>
#include <unordered_map>
#include <map>
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
#include <ctMath.hpp>

#define _LIVE_PRINT

namespace ctsPerf {

namespace details {

    // Invalid*EstatsValues come only when we have enabled Application Verifier
    // pageheap will pack uninitialized heap with this bit pattern
    // This helps us detect if we are trying to read uninitialized memory in our structs
    // returned from estats* APIs
    static const unsigned long InvalidLongEstatsValue = 0xc0c0c0c0;
    static const unsigned long long InvalidLongLongEstatsValue = 0xc0c0c0c0c0c0c0c0;

    // Will also check for -1 since we will initialize our structs with that value before handing them down
    // if we see -1 it tells us that there's not an ESTATS value in this case
    inline
    bool IsRodValueValid(_In_ LPCWSTR name, ULONG t) noexcept
    {
#ifdef _TESTING_ESTATS_VALUES
        if (t == InvalidLongEstatsValue)
        {
            wprintf(L"\t** %ws : %lu\n", name, t);
            return false;
        }
#endif
        if (t == 0xffffffff)
        {
            return false;
        }

        UNREFERENCED_PARAMETER(name);
        return true;
    }
    inline
    bool IsRodValueValid(_In_ LPCWSTR name, ULONG64 t) noexcept
    {
#ifdef _TESTING_ESTATS_VALUES
        if (t == InvalidLongLongEstatsValue)
        {
            wprintf(L"\t** %ws : %llu\n", name, t);
            return false;
        }
#endif
        if (t == 0xffffffffffffffff)
        {
            return false;
        }
        
        UNREFERENCED_PARAMETER(name);
        return true;
    }

    template <TCP_ESTATS_TYPE TcpType>
    struct EstatsTypeConverter {};

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsSynOpts> {
        typedef void* read_write_type;
        typedef TCP_ESTATS_SYN_OPTS_ROS_v0 read_only_static_type;
        typedef void* read_only_dynamic_type;
    };

    template<>
    struct EstatsTypeConverter<TcpConnectionEstatsData> {
        typedef TCP_ESTATS_DATA_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef TCP_ESTATS_DATA_ROD_v0 read_only_dynamic_type;
    };

    template<>
    struct EstatsTypeConverter<TcpConnectionEstatsSndCong> {
        typedef TCP_ESTATS_SND_CONG_RW_v0 read_write_type;
        typedef TCP_ESTATS_SND_CONG_ROS_v0 read_only_static_type;
        typedef TCP_ESTATS_SND_CONG_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsPath> {
        typedef TCP_ESTATS_PATH_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef TCP_ESTATS_PATH_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsSendBuff> {
        typedef TCP_ESTATS_SEND_BUFF_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef TCP_ESTATS_SEND_BUFF_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsRec> {
        typedef TCP_ESTATS_REC_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef TCP_ESTATS_REC_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsObsRec> {
        typedef TCP_ESTATS_OBS_REC_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef TCP_ESTATS_OBS_REC_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsBandwidth> {
        typedef TCP_ESTATS_BANDWIDTH_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef TCP_ESTATS_BANDWIDTH_ROD_v0 read_only_dynamic_type;
    };

    template <>
    struct EstatsTypeConverter<TcpConnectionEstatsFineRtt> {
        typedef TCP_ESTATS_FINE_RTT_RW_v0 read_write_type;
        typedef void* read_only_static_type;
        typedef TCP_ESTATS_FINE_RTT_ROD_v0 read_only_dynamic_type;
    };

    template <TCP_ESTATS_TYPE TcpType>
    void SetPerConnectionEstats(const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_write_type *pRw)  // NOLINT
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
    void SetPerConnectionEstats(const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_write_type *pRw)  // NOLINT
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

    // TcpConnectionEstatsSynOpts is unique in that there isn't a RW type to query for
    inline ULONG GetPerConnectionStaticEstats(const PMIB_TCPROW tcpRow, TCP_ESTATS_SYN_OPTS_ROS_v0* pRos) noexcept  // NOLINT
    {
        return ::GetPerTcpConnectionEStats(
            tcpRow,
            TcpConnectionEstatsSynOpts,
            nullptr, 0, 0, // read-write information
            reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
            nullptr, 0, 0); // read-only dynamic information
    }
    template <TCP_ESTATS_TYPE TcpType>
    ULONG GetPerConnectionStaticEstats(const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_static_type *pRos) noexcept  // NOLINT
    {
        typename EstatsTypeConverter<TcpType>::read_write_type rw;

        auto error = ::GetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            reinterpret_cast<PUCHAR>(&rw), 0, static_cast<ULONG>(sizeof(rw)), // read-write information
            reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
            nullptr, 0, 0); // read-only dynamic information
        // only return success if the read-only dynamic struct returned that this was enabled
        // else the read-only static information is not populated
        if (error == ERROR_SUCCESS && !rw.EnableCollection)
        {
            error = ERROR_NO_DATA;
        }
        return error;
    }

    // TcpConnectionEstatsSynOpts is unique in that there isn't a RW type to query for
    inline ULONG GetPerConnectionStaticEstats(const PMIB_TCP6ROW tcpRow, TCP_ESTATS_SYN_OPTS_ROS_v0* pRos) noexcept  // NOLINT
    {
        return ::GetPerTcp6ConnectionEStats(
            tcpRow,
            TcpConnectionEstatsSynOpts,
            nullptr, 0, 0, // read-write information
            reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
            nullptr, 0, 0); // read-only dynamic information
    }
    template <TCP_ESTATS_TYPE TcpType>
    ULONG GetPerConnectionStaticEstats(const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_static_type *pRos) noexcept  // NOLINT
    {
        typename EstatsTypeConverter<TcpType>::read_write_type rw;

        auto error = ::GetPerTcp6ConnectionEStats(
            tcpRow,
            TcpType,
            reinterpret_cast<PUCHAR>(&rw), 0, static_cast<ULONG>(sizeof(rw)), // read-write information
            reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
            nullptr, 0, 0); // read-only dynamic information
        // only return success if the read-only dynamic struct returned that this was enabled
        // else the read-only static information is not populated
        if (error == ERROR_SUCCESS && !rw.EnableCollection)
        {
            error = ERROR_NO_DATA;
        }
        return error;
    }


    // TcpConnectionEstatsBandwidth is unique in that there are two rw flags
    inline ULONG GetPerConnectionDynamicEstats(const PMIB_TCPROW tcpRow, TCP_ESTATS_BANDWIDTH_ROD_v0* pRod) noexcept // NOLINT
    {
        EstatsTypeConverter<TcpConnectionEstatsBandwidth>::read_write_type rw;

        auto error = ::GetPerTcpConnectionEStats(
            tcpRow,
            TcpConnectionEstatsBandwidth,
            reinterpret_cast<PUCHAR>(&rw), 0, static_cast<ULONG>(sizeof(rw)), // read-write information
            nullptr, 0, 0, // read-only static information
            reinterpret_cast<PUCHAR>(pRod), 0, static_cast<ULONG>(sizeof(*pRod))); // read-only dynamic information
        // only return success if the read-only dynamic struct returned that this was enabled
        // else the read-only static information is not populated
        if (error == ERROR_SUCCESS && !rw.EnableCollectionInbound && !rw.EnableCollectionOutbound)
        {
            error = ERROR_NO_DATA;
        }
        return error;
    }    
    template <TCP_ESTATS_TYPE TcpType>
    ULONG GetPerConnectionDynamicEstats(const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_dynamic_type *pRod) noexcept  // NOLINT
    {
        typename EstatsTypeConverter<TcpType>::read_write_type rw;

        auto error = ::GetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            reinterpret_cast<PUCHAR>(&rw), 0, static_cast<ULONG>(sizeof(rw)), // read-write information
            nullptr, 0, 0, // read-only static information
            reinterpret_cast<PUCHAR>(pRod), 0, static_cast<ULONG>(sizeof(*pRod))); // read-only dynamic information
        // only return success if the read-only dynamic struct returned that this was enabled
        // else the read-only static information is not populated
        if (error == ERROR_SUCCESS && !rw.EnableCollection)
        {
            error = ERROR_NO_DATA;
        }
        return error;
    }
    // TcpConnectionEstatsBandwidth is unique in that there are two rw flags
    inline ULONG GetPerConnectionDynamicEstats(const PMIB_TCP6ROW tcpRow, TCP_ESTATS_BANDWIDTH_ROD_v0* pRod) noexcept // NOLINT
    {
        EstatsTypeConverter<TcpConnectionEstatsBandwidth>::read_write_type rw;

        auto error = ::GetPerTcp6ConnectionEStats(
            tcpRow,
            TcpConnectionEstatsBandwidth,
            reinterpret_cast<PUCHAR>(&rw), 0, static_cast<ULONG>(sizeof(rw)),      // read-write information
            nullptr, 0, 0,                                                         // read-only static information
            reinterpret_cast<PUCHAR>(pRod), 0, static_cast<ULONG>(sizeof(*pRod))); // read-only dynamic information
        // only return success if the read-only dynamic struct returned that this was enabled
        // else the read-only static information is not populated
        if (error == ERROR_SUCCESS && !rw.EnableCollectionInbound && !rw.EnableCollectionOutbound)
        {
            error = ERROR_NO_DATA;
        }
        return error;
    }
    template <TCP_ESTATS_TYPE TcpType>
    ULONG GetPerConnectionDynamicEstats(const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_dynamic_type *pRod) noexcept  // NOLINT
    {
        typename EstatsTypeConverter<TcpType>::read_write_type rw;

        auto error = ::GetPerTcp6ConnectionEStats(
            tcpRow,
            TcpType,
            reinterpret_cast<PUCHAR>(&rw), 0, static_cast<ULONG>(sizeof(rw)), // read-write information
            nullptr, 0, 0, // read-only static information
            reinterpret_cast<PUCHAR>(pRod), 0, static_cast<ULONG>(sizeof(*pRod))); // read-only dynamic information
        // only return success if the read-only dynamic struct returned that this was enabled
        // else the read-only static information is not populated
        if (error == ERROR_SUCCESS && !rw.EnableCollection)
        {
            error = ERROR_NO_DATA;
        }
        return error;
    }

    // the root template type that each ESTATS_TYPE will specialize for
    template <TCP_ESTATS_TYPE TcpType>
    class EstatsDataTracking {
        EstatsDataTracking() = default;
        ~EstatsDataTracking() = default;
    public:
        EstatsDataTracking(const EstatsDataTracking&) = delete;
        EstatsDataTracking& operator=(const EstatsDataTracking&) = delete;
        EstatsDataTracking(EstatsDataTracking&&) = delete;
        EstatsDataTracking& operator=(EstatsDataTracking&&) = delete;

        static LPCWSTR PrintHeader() = delete;

        void PrintData() const = delete;

        template <typename PTCPROW>
        void StartTracking(PTCPROW tcpRow) const = delete;

        template <typename PTCPROW>
        void UpdateData(PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr) = delete;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsSynOpts> {
    public:
        static LPCWSTR PrintHeader() noexcept
        {
            return L"Mss-Received,Mss-Sent";
        }
        std::wstring PrintData() const
        {
            std::wstring formattedString(L"");
            formattedString += (MssRcvd.empty()) ? L"," : L"," + std::to_wstring(MssRcvd.back());
            formattedString += (MssSent.empty()) ? L"," : L"," + std::to_wstring(MssSent.back());
            return formattedString;
        }
        // std::wstring DetailPrintData(std::unordered_map<std::wstring, BOOLEAN> &liveTrackedStatistics) const
        // {
        //     return 
        // }
        std::unordered_map<std::wstring, std::vector<ULONG64> *> GetData()
        {
            return {
                {L"MssRcvd", &MssRcvd},
                {L"MssSent", &MssSent}
            };
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW) const noexcept
        {
            // always on
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&)
        {
            if (MssRcvdCount == 0) {
                TCP_ESTATS_SYN_OPTS_ROS_v0 Ros;
                FillMemory(&Ros, sizeof Ros, -1);
                if (0 == GetPerConnectionStaticEstats(tcpRow, &Ros)) {

                    if (IsRodValueValid(L"TcpConnectionEstatsSynOpts - MssRcvd", Ros.MssRcvd)) {
                        MssRcvd.push_back(Ros.MssRcvd - MssRcvdCount);
                        MssRcvdCount = Ros.MssRcvd;
                    }
                    if (IsRodValueValid(L"TcpConnectionEstatsSynOpts - MssSent", Ros.MssSent)) {
                        MssSent.push_back(Ros.MssSent - MssSentCount);
                        MssSentCount = Ros.MssSent;
                    }
                }
            }
        }

    private:
        std::vector<ULONG64> MssRcvd;
        ULONG MssRcvdCount = 0;
        std::vector<ULONG64> MssSent;
        ULONG MssSentCount = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsData> {
    public:
        static LPCWSTR PrintHeader() noexcept
        {
            return L"Bytes-In,Bytes-Out";
        }
        std::wstring PrintData() const
        {
            std::wstring formattedString(L"");
            formattedString += (DataBytesIn.empty()) ? L"," : L"," + std::to_wstring(DataBytesIn.back());
            formattedString += (DataBytesOut.empty()) ? L"," : L"," + std::to_wstring(DataBytesOut.back());
            return formattedString;
        }
        std::unordered_map<std::wstring, std::vector<ULONG64>*> GetData()
        {
            return {
                {L"DataBytesIn", &DataBytesIn},
                {L"DataBytesOut", &DataBytesOut}
            };
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_DATA_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetPerConnectionEstats<TcpConnectionEstatsData>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&)
        {
            TCP_ESTATS_DATA_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsData>(tcpRow, &Rod)) {

                if (IsRodValueValid(L"TcpConnectionEstatsData - DataBytesIn", Rod.DataBytesIn)) {
                    DataBytesIn.push_back(Rod.DataBytesIn - DataBytesInCount);
                    DataBytesInCount = Rod.DataBytesIn;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsData - DataBytesOut", Rod.DataBytesOut)) {
                    DataBytesOut.push_back(Rod.DataBytesOut - DataBytesOutCount);
                    DataBytesOutCount = Rod.DataBytesOut;                
                }
            }
        }

    private:
        std::vector<ULONG64> DataBytesIn;
        ULONG64 DataBytesInCount = 0;
        std::vector<ULONG64> DataBytesOut;
        ULONG64 DataBytesOutCount = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsSndCong> {
    public:
        static LPCWSTR PrintHeader() noexcept
        {
            return L"CongWin(mean),CongWin(stddev),"
                L"XIntoReceiverLimited,XIntoSenderLimited,XIntoCongestionLimited,"
                L"BytesSentRecvLimited,BytesSentSenderLimited,BytesSentCongLimited";
        }
        std::wstring PrintData() const
        {
            std::wstring formattedString = ctsPerf::ctsWriteDetails::PrintMeanStdDev(conjestionWindow);
            formattedString += (transitionsIntoReceiverLimited.empty()) ? L"," : L"," + std::to_wstring(transitionsIntoReceiverLimited.back());
            formattedString += (transitionsIntoSenderLimited.empty()) ? L"," : L"," + std::to_wstring(transitionsIntoSenderLimited.back());
            formattedString += (transitionsIntoCongestionLimited.empty()) ? L"," : L"," + std::to_wstring(transitionsIntoCongestionLimited.back());
            formattedString += (bytesSentInReceiverLimited.empty()) ? L"," : L"," + std::to_wstring(bytesSentInReceiverLimited.back());
            formattedString += (bytesSentInSenderLimited.empty()) ? L"," : L"," + std::to_wstring(bytesSentInSenderLimited.back());
            formattedString += (bytesSentInCongestionLimited.empty()) ? L"," : L"," + std::to_wstring(bytesSentInCongestionLimited.back());
            return formattedString;

        }
        std::unordered_map<std::wstring, std::vector<ULONG64> *> GetData()
        {
            return {
                {L"conjestionWindow", &conjestionWindow},
                {L"bytesSentInReceiverLimited", &bytesSentInReceiverLimited},
                {L"bytesSentInSenderLimited", &bytesSentInSenderLimited},
                {L"bytesSentInCongestionLimited", &bytesSentInCongestionLimited},
                {L"transitionsIntoReceiverLimited", &transitionsIntoReceiverLimited},
                {L"transitionsIntoSenderLimited", &transitionsIntoSenderLimited},
                {L"transitionsIntoCongestionLimited", &transitionsIntoCongestionLimited}
            };
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_SND_CONG_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetPerConnectionEstats<TcpConnectionEstatsSndCong>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr)
        {
            TCP_ESTATS_SND_CONG_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsSndCong>(tcpRow, &Rod)) {

#ifdef _TESTING_ESTATS_VALUES
                if (Rod.CurCwnd == InvalidLongEstatsValue ||
                    Rod.SndLimBytesRwin == InvalidLongLongEstatsValue ||
                    Rod.SndLimBytesSnd == InvalidLongLongEstatsValue ||
                    Rod.SndLimBytesCwnd == InvalidLongLongEstatsValue ||
                    Rod.SndLimTransRwin == InvalidLongEstatsValue ||
                    Rod.SndLimTransSnd == InvalidLongEstatsValue ||
                    Rod.SndLimTransCwnd == InvalidLongEstatsValue)
                {
                    WCHAR local_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)localAddr.writeCompleteAddress(local_address);
                    WCHAR remote_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)remoteAddr.writeCompleteAddress(remote_address);

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
                }
#endif
                UNREFERENCED_PARAMETER(localAddr);
                UNREFERENCED_PARAMETER(remoteAddr);

                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - CurCwnd", Rod.CurCwnd)) {
                    conjestionWindow.push_back(Rod.CurCwnd);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimBytesRwin", Rod.SndLimBytesRwin)) {
                    bytesSentInReceiverLimited.push_back(Rod.SndLimBytesRwin - bytesSentInReceiverLimitedCount);
                    bytesSentInReceiverLimitedCount = Rod.SndLimBytesRwin;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimBytesSnd", Rod.SndLimBytesSnd)) {
                    bytesSentInSenderLimited.push_back(Rod.SndLimBytesSnd - bytesSentInSenderLimitedCount);
                    bytesSentInSenderLimitedCount = Rod.SndLimBytesSnd;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimBytesCwnd", Rod.SndLimBytesCwnd)) {
                    bytesSentInCongestionLimited.push_back(Rod.SndLimBytesCwnd - bytesSentInCongestionLimitedCount);
                    bytesSentInCongestionLimitedCount = Rod.SndLimBytesCwnd;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimTransRwin", Rod.SndLimTransRwin)) {
                    transitionsIntoReceiverLimited.push_back(Rod.SndLimTransRwin - transitionsIntoReceiverLimitedCount);
                    transitionsIntoReceiverLimitedCount = Rod.SndLimTransRwin;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimTransSnd", Rod.SndLimTransSnd)) {
                    transitionsIntoSenderLimited.push_back(Rod.SndLimTransSnd - transitionsIntoSenderLimitedCount);
                    transitionsIntoSenderLimitedCount = Rod.SndLimTransSnd;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimTransCwnd", Rod.SndLimTransCwnd)) {
                    transitionsIntoCongestionLimited.push_back(Rod.SndLimTransCwnd - transitionsIntoCongestionLimitedCount);
                    transitionsIntoCongestionLimitedCount = Rod.SndLimTransCwnd;
                }
            }
        }

    private:
        std::vector<ULONG64> conjestionWindow;

        std::vector<ULONG64> bytesSentInReceiverLimited;
        ULONG64 bytesSentInReceiverLimitedCount = 0;
        std::vector<ULONG64> bytesSentInSenderLimited;
        ULONG64 bytesSentInSenderLimitedCount = 0;
        std::vector<ULONG64> bytesSentInCongestionLimited;
        ULONG64 bytesSentInCongestionLimitedCount = 0;

        std::vector<ULONG64> transitionsIntoReceiverLimited;
        ULONG64 transitionsIntoReceiverLimitedCount = 0;
        std::vector<ULONG64> transitionsIntoSenderLimited;
        ULONG64 transitionsIntoSenderLimitedCount = 0;
        std::vector<ULONG64> transitionsIntoCongestionLimited;
        ULONG64 transitionsIntoCongestionLimitedCount = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsPath> {
    public:
        static LPCWSTR PrintHeader() noexcept
        {
            return L"BytesRetrans,DupeAcks,SelectiveAcks,CongSignals,MaxSegSize,"
                L"RetransTimer(mean),RetransTimer(stddev),"
                L"RTT(mean),Rtt(stddev)";
        }
        std::wstring PrintData() const
        {
            std::wstring formattedString(L"");
            formattedString += (bytesRetrans.empty()) ? L"," : L"," + std::to_wstring(bytesRetrans.back());
            formattedString += (dupAcksRcvd.empty()) ? L"," : L"," + std::to_wstring(dupAcksRcvd.back());
            formattedString += (sacksRcvd.empty()) ? L"," : L"," + std::to_wstring(sacksRcvd.back());
            formattedString += (congestionSignals.empty()) ? L"," : L"," + std::to_wstring(congestionSignals.back());
            formattedString += (maxSegmentSize.empty()) ? L"," : L"," + std::to_wstring(maxSegmentSize.back());
            formattedString += ctsWriteDetails::PrintMeanStdDev(retransmitTimer);
            formattedString += ctsWriteDetails::PrintMeanStdDev(roundTripTime);
            return formattedString;
        }
        std::unordered_map<std::wstring, std::vector<ULONG64> *> GetData()
        {
            return {
                {L"retransmitTimer", &retransmitTimer},
                {L"roundTripTime", &roundTripTime},
                {L"bytesRetrans", &bytesRetrans},
                {L"dupAcksRcvd", &dupAcksRcvd},
                {L"sacksRcvd", &sacksRcvd},
                {L"congestionSignals", &congestionSignals},
                {L"maxSegmentSize", &maxSegmentSize}
            };
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_PATH_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetPerConnectionEstats<TcpConnectionEstatsPath>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr)
        {
            TCP_ESTATS_PATH_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsPath>(tcpRow, &Rod)) {

#ifdef _TESTING_ESTATS_VALUES
                if (Rod.CurRto == InvalidLongEstatsValue ||
                    Rod.SmoothedRtt == InvalidLongEstatsValue ||
                    Rod.BytesRetrans == InvalidLongEstatsValue ||
                    Rod.DupAcksIn == InvalidLongEstatsValue ||
                    Rod.SacksRcvd == InvalidLongEstatsValue ||
                    Rod.CongSignals == InvalidLongEstatsValue ||
                    Rod.CurMss == InvalidLongEstatsValue)
                {
                    WCHAR local_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)localAddr.writeCompleteAddress(local_address);
                    WCHAR remote_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)remoteAddr.writeCompleteAddress(remote_address);

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
                }
#endif
                UNREFERENCED_PARAMETER(localAddr);
                UNREFERENCED_PARAMETER(remoteAddr);

                if (IsRodValueValid(L"TcpConnectionEstatsPath - CurRto", Rod.CurRto)) {
                    retransmitTimer.push_back(Rod.CurRto);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - SmoothedRtt", Rod.SmoothedRtt)) {
                    roundTripTime.push_back(Rod.SmoothedRtt);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - BytesRetrans", Rod.BytesRetrans)) {
                    bytesRetrans.push_back(Rod.BytesRetrans - bytesRetransCount);
                    bytesRetransCount = Rod.BytesRetrans;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - DupAcksIn", Rod.DupAcksIn)) {
                    dupAcksRcvd.push_back(Rod.DupAcksIn - dupAcksRcvdCount);
                    dupAcksRcvdCount = Rod.DupAcksIn;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - SacksRcvd", Rod.SacksRcvd)) {
                    sacksRcvd.push_back(Rod.SacksRcvd - sacksRcvdCount);
                    sacksRcvdCount = Rod.SacksRcvd;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - CongSignals", Rod.CongSignals)) {
                    congestionSignals.push_back(Rod.CongSignals - congestionSignalsCount);
                    congestionSignalsCount = Rod.CongSignals;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - CurMss", Rod.CurMss)) {
                    maxSegmentSize.push_back(Rod.CurMss - maxSegmentSizeCount);
                    maxSegmentSizeCount = Rod.CurMss;
                }
            }
        }

    private:
        std::vector<ULONG64> retransmitTimer;
        std::vector<ULONG64> roundTripTime;

        std::vector<ULONG64> bytesRetrans;
        ULONG64 bytesRetransCount = 0;
        std::vector<ULONG64> dupAcksRcvd;
        ULONG64 dupAcksRcvdCount = 0;
        std::vector<ULONG64> sacksRcvd;
        ULONG64 sacksRcvdCount = 0;
        std::vector<ULONG64> congestionSignals;
        ULONG64 congestionSignalsCount = 0;
        std::vector<ULONG64> maxSegmentSize;
        ULONG64 maxSegmentSizeCount = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsRec> {
    public:
        static LPCWSTR PrintHeader() noexcept
        {
            return L"LocalRecvWin(min),LocalRecvWin(max),LocalRecvWin(calculated-min),LocalRecvWin(calculated-max),LocalRecvWin(calculated-mean),LocalRecvWin(calculated-stddev)";
        }
        std::wstring PrintData() const
        {
            std::wstring formattedString(L",");
            formattedString += ((!minReceiveWindow.empty()) && (minReceiveWindow.back() == InvalidLongEstatsValue)) ?
                L"(bad)," :
                ctl::ctString::format_string(L"%lu,", minReceiveWindow.back());

            formattedString += ((!maxReceiveWindow.empty()) && (maxReceiveWindow.back() == InvalidLongEstatsValue)) ?
                L"(bad)," :
                ctl::ctString::format_string(L"%lu,", maxReceiveWindow.back());

            ULONG64 calculatedMin = InvalidLongEstatsValue;
            ULONG64 calculatedMax = InvalidLongEstatsValue;
            for (const auto &value : curReceiveWindow)
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
                L"(bad)," :
                ctl::ctString::format_string(L"%lu,", calculatedMin);

            formattedString += (calculatedMax == InvalidLongEstatsValue) ?
                L"(bad)," :
                ctl::ctString::format_string(L"%lu", calculatedMax);

            return formattedString +
                   ctsWriteDetails::PrintMeanStdDev(curReceiveWindow);
        }
        std::unordered_map<std::wstring, std::vector<ULONG64> *> GetData()
        {
            return {
                {L"curLocalReceiveWindow", &curReceiveWindow},
                {L"minLocalReceiveWindow", &minReceiveWindow},
                {L"maxLocalReceiveWindow", &maxReceiveWindow}
            };
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_REC_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetPerConnectionEstats<TcpConnectionEstatsRec>(tcpRow, &Rw);
        }

        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr)
        {
            TCP_ESTATS_REC_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsRec>(tcpRow, &Rod)) {

#ifdef _TESTING_ESTATS_VALUES
                if (Rod.CurRwinSent == InvalidLongEstatsValue ||
                    Rod.MinRwinSent == InvalidLongEstatsValue ||
                    Rod.MaxRwinSent == InvalidLongEstatsValue ||
                    (Rod.MinRwinSent != InvalidLongEstatsValue && Rod.MinRwinSent > Rod.MaxRwinSent && Rod.MaxRwinSent > 0))
                {
                    WCHAR local_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)localAddr.writeCompleteAddress(local_address);
                    WCHAR remote_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)remoteAddr.writeCompleteAddress(remote_address);

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
                }
#endif
                UNREFERENCED_PARAMETER(localAddr);
                UNREFERENCED_PARAMETER(remoteAddr);

                if (IsRodValueValid(L"TcpConnectionEstatsRec - CurRwinSent", Rod.CurRwinSent)) {
                    curReceiveWindow.push_back(Rod.CurRwinSent);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsRec - MinRwinSent", Rod.MinRwinSent)) {
                    minReceiveWindow.push_back(Rod.MinRwinSent);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsRec - MaxRwinSent", Rod.MaxRwinSent)) {
                    maxReceiveWindow.push_back(Rod.MaxRwinSent);
                }
            }
        }

    private:
        std::vector<ULONG64> curReceiveWindow;

        std::vector<ULONG64> minReceiveWindow;
        ULONG64 minReceiveWindowCount = 0;
        std::vector<ULONG64> maxReceiveWindow;
        ULONG64 maxReceiveWindowCount = 0;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsObsRec> {
    public:
        static LPCWSTR PrintHeader() noexcept
        {
            return L"RemoteRecvWin(min),RemoteRecvWin(max),RemoteRecvWin(calculated-min),RemoteRecvWin(calculated-max),RemoteRecvWin(calculated-mean),RemoteRecvWin(calculated-stddev)";
        }
        std::wstring PrintData() const
        {
            std::wstring formattedString(L",");
            formattedString += ((!minReceiveWindow.empty()) && (minReceiveWindow.back() == InvalidLongEstatsValue)) ?                
                ctl::ctString::format_string(L"%lu,", minReceiveWindow.back())
                : L"(bad),";

            formattedString += ((!maxReceiveWindow.empty()) && (maxReceiveWindow.back() == InvalidLongEstatsValue)) ?                
                ctl::ctString::format_string(L"%lu,", maxReceiveWindow.back())
                : L"(bad),";

            ULONG64 calculatedMin = InvalidLongEstatsValue;
            ULONG64 calculatedMax = InvalidLongEstatsValue;
            for (const auto &value : curReceiveWindow)
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
                ctl::ctString::format_string(L"%lu,", minReceiveWindow.back())
                : L"(bad),";

            formattedString += (calculatedMax == InvalidLongEstatsValue) ?
                ctl::ctString::format_string(L"%lu,", maxReceiveWindow.back())
                : L"(bad),";

            return formattedString +
                   ctsWriteDetails::PrintMeanStdDev(curReceiveWindow);
        }
        std::unordered_map<std::wstring, std::vector<ULONG64> *> GetData()
        {
            return {
                {L"curRemoteReceiveWindow", &curReceiveWindow},
                {L"minRemoteReceiveWindow", &minReceiveWindow},
                {L"maxRemoteReceiveWindow", &maxReceiveWindow}
            };
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_OBS_REC_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetPerConnectionEstats<TcpConnectionEstatsObsRec>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr)
        {
            TCP_ESTATS_OBS_REC_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsObsRec>(tcpRow, &Rod)) {

#ifdef _TESTING_ESTATS_VALUES
                if (Rod.CurRwinRcvd == InvalidLongEstatsValue ||
                    Rod.MinRwinRcvd == InvalidLongEstatsValue ||
                    Rod.MaxRwinRcvd == InvalidLongEstatsValue ||
                    (Rod.MinRwinRcvd != InvalidLongEstatsValue && Rod.MinRwinRcvd != 0xffffffff && Rod.MinRwinRcvd > Rod.MaxRwinRcvd && Rod.MaxRwinRcvd > 0))
                {
                    WCHAR local_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)localAddr.writeCompleteAddress(local_address);
                    WCHAR remote_address[ctl::IP_STRING_MAX_LENGTH] = {};
                    (void)remoteAddr.writeCompleteAddress(remote_address);

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
#endif
                UNREFERENCED_PARAMETER(localAddr);
                UNREFERENCED_PARAMETER(remoteAddr);

                if (IsRodValueValid(L"TcpConnectionEstatsObsRec - CurRwinRcvd", Rod.CurRwinRcvd)) {
                    curReceiveWindow.push_back(Rod.CurRwinRcvd);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsObsRec - MinRwinRcvd", Rod.MinRwinRcvd)) {
                    minReceiveWindow.push_back(Rod.MinRwinRcvd);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsObsRec - MaxRwinRcvd", Rod.MaxRwinRcvd)) {
                    maxReceiveWindow.push_back(Rod.MaxRwinRcvd);
                }
            }
        }

    private:
        std::vector<ULONG64> curReceiveWindow;
        std::vector<ULONG64> minReceiveWindow;
        std::vector<ULONG64> maxReceiveWindow;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsBandwidth> {
    public:
        static LPCWSTR PrintHeader() noexcept
        {
            return L"OutboundBandwidth(mean),OutboundBandwidth(stddev),InboundBandwidth(mean),InboundBandwidth(stddev),OutboundInstability(mean),OutboundInstability(stddev),"
                   L"InboundInstability(mean),InboundInstability(stddev),OutboundBandwidthPeaked,InboundBandwidthPeaked";
        }
        std::wstring PrintData() const
        {
            return ctsWriteDetails::PrintMeanStdDev(outboundBandwidth) +
                   ctsWriteDetails::PrintMeanStdDev(inboundBandwidth) +
                   ctsWriteDetails::PrintMeanStdDev(outboundInstability) +
                   ctsWriteDetails::PrintMeanStdDev(inboundInstability) +
                   std::wstring((outboundBandwidthPeaked) ? L",yes" : L",no") +
                   std::wstring((inboundBandwidthPeaked) ? L",yes" : L",no");
        }
        std::unordered_map<std::wstring, std::vector<ULONG64>*> GetData()
        {
            return {
                {L"outboundBandwidth", &outboundBandwidth},
                {L"inboundBandwidth", &inboundBandwidth},
                {L"outboundInstability", &outboundInstability},
                {L"inboundInstability", &inboundInstability}
            };
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_BANDWIDTH_RW_v0 Rw;
            Rw.EnableCollectionInbound = TcpBoolOptEnabled;
            Rw.EnableCollectionOutbound = TcpBoolOptEnabled;
            SetPerConnectionEstats<TcpConnectionEstatsBandwidth>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr &, const ctl::ctSockaddr &)
        {
            TCP_ESTATS_BANDWIDTH_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetPerConnectionDynamicEstats(tcpRow, &Rod))
            {
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - OutboundBandwidth", Rod.OutboundBandwidth))
                {
                    outboundBandwidth.push_back(Rod.OutboundBandwidth);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - InboundBandwidth", Rod.InboundBandwidth))
                {
                    inboundBandwidth.push_back(Rod.InboundBandwidth);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - OutboundInstability", Rod.OutboundInstability))
                {
                    outboundInstability.push_back(Rod.OutboundInstability);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - InboundInstability", Rod.InboundInstability))
                {
                    inboundInstability.push_back(Rod.InboundInstability);
                }
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - OutboundBandwidthPeaked", Rod.OutboundBandwidthPeaked))
                {
                    outboundBandwidthPeaked = Rod.OutboundBandwidthPeaked;
                }
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - InboundBandwidthPeaked", Rod.InboundBandwidthPeaked))
                {
                    inboundBandwidthPeaked = Rod.InboundBandwidthPeaked;
                }
            }
        }

    private:
        std::vector<ULONG64> outboundBandwidth;
        std::vector<ULONG64> inboundBandwidth;
        std::vector<ULONG64> outboundInstability;
        std::vector<ULONG64> inboundInstability;
        BOOLEAN outboundBandwidthPeaked = false;
        BOOLEAN inboundBandwidthPeaked = false;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsFineRtt> {
    public:
        static LPCWSTR PrintHeader() noexcept
        {
            return L"";
        }
        std::wstring PrintData() const
        {
            return L"";
        }
        std::unordered_map<std::wstring, std::vector<ULONG64> *> GetData()
        {
            return {};
        }

        template <typename PTCPROW>
        void StartTracking(const PTCPROW tcpRow) const
        {
            TCP_ESTATS_FINE_RTT_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            SetPerConnectionEstats<TcpConnectionEstatsFineRtt>(tcpRow, &Rw);
        }
        template <typename PTCPROW>
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&)
        {
            TCP_ESTATS_FINE_RTT_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsFineRtt>(tcpRow, &Rod)) {
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
        std::unordered_map<std::wstring, std::vector<ULONG64>* > GetData() const
        {
            return data.GetData();
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

    ~ctsEstats() noexcept
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
                const details::EstatsDataPoint<TcpConnectionEstatsBandwidth> matchingbandwidthData(
                    entry.LocalAddr(),
                    entry.RemoteAddr());

                const auto foundEntry = byteTrackingData.find(matchingData);
                const auto foundBandwidthEntry = bandwidthData.find(matchingbandwidthData);
                if ((foundEntry != byteTrackingData.end()) || (foundBandwidthEntry != bandwidthData.end())) {
                    senderCongestionWriter.write_row(
                        entry.PrintAddresses() +
                        entry.PrintData() +
                        ((foundEntry != byteTrackingData.end()) ? foundEntry->PrintData() : L",,") + 
                        ((foundBandwidthEntry != bandwidthData.end()) ? foundBandwidthEntry->PrintData() : L",,,,,,,,,"));
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
                L"," + details::EstatsDataPoint<TcpConnectionEstatsData>::PrintHeader() +
                L"," + details::EstatsDataPoint<TcpConnectionEstatsBandwidth>::PrintHeader());

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
    std::set<details::EstatsDataPoint<TcpConnectionEstatsBandwidth>> bandwidthData;

    ctsWriteDetails pathInfoWriter;
    ctsWriteDetails receiveWindowWriter;
    ctsWriteDetails senderCongestionWriter;

    // Mapping of which type of data structure each stat is tracked in
    std::map<std::wstring, TCP_ESTATS_TYPE> trackedStatisticsDataTypes {
        {L"MssRcvd",                          TcpConnectionEstatsSynOpts},
        {L"MssSent",                          TcpConnectionEstatsSynOpts},
        {L"DataBytesIn",                      TcpConnectionEstatsData},
        {L"DataBytesOut",                     TcpConnectionEstatsData},
        {L"conjestionWindow",                 TcpConnectionEstatsSndCong},
        {L"bytesSentInReceiverLimited",       TcpConnectionEstatsSndCong},
        {L"bytesSentInSenderLimited",         TcpConnectionEstatsSndCong},
        {L"bytesSentInCongestionLimited",     TcpConnectionEstatsSndCong},
        {L"transitionsIntoReceiverLimited",   TcpConnectionEstatsSndCong},
        {L"transitionsIntoSenderLimited",     TcpConnectionEstatsSndCong},
        {L"transitionsIntoCongestionLimited", TcpConnectionEstatsSndCong},
        {L"retransmitTimer",                  TcpConnectionEstatsPath},
        {L"roundTripTime",                    TcpConnectionEstatsPath},
        {L"bytesRetrans",                     TcpConnectionEstatsPath},
        {L"dupAcksRcvd",                      TcpConnectionEstatsPath},
        {L"sacksRcvd",                        TcpConnectionEstatsPath},
        {L"congestionSignals",                TcpConnectionEstatsPath},
        {L"maxSegmentSize",                   TcpConnectionEstatsPath},
        {L"curLocalReceiveWindow",            TcpConnectionEstatsRec},
        {L"minLocalReceiveWindow",            TcpConnectionEstatsRec},
        {L"maxLocalReceiveWindow",            TcpConnectionEstatsRec},
        {L"curRemoteReceiveWindow",           TcpConnectionEstatsObsRec},
        {L"minRemoteReceiveWindow",           TcpConnectionEstatsObsRec},
        {L"maxRemoteReceiveWindow",           TcpConnectionEstatsObsRec},
        {L"outboundBandwidth",                TcpConnectionEstatsBandwidth},
        {L"inboundBandwidth",                 TcpConnectionEstatsBandwidth},
        {L"outboundInstability",              TcpConnectionEstatsBandwidth},
        {L"inboundInstability",               TcpConnectionEstatsBandwidth}
    };

    enum TRACKING_TYPE {
        UNTRACKED,
        BASIC,
        DETAIL
    };

    // Map of which stats are enabled
    std::map<std::wstring, TRACKING_TYPE> liveTrackedStatistics {
        {L"MssRcvd",                          BASIC},
        {L"MssSent",                          BASIC},
        {L"DataBytesIn",                      UNTRACKED},
        {L"DataBytesOut",                     UNTRACKED},
        {L"conjestionWindow",                 BASIC},
        {L"bytesSentInReceiverLimited",       UNTRACKED},
        {L"bytesSentInSenderLimited",         UNTRACKED},
        {L"bytesSentInCongestionLimited",     UNTRACKED},
        {L"transitionsIntoReceiverLimited",   UNTRACKED},
        {L"transitionsIntoSenderLimited",     UNTRACKED},
        {L"transitionsIntoCongestionLimited", UNTRACKED},
        {L"retransmitTimer",                  BASIC},
        {L"roundTripTime",                    DETAIL},
        {L"bytesRetrans",                     BASIC},
        {L"dupAcksRcvd",                      UNTRACKED},
        {L"sacksRcvd",                        UNTRACKED},
        {L"congestionSignals",                UNTRACKED},
        {L"maxSegmentSize",                   UNTRACKED},
        {L"curLocalReceiveWindow",            UNTRACKED},
        {L"minLocalReceiveWindow",            UNTRACKED},
        {L"maxLocalReceiveWindow",            UNTRACKED},
        {L"curRemoteReceiveWindow",           UNTRACKED},
        {L"minRemoteReceiveWindow",           UNTRACKED},
        {L"maxRemoteReceiveWindow",           UNTRACKED},
        {L"outboundBandwidth",                BASIC},
        {L"inboundBandwidth",                 BASIC},
        {L"outboundInstability",              BASIC},
        {L"inboundInstability",               BASIC}
    };

    // since updates are always serialized on a timer, just reuse the same buffer
    static const ULONG StartingTableSize = 4096;
    std::vector<char> tcpTable;
    ULONG tableCounter = 0;

    // Statistics summary
    typedef struct detailedStats {
        size_t  samples = 0;
        ULONG64 min = ULONG_MAX;
        ULONG64 max = ULONG_MAX;
        DOUBLE mean = -0.00001;
        DOUBLE stddev = -0.00001;
        DOUBLE median = -0.00001;
        DOUBLE iqr = -0.00001;
    } DETAILED_STATS;
    // Representation of the %change of each statistic since the last poll
    typedef struct detailedStatsChange {
        DOUBLE samples = 1.0;
        DOUBLE min = 0.0;
        DOUBLE max = 0.0;
        DOUBLE mean = 0.0;
        DOUBLE stddev = 0.0;
        DOUBLE median = 0.0;
        DOUBLE iqr = 0.0;
    } DETAILED_STATS_PERCENT_CHANGE;

    std::map<std::tuple<std::wstring, ctl::ctSockaddr, ctl::ctSockaddr>, DETAILED_STATS> previousPerConnectionStatsSummaries;
    std::map<std::wstring, DETAILED_STATS> previousGlobalStatsSummaries;

    template<typename T>
    DOUBLE PercentChange(T oldVal, T newVal) {
        if (newVal > oldVal) {
            return (static_cast<DOUBLE>(newVal - oldVal) / oldVal);
        }
        else if (newVal < oldVal) {
            return -1 * (static_cast<DOUBLE>(oldVal - newVal) / oldVal);
        }
        else {
            return 0.0;
        }
    }

    // Generate a DETAILED_STATS struct for the given statistic across all connections.
    //      Min/Max: global min/max, Mean: mean of means, Median: median of medians,
    //      StdDev: stddev of means, IQR: iqr of medians.
    std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE> GatherGlobalStatisticSummary(std::wstring statName, TCP_ESTATS_TYPE TcpType)
    {
        // Handle different data structure types
        // This sucks
        switch (TcpType)
        {
            case TcpConnectionEstatsSynOpts:
                return _GatherGlobalStatisticSummary(statName, synOptsData);
            case TcpConnectionEstatsData:
                return _GatherGlobalStatisticSummary(statName, byteTrackingData);
            case TcpConnectionEstatsPath:
                return _GatherGlobalStatisticSummary(statName, pathInfoData);
            case TcpConnectionEstatsRec:
                return _GatherGlobalStatisticSummary(statName, localReceiveWindowData);
            case TcpConnectionEstatsObsRec:
                return _GatherGlobalStatisticSummary(statName, remoteReceiveWindowData);
            case TcpConnectionEstatsSndCong:
                return _GatherGlobalStatisticSummary(statName, senderCongestionData);
            case TcpConnectionEstatsBandwidth:
                return _GatherGlobalStatisticSummary(statName, bandwidthData);
            default: // Never occurs bc this is an enum
                return {};
        }
    }
    // Actual function, wrapper handles differing datastructure types
    template <TCP_ESTATS_TYPE TcpType>
    std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE> _GatherGlobalStatisticSummary(std::wstring statName, std::set<details::EstatsDataPoint<TcpType>> &dataStructure)
    {
        std::vector<ULONG64> mins;
        std::vector<ULONG64> maxs;
        std::vector<DOUBLE> means;
        std::vector<DOUBLE> medians;

        for (const auto &entry : dataStructure)
        {
            std::vector<ULONG64> values = *(entry.GetData().at(statName));
            if (values.empty()) {continue;} // Ignore empty entries

            sort(std::begin(values), std::end(values));

            mins.push_back(*std::min_element(std::begin(values), std::end(values)));
            maxs.push_back(*std::max_element(std::begin(values), std::end(values)));
            means.push_back(std::get<0>(ctl::ctSampledStandardDeviation(std::begin(values), std::end(values))));
            medians.push_back(std::get<1>(ctl::ctInterquartileRange(std::begin(values), std::end(values))));
        }

        // If no data was collected, return an empty struct
        if (mins.empty()) {return {};}

        auto mstddev_tuple = ctl::ctSampledStandardDeviation(std::begin(means), std::end(means));
        auto interquartile_tuple = ctl::ctInterquartileRange(std::begin(medians), std::end(medians));

        // Build summary struct
        DETAILED_STATS s = {
            std::size(mins),
            *std::min_element(std::begin(mins), std::end(mins)),
            *std::max_element(std::begin(maxs), std::end(maxs)),
            std::get<0>(mstddev_tuple),
            std::get<1>(mstddev_tuple),
            std::get<1>(interquartile_tuple),
            std::get<2>(interquartile_tuple) - std::get<0>(interquartile_tuple)
        };

        // Build a struct marking %change of each value
        // Handle case where no previous summary exists
        DETAILED_STATS_PERCENT_CHANGE c;
        try {
            DETAILED_STATS s_prev = previousGlobalStatsSummaries.at(statName);
            c = {
                PercentChange(s.samples, s_prev.samples),
                PercentChange(s.min, s_prev.min),
                PercentChange(s.max, s_prev.max),
                PercentChange(s.mean, s_prev.mean),
                PercentChange(s.stddev, s_prev.stddev),
                PercentChange(s.median, s_prev.median),
                PercentChange(s.iqr, s_prev.iqr)
            };
        }
        catch (std::out_of_range&) {
            c = {};
        }

        // Update previous tracked with this new summary
        previousGlobalStatsSummaries.insert_or_assign(statName, s);

        return std::make_tuple(s, c);
    }


    // Generate a vector of DETAILED_STATS structs representing a summaries for the given statistic for each connection.
    std::vector<std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE>> GatherPerConnectionStatisticSummaries(std::wstring statName, TCP_ESTATS_TYPE TcpType)
    {
        // Handle different data structure types
        // This sucks
        switch (TcpType)
        {
            case TcpConnectionEstatsSynOpts:
                return _GatherPerConnectionStatisticSummaries(statName, synOptsData);
            case TcpConnectionEstatsData:
                return _GatherPerConnectionStatisticSummaries(statName, byteTrackingData);
            case TcpConnectionEstatsPath:
                return _GatherPerConnectionStatisticSummaries(statName, pathInfoData);
            case TcpConnectionEstatsRec:
                return _GatherPerConnectionStatisticSummaries(statName, localReceiveWindowData);
            case TcpConnectionEstatsObsRec:
                return _GatherPerConnectionStatisticSummaries(statName, remoteReceiveWindowData);
            case TcpConnectionEstatsSndCong:
                return _GatherPerConnectionStatisticSummaries(statName, senderCongestionData);
            case TcpConnectionEstatsBandwidth:
                return _GatherPerConnectionStatisticSummaries(statName, bandwidthData);
            default: // Never occurs bc this is an enum
                return {};
        }
    }
    // Actual function, wrapper handles differing datastructure types
    template <TCP_ESTATS_TYPE TcpType>
    std::vector<std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE>> _GatherPerConnectionStatisticSummaries(std::wstring statName, std::set<details::EstatsDataPoint<TcpType>> &dataStructure)
    {
        std::vector<std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE>> perConnectionSatisticSummaries;

        for (const auto &entry : dataStructure)
        {
            std::vector<ULONG64> values = *(entry.GetData().at(statName));
            if (values.empty()) {continue;} // Ignore empty entries

            sort(std::begin(values), std::end(values));
            auto mstddev_tuple = ctl::ctSampledStandardDeviation(std::begin(values), std::end(values));
            auto interquartile_tuple = ctl::ctInterquartileRange(std::begin(values), std::end(values));

            DETAILED_STATS s = {
                std::size(values),
                *std::min_element(std::begin(values), std::end(values)),
                *std::max_element(std::begin(values), std::end(values)),
                std::get<0>(mstddev_tuple),
                std::get<1>(mstddev_tuple),
                std::get<1>(interquartile_tuple),
                std::get<2>(interquartile_tuple) - std::get<0>(interquartile_tuple)
            };

            // Tuple to identify this entry in the previous summaries tracking structure
            auto perConnectionStatIDTuple = std::make_tuple(
                statName,
                entry.LocalAddr(),
                entry.RemoteAddr()
            );

            // Build a struct marking %change of each value
            // Handle case where no previous summary exists
            DETAILED_STATS_PERCENT_CHANGE c;
            try {
                DETAILED_STATS s_prev = previousPerConnectionStatsSummaries.at(perConnectionStatIDTuple);
                c = {
                    PercentChange(s.samples, s_prev.samples),
                    PercentChange(s.min, s_prev.min),
                    PercentChange(s.max, s_prev.max),
                    PercentChange(s.mean, s_prev.mean),
                    PercentChange(s.stddev, s_prev.stddev),
                    PercentChange(s.median, s_prev.median),
                    PercentChange(s.iqr, s_prev.iqr)
                };
            }
            catch (std::out_of_range&) {
                c = {};
            }

            previousPerConnectionStatsSummaries.insert_or_assign(perConnectionStatIDTuple, s);

            perConnectionSatisticSummaries.push_back(std::make_tuple(s, c));
        }

        return perConnectionSatisticSummaries;
    }

    void ResetSetConsoleColor() {
        auto hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
    void SetConsoleColorConnectionStatus(BOOLEAN connectionOpen) {
        auto hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (connectionOpen) {
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE);
        }
        else {
            ResetSetConsoleColor();
        }
    }
    void SetConsoleColorFromPercentChange(DOUBLE percentChange) {
        auto hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if(percentChange < -1.0) { // Blue BG
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_BLUE);
        }
        if(percentChange < -0.25) { // Blue
            SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        }
        if(percentChange < -0.01) { // Cyan
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        }
        if(percentChange < 0.0) { // Green
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);
        }
        if (percentChange == 0.0) { // White -- "No Change"
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }
        if (percentChange < 0.01) { // Yellow
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
        }
        else if (percentChange < 0.25) { // Magenta
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        }
        else if (percentChange < 1.0) { // Red
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        }
        else { // Red BG
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_RED);
        }
    }
    void PrintStat(ULONG64 stat, DOUBLE percentChange, const int& width) {
        ResetSetConsoleColor();
        std::wcout << L" | ";

        if (stat > 0.0) {SetConsoleColorFromPercentChange(percentChange);}
        std::wcout << std::right << std::setw(width) << std::setfill(L' ') << stat;
    }
    void PrintStat(DOUBLE stat, DOUBLE percentChange, const int& width) {
        ResetSetConsoleColor();
        std::wcout << L" | ";

        if (stat > 0.0) {SetConsoleColorFromPercentChange(percentChange);}
        std::wcout.precision(3);
        std::wcout << std::right << std::setw(width) << std::setfill(L' ') << std::fixed << stat;
    }
    void PrintGlobalStatSummary(std::wstring title, std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE> summary, const int& width) {
        std::wcout << std::left << std::setw(20) << std::setfill(L' ') << title;
            PrintStat(std::get<0>(summary).min, std::get<1>(summary).min, width);
            PrintStat(std::get<0>(summary).mean, std::get<1>(summary).mean, width);
            PrintStat(std::get<0>(summary).max, std::get<1>(summary).max, width);
            PrintStat(std::get<0>(summary).stddev, std::get<1>(summary).stddev, width);
            PrintStat(std::get<0>(summary).median, std::get<1>(summary).median, width);
            PrintStat(std::get<0>(summary).iqr, std::get<1>(summary).iqr, width);
        ResetSetConsoleColor();
        std::wcout << L" |" << std::endl;
    }
    void PrintPerConnectionStatSummary(std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE> summary, const int& width) {
    SetConsoleColorConnectionStatus(std::get<0>(summary).samples > std::get<1>(summary).samples);
    std::wcout << std::left << std::setw(20) << std::setfill(L' ') << L"Samples: " << std::get<0>(summary).samples;
        PrintStat(std::get<0>(summary).min, std::get<1>(summary).min, width);
        PrintStat(std::get<0>(summary).mean, std::get<1>(summary).mean, width);
        PrintStat(std::get<0>(summary).max, std::get<1>(summary).max, width);
        PrintStat(std::get<0>(summary).stddev, std::get<1>(summary).stddev, width);
        PrintStat(std::get<0>(summary).median, std::get<1>(summary).median, width);
        PrintStat(std::get<0>(summary).iqr, std::get<1>(summary).iqr, width);
    ResetSetConsoleColor();
    std::wcout << L" |" << std::endl;
    }
    
    void PrintHeaderTitle(std::wstring title, const int& width) {
        std::wcout << L" | " << std::right << std::setw(width) << std::setfill(L' ') << title;
    }
    void PrintStdHeader(std::wstring firstColumnName, const int& width) {
        std::wcout << "---------------------------------------------------------------------------------------------------------------+" << std::endl;
        std::wcout << std::left << std::setw(20) << std::setfill(L' ') << firstColumnName;
            PrintHeaderTitle(L"Min", width);
            PrintHeaderTitle(L"Mean", width);
            PrintHeaderTitle(L"Max", width);
            PrintHeaderTitle(L"StdDev", width);
            PrintHeaderTitle(L"Median", width);
            PrintHeaderTitle(L"IQR", width);
        std::wcout << L" |" << std::endl;
        std::wcout << "---------------------------------------------------------------------------------------------------------------+" << std::endl;
    }
    void PrintStdFooter() {
        std::wcout << "---------------------------------------------------------------------------------------------------------------+" << std::endl;
    }

    void clear_screen(char fill = ' ') { 
        COORD tl = {0,0};
        CONSOLE_SCREEN_BUFFER_INFO s;
        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);   
        GetConsoleScreenBufferInfo(console, &s);
        DWORD written, cells = s.dwSize.X * s.dwSize.Y;
        FillConsoleOutputCharacter(console, fill, cells, tl, &written);
        FillConsoleOutputAttribute(console, s.wAttributes, cells, tl, &written);
        SetConsoleCursorPosition(console, tl);
    }

    void PrintDataUpdate() {
        // -- Global summary table --
        clear_screen();
        PrintStdHeader(L"GLobal Statistics", 12);
        for (auto stat : liveTrackedStatistics)
        {
            // Ignore utracked stats
            if (stat.second == UNTRACKED) {continue;}

            auto detailedStatsSummary = GatherGlobalStatisticSummary(stat.first, trackedStatisticsDataTypes.at(stat.first));
            PrintGlobalStatSummary(stat.first, detailedStatsSummary, 12);
        }
        PrintStdFooter();
        std::wcout << std::endl;

        // -- Detailed Pre-Statistic Results --
        for (auto stat : liveTrackedStatistics)
        {
            // Only print Detail-level tracked stats in the detailed format
            if (stat.second != DETAIL) {continue;}

            PrintStdHeader(stat.first, 12);

            auto detailedStatsSummaries = GatherPerConnectionStatisticSummaries(stat.first, trackedStatisticsDataTypes.at(stat.first));

            for (auto summary : detailedStatsSummaries)
            {
                PrintPerConnectionStatSummary(summary, 12);
            }
            PrintStdFooter();
        }
    }

    bool UpdateEstats() noexcept
    try
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
                    UpdateDataPoints(bandwidthData, tableEntry);
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
                    UpdateDataPoints(bandwidthData, tableEntry);
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

        // Print to console
        PrintDataUpdate();

        if (!accessDenied) {
            // schedule timer from this moment
            timer.schedule_singleton([this]() { UpdateEstats(); }, 1000);
        }

        return !accessDenied;
    }
    catch (const std::exception& e) {
        wprintf(L"ctsEstats::UpdateEstats exception: %ws\n", ctl::ctString::format_exception(e).c_str());
        return false;
    }

    void RefreshIPv4Data()
    {
        tcpTable.resize(tcpTable.capacity());
        auto table_size = static_cast<DWORD>(tcpTable.size());
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
        auto table_size = static_cast<DWORD>(tcpTable.size());
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

    template <TCP_ESTATS_TYPE TcpType, typename Mibtype>
    void UpdateDataPoints(std::set<details::EstatsDataPoint<TcpType>>& data, Mibtype tableEntry)
    {
        const auto emplaceResults = data.emplace(tableEntry);
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
