/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// c headers
#include <wchar.h>

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
#include <wlanapi.h>

// ctl headers
#include <ctString.hpp>
#include <ctSockaddr.hpp>
#include <ctThreadPoolTimer.hpp>
#include <ctMath.hpp>


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

    // Get L2 WLAN data
    ULONG GetWLANInformation(PWLAN_CONNECTION_ATTRIBUTES &connectionAttributes, PWLAN_STATISTICS &statistics, PWLAN_BSS_ENTRY &bssEntry, HANDLE hClient)
    {
        // Reused return code var
        DWORD dwResult = 0;

        // Enumerate WLAN interfaces to find the currently connected one, if there is one
        PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
        dwResult = WlanEnumInterfaces(hClient, NULL, &pIfList);
        if (dwResult != ERROR_SUCCESS) {
            wprintf(L"WlanEnumInterfaces failed with error: %u\n", dwResult);
            return dwResult;
            // You can use FormatMessage here to find out why the function failed
        }

        // Find the currecntly connected interface
        PWLAN_INTERFACE_INFO pIfInfo = NULL;
        BOOLEAN connectedInterfaceFound = false;
        for (UINT i = 0; i < static_cast<UINT>(pIfList->dwNumberOfItems); i++) {
            pIfInfo = (WLAN_INTERFACE_INFO *) & pIfList->InterfaceInfo[i];

            // Get the GUID for this interface
            WCHAR GuidString[39] = {0};
            INT stringFromGUIDRes = StringFromGUID2(pIfInfo->InterfaceGuid, (LPOLESTR) & GuidString, sizeof(GuidString) / sizeof(*GuidString));

            if (pIfInfo->isState == wlan_interface_state_connected) {
                if (stringFromGUIDRes == 0) {
                    wprintf(L"ERROR: Connected Interface found, but could not convert GUID.\n");
                    return 1;
                }
                connectedInterfaceFound = true;
                break;
            }
        }

        // If a connected interface wasn't found, return error
        if (!connectedInterfaceFound) {
            wprintf(L"No connected Interface Found.\n");
            return 1;
        }

        // If a connected interface was found,
        // call WlanQueryInterface and WlanGetNetworkBssList to get current connection attributes and statistics

        // Get WLAN_CONNECTION_ATTRIBUTES structure
        DWORD connectInfoSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
        dwResult = WlanQueryInterface(
            hClient,
            &pIfInfo->InterfaceGuid,
            wlan_intf_opcode_current_connection,
            NULL,
            &connectInfoSize,
            (PVOID *) &connectionAttributes, 
            NULL
        );
        if (dwResult != ERROR_SUCCESS) {
            wprintf(L"WlanQueryInterface [connectionAttributes] failed with error: %u\n", dwResult);
            return 1;
        }

        // Get WLAN_STATISTICS structure
        DWORD statisticsSize = sizeof(WLAN_STATISTICS);
        dwResult = WlanQueryInterface(
            hClient,
            &pIfInfo->InterfaceGuid,
            wlan_intf_opcode_statistics,
            NULL,
            &statisticsSize,
            (PVOID *) &statistics, 
            NULL
        );
        if (dwResult != ERROR_SUCCESS) {
            wprintf(L"WlanQueryInterface [statistics] failed with error: %u\n", dwResult);
            return 1;
        }

        // Get the available BSS list for the connected interface
        PWLAN_BSS_LIST pBssList = NULL;
        dwResult = WlanGetNetworkBssList(
            hClient,
            &pIfInfo->InterfaceGuid,
            &connectionAttributes->wlanAssociationAttributes.dot11Ssid,
            connectionAttributes->wlanAssociationAttributes.dot11BssType,
            connectionAttributes->wlanSecurityAttributes.bSecurityEnabled,
            NULL,
            &pBssList
        );
        if (dwResult != ERROR_SUCCESS) {
            wprintf(L"WlanGetNetworkBssList failed with error: %ld\n", dwResult);
            return 1;
        }

        // Find currently connected BSS entry
        // Get WLAN_BSS_ENTRY structure
        BOOLEAN connectedBSSFound = false;
        for (UINT i = 0; i < pBssList->dwNumberOfItems; i++) {
            bssEntry = (WLAN_BSS_ENTRY *) & pBssList->wlanBssEntries[i];
            
            if (std::strncmp(reinterpret_cast<PCHAR>(connectionAttributes->wlanAssociationAttributes.dot11Bssid), 
                             reinterpret_cast<PCHAR>(bssEntry->dot11Bssid), 
                             6) == 0) {
                connectedBSSFound = true;
            }
        }

        return 0;
    }


    // Helper for handling stat arrays which collect stats in a per-time-slice fashion, 
    // but pull from values which represent total counts
    template <typename T>
    inline void updateTotalCountBasedStatVector(std::vector<T> &vector, T &srcCount, T &trackedCount, const ULONG &maxLength) {
        vector.push_back(srcCount - trackedCount);
        // Enforce the max length of data history vector
        if (vector.size() > maxLength) {
            vector.erase(std::begin(vector));
        }
        trackedCount = srcCount;
    }
    // Helper for handling stat arrays
    template <typename T>
    inline void updateStatVector(std::vector<T> &vector, T &srcValue, const ULONG &maxLength) {
        vector.push_back(srcValue);
        // Enforce the max length of data history vector
        if (vector.size() > maxLength) {
            vector.erase(std::begin(vector));
        }
    }

    inline ULONGLONG sumULONGLONGVect(std::vector<ULONGLONG> &vect) {
        return std::accumulate(std::begin(vect), std::end(vect), 0ULL);
    }

    // Class to track and poll WLAN Statistics
    class WLANDataTracking {
    public:
    /**
        std::wstring GetSSID() {
            if (dot11Ssid == NULL) {
                return L"";
            }
            // Handle conversions from UCHAR[] to std::wstring
            std::wstringstream wssSSID;
            for (UINT i = 0; i < dot11Ssid->uSSIDLength; i++) {
                wssSSID << dot11Ssid->ucSSID[i];
            }
            return std::wstring(wssSSID.str());
        }
        std::wstring GetBSSID() {
            if (dot11Bssid == NULL) {
                return L"";
            }
            // Handle conversions from UCHAR[] to std::wstring
            std::wstringstream wssBSSID;
            for (UINT i = 0; i < 6; i++) {
                if(i == 5) {
                    wssBSSID << std::hex << *dot11Bssid[i];
                }
                else {
                    wssBSSID << std::hex << *dot11Bssid[i] << L"-";
                }
            }
            return std::wstring(wssBSSID.str());
        }
    **/
        std::vector<LONG> * GetRSSIData() {
            return &Rssi;
        }
        std::unordered_map<std::wstring, std::vector<ULONG> *> GetULONGNumericalData() {
            return {
                {L"wlanSignalQuality", &wlanSignalQuality},
                {L"RxRate", &RxRate},
                {L"TxRate", &TxRate},
                {L"LinkQuality", &LinkQuality},
                {L"ChCenterFrequency", &ChCenterFrequency}
            };
        }
        std::unordered_map<std::wstring, std::vector<ULONGLONG> *> GetULONGLONGNumericalData()
        {
            return {
                {L"FourWayHandshakeFailures", &FourWayHandshakeFailures},
                {L"TKIPCounterMeasuresInvoked", &TKIPCounterMeasuresInvoked},
                {L"UC_TransmittedFrames", &UC_TransmittedFrames},
                {L"UC_ReceivedFrames", &UC_ReceivedFrames},
                {L"UC_WEPExcludedFrames", &UC_WEPExcludedFrames},
                {L"UC_TKIPLocalMICFailures", &UC_TKIPLocalMICFailures},
                {L"UC_TKIPReplays", &UC_TKIPReplays},
                {L"UC_TKIPICVErrors", &UC_TKIPICVErrors},
                {L"UC_CCMPReplays", &UC_CCMPReplays},
                {L"UC_CCMPDecryptErrors", &UC_CCMPDecryptErrors},
                {L"UC_WEPUndecryptablePackets", &UC_WEPUndecryptablePackets},
                {L"UC_WEPICVErrors", &UC_WEPICVErrors},
                {L"UC_DecryptSuccesses", &UC_DecryptSuccesses},
                {L"UC_DecryptFailures", &UC_DecryptFailures},
                {L"MC_TransmittedFrames", &MC_TransmittedFrames},
                {L"MC_ReceivedFrames", &MC_ReceivedFrames},
                {L"MC_WEPExcludedFrames", &MC_WEPExcludedFrames},
                {L"MC_TKIPLocalMICFailures", &MC_TKIPLocalMICFailures},
                {L"MC_TKIPReplays", &MC_TKIPReplays},
                {L"MC_TKIPICVErrors", &MC_TKIPICVErrors},
                {L"MC_CCMPReplays", &MC_CCMPReplays},
                {L"MC_CCMPDecryptErrors", &MC_CCMPDecryptErrors},
                {L"MC_WEPUndecryptablePackets", &MC_WEPUndecryptablePackets},
                {L"MC_WEPICVErrors", &MC_WEPICVErrors},
                {L"MC_DecryptSuccesses", &MC_DecryptSuccesses},
                {L"MC_DecryptFailures", &MC_DecryptFailures},
                {L"TransmittedFrames", &TransmittedFrames},
                {L"MulticastTransmittedFrames", &MulticastTransmittedFrames},
                {L"FailedFrameTransmissions", &FailedFrameTransmissions},
                {L"RetriedFrameTransmissions", &RetriedFrameTransmissions},
                {L"MultipleRetriedFrameTransmissions", &MultipleRetriedFrameTransmissions},
                {L"MaxTXLifetimeExceededFrames", &MaxTXLifetimeExceededFrames},
                {L"TransmittedFragments", &TransmittedFragments},
                {L"RTSSuccesses", &RTSSuccesses},
                {L"RTSFailures", &RTSFailures},
                {L"ACKFailures", &ACKFailures},
                {L"ReceivedFrames", &ReceivedFrames},
                {L"MulticastReceivedFrames", &MulticastReceivedFrames},
                {L"PromiscuousReceivedFrames", &PromiscuousReceivedFrames},
                {L"MaxRXLifetimeExceededFrames", &MaxRXLifetimeExceededFrames},
                {L"FrameDuplicates", &FrameDuplicates},
                {L"ReceivedFragments", &ReceivedFragments},
                {L"PromiscuousReceivedFragments", &PromiscuousReceivedFragments},
                {L"FCSErrors", &FCSErrors}
            };
        }

        static std::wstring PrintHeader() {
            return L"Statistic,Sum,SampleCount,Min,Max,-1Std,Mean,+1Std,-1IQR,Median,+1IQR";
        }
        void WriteConnectionInfoData(ctsWriteDetails &writer) {
            writer.write_row(L"Signal Quality," + ctsPerf::ctsWriteDetails::PrintDetails(wlanSignalQuality));
            writer.write_row(L"RX Rate," + ctsPerf::ctsWriteDetails::PrintDetails(RxRate));
            writer.write_row(L"TX Rate," + ctsPerf::ctsWriteDetails::PrintDetails(TxRate));
            writer.write_row(L"RSSI," + ctsPerf::ctsWriteDetails::PrintDetails(Rssi));
            writer.write_row(L"Link Quality," + ctsPerf::ctsWriteDetails::PrintDetails(LinkQuality));
            writer.write_row(L"Four-Way Handshake Failures," + std::to_wstring(sumULONGLONGVect(FourWayHandshakeFailures)) + ctsPerf::ctsWriteDetails::PrintDetails(FourWayHandshakeFailures));
            writer.write_row(L"TKIP Countermeasures Invoked," + std::to_wstring(sumULONGLONGVect(TKIPCounterMeasuresInvoked)) + ctsPerf::ctsWriteDetails::PrintDetails(TKIPCounterMeasuresInvoked));
        }
        void WriteMACStatsData(ctsWriteDetails &writer) {
            writer.write_row(L"[U] Transmitted Frames," + std::to_wstring(sumULONGLONGVect(UC_TransmittedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_TransmittedFrames));
            writer.write_row(L"[U] Received Frames," + std::to_wstring(sumULONGLONGVect(UC_ReceivedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_ReceivedFrames));
            writer.write_row(L"[U] WEP Excluded Frames," + std::to_wstring(sumULONGLONGVect(UC_WEPExcludedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_WEPExcludedFrames));
            writer.write_row(L"[U] TKIP LocalMIC Failures," + std::to_wstring(sumULONGLONGVect(UC_TKIPLocalMICFailures)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_TKIPLocalMICFailures));
            writer.write_row(L"[U] TKIP Replays," + std::to_wstring(sumULONGLONGVect(UC_TKIPReplays)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_TKIPReplays));
            writer.write_row(L"[U] TKIP ICV Errors," + std::to_wstring(sumULONGLONGVect(UC_TKIPICVErrors)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_TKIPICVErrors));
            writer.write_row(L"[U] CCMP Replays," + std::to_wstring(sumULONGLONGVect(UC_CCMPReplays)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_CCMPReplays));
            writer.write_row(L"[U] CCMP Decrypt Errors," + std::to_wstring(sumULONGLONGVect(UC_CCMPDecryptErrors)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_CCMPDecryptErrors));
            writer.write_row(L"[U] WEP Undecryptable Packets," + std::to_wstring(sumULONGLONGVect(UC_WEPUndecryptablePackets)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_WEPUndecryptablePackets));
            writer.write_row(L"[U] WEP ICV Errors," + std::to_wstring(sumULONGLONGVect(UC_WEPICVErrors)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_WEPICVErrors));
            writer.write_row(L"[U] Decrypt Successes," + std::to_wstring(sumULONGLONGVect(UC_DecryptSuccesses)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_DecryptSuccesses));
            writer.write_row(L"[U] Decrypt Failures," + std::to_wstring(sumULONGLONGVect(UC_DecryptFailures)) + ctsPerf::ctsWriteDetails::PrintDetails(UC_DecryptFailures));
            writer.write_empty_row();
            writer.write_row(L"[M] Transmitted Frames," + std::to_wstring(sumULONGLONGVect(MC_TransmittedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_TransmittedFrames));
            writer.write_row(L"[M] Received Frames," + std::to_wstring(sumULONGLONGVect(MC_ReceivedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_ReceivedFrames));
            writer.write_row(L"[M] WEP Excluded Frames," + std::to_wstring(sumULONGLONGVect(MC_WEPExcludedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_WEPExcludedFrames));
            writer.write_row(L"[M] TKIP LocalMIC Failures," + std::to_wstring(sumULONGLONGVect(MC_TKIPLocalMICFailures)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_TKIPLocalMICFailures));
            writer.write_row(L"[M] TKIP Replays," + std::to_wstring(sumULONGLONGVect(MC_TKIPReplays)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_TKIPReplays));
            writer.write_row(L"[M] TKIP ICV Errors," + std::to_wstring(sumULONGLONGVect(MC_TKIPICVErrors)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_TKIPICVErrors));
            writer.write_row(L"[M] CCMP Replays," + std::to_wstring(sumULONGLONGVect(MC_CCMPReplays)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_CCMPReplays));
            writer.write_row(L"[M] CCMP Decrypt Errors," + std::to_wstring(sumULONGLONGVect(MC_CCMPDecryptErrors)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_CCMPDecryptErrors));
            writer.write_row(L"[M] WEP Undecryptable Packets," + std::to_wstring(sumULONGLONGVect(MC_WEPUndecryptablePackets)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_WEPUndecryptablePackets));
            writer.write_row(L"[M] WEP ICV Errors," + std::to_wstring(sumULONGLONGVect(MC_WEPICVErrors)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_WEPICVErrors));
            writer.write_row(L"[M] Decrypt Successes," + std::to_wstring(sumULONGLONGVect(MC_DecryptSuccesses)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_DecryptSuccesses));
            writer.write_row(L"[M] Decrypt Failures," + std::to_wstring(sumULONGLONGVect(MC_DecryptFailures)) + ctsPerf::ctsWriteDetails::PrintDetails(MC_DecryptFailures));
        }
        void WritePhyStatsData(ctsWriteDetails &writer) {
            writer.write_row(L"Transmitted Frames," + std::to_wstring(sumULONGLONGVect(TransmittedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(TransmittedFrames));
            writer.write_row(L"Multicast Transmitted Frames," + std::to_wstring(sumULONGLONGVect(MulticastTransmittedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(MulticastTransmittedFrames));
            writer.write_row(L"Failed Frame Transmissions," + std::to_wstring(sumULONGLONGVect(FailedFrameTransmissions)) + ctsPerf::ctsWriteDetails::PrintDetails(FailedFrameTransmissions));
            writer.write_row(L"Retried Frame Transmissions," + std::to_wstring(sumULONGLONGVect(RetriedFrameTransmissions)) + ctsPerf::ctsWriteDetails::PrintDetails(RetriedFrameTransmissions));
            writer.write_row(L"Multiple Retried Frame Transmissions," + std::to_wstring(sumULONGLONGVect(MultipleRetriedFrameTransmissions)) + ctsPerf::ctsWriteDetails::PrintDetails(MultipleRetriedFrameTransmissions));
            writer.write_row(L"Max TX Lifetime Exceeded Frames," + std::to_wstring(sumULONGLONGVect(MaxTXLifetimeExceededFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(MaxTXLifetimeExceededFrames));
            writer.write_row(L"Transmitted Fragments," + std::to_wstring(sumULONGLONGVect(TransmittedFragments)) + ctsPerf::ctsWriteDetails::PrintDetails(TransmittedFragments));
            writer.write_row(L"RTS Successes," + std::to_wstring(sumULONGLONGVect(RTSSuccesses)) + ctsPerf::ctsWriteDetails::PrintDetails(RTSSuccesses));
            writer.write_row(L"RTS Failures," + std::to_wstring(sumULONGLONGVect(RTSFailures)) + ctsPerf::ctsWriteDetails::PrintDetails(RTSFailures));
            writer.write_row(L"ACK Failures," + std::to_wstring(sumULONGLONGVect(ACKFailures)) + ctsPerf::ctsWriteDetails::PrintDetails(ACKFailures));
            writer.write_row(L"Received Frames," + std::to_wstring(sumULONGLONGVect(ReceivedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(ReceivedFrames));
            writer.write_row(L"Multicast Received Frames," + std::to_wstring(sumULONGLONGVect(MulticastReceivedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(MulticastReceivedFrames));
            writer.write_row(L"Promiscuous Received Frames," + std::to_wstring(sumULONGLONGVect(PromiscuousReceivedFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(PromiscuousReceivedFrames));
            writer.write_row(L"Max RX Lifetime Exceeded Frames," + std::to_wstring(sumULONGLONGVect(MaxRXLifetimeExceededFrames)) + ctsPerf::ctsWriteDetails::PrintDetails(MaxRXLifetimeExceededFrames));
            writer.write_row(L"Frame Duplicates," + std::to_wstring(sumULONGLONGVect(FrameDuplicates)) + ctsPerf::ctsWriteDetails::PrintDetails(FrameDuplicates));
            writer.write_row(L"Received Fragments," + std::to_wstring(sumULONGLONGVect(ReceivedFragments)) + ctsPerf::ctsWriteDetails::PrintDetails(ReceivedFragments));
            writer.write_row(L"Promiscuous Received Fragments," + std::to_wstring(sumULONGLONGVect(PromiscuousReceivedFragments)) + ctsPerf::ctsWriteDetails::PrintDetails(PromiscuousReceivedFragments));
            writer.write_row(L"FCS Errors," + std::to_wstring(sumULONGLONGVect(FCSErrors)) + ctsPerf::ctsWriteDetails::PrintDetails(FCSErrors));
        }


        void UpdateData(ULONG tableCounter, const ULONG maxHistoryLength, HANDLE hClient)
        {
            PWLAN_CONNECTION_ATTRIBUTES pconnectionAttributes = NULL;
            PWLAN_STATISTICS pstatistics = NULL;
            PWLAN_BSS_ENTRY pbssEntry = NULL;
            //FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetWLANInformation(pconnectionAttributes, pstatistics, pbssEntry, hClient)) {

                WLAN_CONNECTION_ATTRIBUTES connectionAttributes = *pconnectionAttributes;
                WLAN_STATISTICS statistics = *pstatistics;
                WLAN_BSS_ENTRY bssEntry = *pbssEntry;
                // State-based Statistics

                // WLAN_ASSOCIATION_ATTRIBUTES
                updateStatVector(wlanSignalQuality, connectionAttributes.wlanAssociationAttributes.wlanSignalQuality, maxHistoryLength);
                updateStatVector(RxRate,            connectionAttributes.wlanAssociationAttributes.ulRxRate, maxHistoryLength);
                updateStatVector(TxRate,            connectionAttributes.wlanAssociationAttributes.ulTxRate, maxHistoryLength);

                // WLAN_BSS
                updateStatVector(Rssi,              bssEntry.lRssi, maxHistoryLength);
                updateStatVector(LinkQuality,       bssEntry.uLinkQuality, maxHistoryLength);
                updateStatVector(ChCenterFrequency, bssEntry.ulChCenterFrequency, maxHistoryLength);

                // Sum-based statistics
                if (initializedCounts) { // If total counts are initialized (previous counts populated by an earlier poll), update the stats
                    // WLAN_STATISTICS
                    updateTotalCountBasedStatVector(FourWayHandshakeFailures,   statistics.ullFourWayHandshakeFailures, FourWayHandshakeFailuresCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(TKIPCounterMeasuresInvoked, statistics.ullTKIPCounterMeasuresInvoked, TKIPCounterMeasuresInvokedCount, maxHistoryLength);

                    // WLAN_MAC_FRAME_STATISTICS (Unicast)
                    updateTotalCountBasedStatVector(UC_TransmittedFrames,       statistics.MacUcastCounters.ullTransmittedFrameCount, UC_TransmittedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_ReceivedFrames,          statistics.MacUcastCounters.ullReceivedFrameCount, UC_ReceivedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_WEPExcludedFrames,       statistics.MacUcastCounters.ullWEPExcludedCount, UC_WEPExcludedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_TKIPLocalMICFailures,    statistics.MacUcastCounters.ullTKIPLocalMICFailures, UC_TKIPLocalMICFailuresCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_TKIPReplays,             statistics.MacUcastCounters.ullTKIPReplays, UC_TKIPReplaysCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_TKIPICVErrors,           statistics.MacUcastCounters.ullTKIPICVErrorCount, UC_TKIPICVErrorsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_CCMPReplays,             statistics.MacUcastCounters.ullCCMPReplays, UC_CCMPReplaysCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_CCMPDecryptErrors,       statistics.MacUcastCounters.ullCCMPDecryptErrors, UC_CCMPDecryptErrorsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_WEPUndecryptablePackets, statistics.MacUcastCounters.ullWEPUndecryptableCount, UC_WEPUndecryptablePacketsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_WEPICVErrors,            statistics.MacUcastCounters.ullWEPICVErrorCount, UC_WEPICVErrorsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_DecryptSuccesses,        statistics.MacUcastCounters.ullDecryptSuccessCount, UC_DecryptSuccessesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(UC_DecryptFailures,         statistics.MacUcastCounters.ullDecryptFailureCount, UC_DecryptFailuresCount, maxHistoryLength);
                    // WLAN_MAC_FRAME_STATISTICS (Multicast)
                    updateTotalCountBasedStatVector(MC_TransmittedFrames,       statistics.MacUcastCounters.ullTransmittedFrameCount, MC_TransmittedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_ReceivedFrames,          statistics.MacUcastCounters.ullReceivedFrameCount, MC_ReceivedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_WEPExcludedFrames,       statistics.MacUcastCounters.ullWEPExcludedCount, MC_WEPExcludedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_TKIPLocalMICFailures,    statistics.MacUcastCounters.ullTKIPLocalMICFailures, MC_TKIPLocalMICFailuresCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_TKIPReplays,             statistics.MacUcastCounters.ullTKIPReplays, MC_TKIPReplaysCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_TKIPICVErrors,           statistics.MacUcastCounters.ullTKIPICVErrorCount, MC_TKIPICVErrorsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_CCMPReplays,             statistics.MacUcastCounters.ullCCMPReplays, MC_CCMPReplaysCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_CCMPDecryptErrors,       statistics.MacUcastCounters.ullCCMPDecryptErrors, MC_CCMPDecryptErrorsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_WEPUndecryptablePackets, statistics.MacUcastCounters.ullWEPUndecryptableCount, MC_WEPUndecryptablePacketsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_WEPICVErrors,            statistics.MacUcastCounters.ullWEPICVErrorCount, MC_WEPICVErrorsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_DecryptSuccesses,        statistics.MacUcastCounters.ullDecryptSuccessCount, MC_DecryptSuccessesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MC_DecryptFailures,         statistics.MacUcastCounters.ullDecryptFailureCount, MC_DecryptFailuresCount, maxHistoryLength);

                    // WLAN_PHY_FRAME_STATISTICS
                    updateTotalCountBasedStatVector(TransmittedFrames,                 statistics.PhyCounters[0].ullTransmittedFrameCount, TransmittedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MulticastTransmittedFrames,        statistics.PhyCounters[0].ullMulticastTransmittedFrameCount, MulticastTransmittedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(FailedFrameTransmissions,          statistics.PhyCounters[0].ullFailedCount, FailedFrameTransmissionsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(RetriedFrameTransmissions,         statistics.PhyCounters[0].ullRetryCount, RetriedFrameTransmissionsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MultipleRetriedFrameTransmissions, statistics.PhyCounters[0].ullMultipleRetryCount, MultipleRetriedFrameTransmissionsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MaxTXLifetimeExceededFrames,       statistics.PhyCounters[0].ullMaxTXLifetimeExceededCount, MaxTXLifetimeExceededFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(TransmittedFragments,              statistics.PhyCounters[0].ullTransmittedFragmentCount, TransmittedFragmentsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(RTSSuccesses,                      statistics.PhyCounters[0].ullRTSSuccessCount, RTSSuccessesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(RTSFailures,                       statistics.PhyCounters[0].ullRTSFailureCount, RTSFailuresCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(ACKFailures,                       statistics.PhyCounters[0].ullACKFailureCount, ACKFailuresCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(ReceivedFrames,                    statistics.PhyCounters[0].ullReceivedFrameCount, ReceivedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MulticastReceivedFrames,           statistics.PhyCounters[0].ullMulticastReceivedFrameCount, MulticastReceivedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(PromiscuousReceivedFrames,         statistics.PhyCounters[0].ullPromiscuousReceivedFrameCount, PromiscuousReceivedFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(MaxRXLifetimeExceededFrames,       statistics.PhyCounters[0].ullMaxRXLifetimeExceededCount, MaxRXLifetimeExceededFramesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(FrameDuplicates,                   statistics.PhyCounters[0].ullFrameDuplicateCount, FrameDuplicatesCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(ReceivedFragments,                 statistics.PhyCounters[0].ullReceivedFragmentCount, ReceivedFragmentsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(PromiscuousReceivedFragments,      statistics.PhyCounters[0].ullPromiscuousReceivedFragmentCount, PromiscuousReceivedFragmentsCount, maxHistoryLength);
                    updateTotalCountBasedStatVector(FCSErrors,                         statistics.PhyCounters[0].ullFCSErrorCount, FCSErrorsCount, maxHistoryLength);
                }
                else { // Just Update the inital total counts for sum-based stats

                    // WLAN_STATISTICS
                    FourWayHandshakeFailuresCount          = statistics.ullFourWayHandshakeFailures;
                    TKIPCounterMeasuresInvokedCount        = statistics.ullTKIPCounterMeasuresInvoked;
                    // WLAN_MAC_FRAME_STATISTICS (Unicast)                
                    UC_TransmittedFramesCount              = statistics.MacUcastCounters.ullTransmittedFrameCount;
                    UC_ReceivedFramesCount                 = statistics.MacUcastCounters.ullReceivedFrameCount;
                    UC_WEPExcludedFramesCount              = statistics.MacUcastCounters.ullWEPExcludedCount;
                    UC_TKIPLocalMICFailuresCount           = statistics.MacUcastCounters.ullTKIPLocalMICFailures;
                    UC_TKIPReplaysCount                    = statistics.MacUcastCounters.ullTKIPReplays;
                    UC_TKIPICVErrorsCount                  = statistics.MacUcastCounters.ullTKIPICVErrorCount;
                    UC_CCMPReplaysCount                    = statistics.MacUcastCounters.ullCCMPReplays;
                    UC_CCMPDecryptErrorsCount              = statistics.MacUcastCounters.ullCCMPDecryptErrors;
                    UC_WEPUndecryptablePacketsCount        = statistics.MacUcastCounters.ullWEPUndecryptableCount;
                    UC_WEPICVErrorsCount                   = statistics.MacUcastCounters.ullWEPICVErrorCount;
                    UC_DecryptSuccessesCount               = statistics.MacUcastCounters.ullDecryptSuccessCount;
                    UC_DecryptFailuresCount                = statistics.MacUcastCounters.ullDecryptFailureCount;
                    // WLAN_MAC_FRAME_STATISTICS (Multicast)
                    MC_TransmittedFramesCount              = statistics.MacUcastCounters.ullTransmittedFrameCount;
                    MC_ReceivedFramesCount                 = statistics.MacUcastCounters.ullReceivedFrameCount;
                    MC_WEPExcludedFramesCount              = statistics.MacUcastCounters.ullWEPExcludedCount;
                    MC_TKIPLocalMICFailuresCount           = statistics.MacUcastCounters.ullTKIPLocalMICFailures;
                    MC_TKIPReplaysCount                    = statistics.MacUcastCounters.ullTKIPReplays;
                    MC_TKIPICVErrorsCount                  = statistics.MacUcastCounters.ullTKIPICVErrorCount;
                    MC_CCMPReplaysCount                    = statistics.MacUcastCounters.ullCCMPReplays;
                    MC_CCMPDecryptErrorsCount              = statistics.MacUcastCounters.ullCCMPDecryptErrors;
                    MC_WEPUndecryptablePacketsCount        = statistics.MacUcastCounters.ullWEPUndecryptableCount;
                    MC_WEPICVErrorsCount                   = statistics.MacUcastCounters.ullWEPICVErrorCount;
                    MC_DecryptSuccessesCount               = statistics.MacUcastCounters.ullDecryptSuccessCount;
                    MC_DecryptFailuresCount                = statistics.MacUcastCounters.ullDecryptFailureCount;
                    // WLAN_PHY_FRAME_STATISTICS                
                    TransmittedFramesCount                 = statistics.PhyCounters[0].ullTransmittedFrameCount;
                    MulticastTransmittedFramesCount        = statistics.PhyCounters[0].ullMulticastTransmittedFrameCount;
                    FailedFrameTransmissionsCount          = statistics.PhyCounters[0].ullFailedCount;
                    RetriedFrameTransmissionsCount         = statistics.PhyCounters[0].ullRetryCount;
                    MultipleRetriedFrameTransmissionsCount = statistics.PhyCounters[0].ullMultipleRetryCount;
                    MaxTXLifetimeExceededFramesCount       = statistics.PhyCounters[0].ullMaxTXLifetimeExceededCount;
                    TransmittedFragmentsCount              = statistics.PhyCounters[0].ullTransmittedFragmentCount;
                    RTSSuccessesCount                      = statistics.PhyCounters[0].ullRTSSuccessCount;
                    RTSFailuresCount                       = statistics.PhyCounters[0].ullRTSFailureCount;
                    ACKFailuresCount                       = statistics.PhyCounters[0].ullACKFailureCount;
                    ReceivedFramesCount                    = statistics.PhyCounters[0].ullReceivedFrameCount;
                    MulticastReceivedFramesCount           = statistics.PhyCounters[0].ullMulticastReceivedFrameCount;
                    PromiscuousReceivedFramesCount         = statistics.PhyCounters[0].ullPromiscuousReceivedFrameCount;
                    MaxRXLifetimeExceededFramesCount       = statistics.PhyCounters[0].ullMaxRXLifetimeExceededCount;
                    FrameDuplicatesCount                   = statistics.PhyCounters[0].ullFrameDuplicateCount;
                    ReceivedFragmentsCount                 = statistics.PhyCounters[0].ullReceivedFragmentCount;
                    PromiscuousReceivedFragmentsCount      = statistics.PhyCounters[0].ullPromiscuousReceivedFragmentCount;
                    FCSErrorsCount                         = statistics.PhyCounters[0].ullFCSErrorCount;

                    // Set counts as initialized
                    initializedCounts = true;
                }

                latestCounter = tableCounter;
                // Free memory used to store WLAN data structres
                WlanFreeMemory(pconnectionAttributes);
                WlanFreeMemory(pstatistics);
                //WlanFreeMemory(pbssEntry); // This crashes for some reason?
            }
            else {
                wprintf(L"Could not get WLAN information this poll, WiFi may not be connected.\n");
                // Free memory used to store WLAN data structres                
                WlanFreeMemory(pconnectionAttributes);
                WlanFreeMemory(pstatistics);
                //WlanFreeMemory(pbssEntry); // This crashes for some reason?
            }
        }

        ULONG LatestCounter() {
            return latestCounter;
        }

    private:
        mutable ULONG latestCounter = 0;

        // WLAN_ASSOCIATION_ATTRIBUTES
        //PDOT11_SSID dot11Ssid;
        //DOT11_BSS_TYPE dot11BssType;
        //PDOT11_MAC_ADDRESS dot11Bssid;
        std::vector<WLAN_SIGNAL_QUALITY> wlanSignalQuality;
        std::vector<ULONG> RxRate;
        std::vector<ULONG> TxRate;

        // WLAN_BSS
        //std::vector<DOT11_SSID> dot11Ssid;
        //std::vector<ULONG> uPhyId;
        //std::vector<DOT11_MAC_ADDRESS> dot11Bssid;
        //std::vector<DOT11_BSS_TYPE> dot11BssType;
        //std::vector<DOT11_PHY_TYPE> dot11BssPhyType;
        std::vector<LONG> Rssi;
        std::vector<ULONG> LinkQuality;
        //std::vector<BOOLEAN> bInRegDomain;
        //std::vector<USHORT> usBeaconPeriod;
        //std::vector<ULONGLONG> ullTimestamp;
        //std::vector<ULONGLONG> ullHostTimestamp;
        //std::vector<USHORT> usCapabilityInformation;
        std::vector<ULONG> ChCenterFrequency;
        //std::vector<ULONG> ulIeOffset;
        //std::vector<ULONG> ulIeSize;

        BOOLEAN initializedCounts = false;

        // WLAN_STATISTICS
        std::vector<ULONGLONG> FourWayHandshakeFailures;
        ULONGLONG FourWayHandshakeFailuresCount = 0;
        std::vector<ULONGLONG> TKIPCounterMeasuresInvoked;
        ULONGLONG TKIPCounterMeasuresInvokedCount = 0;

        // WLAN_MAC_FRAME_STATISTICS (Unicast)
        std::vector<ULONGLONG> UC_TransmittedFrames;
        ULONGLONG UC_TransmittedFramesCount = 0;
        std::vector<ULONGLONG> UC_ReceivedFrames;
        ULONGLONG UC_ReceivedFramesCount = 0;
        std::vector<ULONGLONG> UC_WEPExcludedFrames;
        ULONGLONG UC_WEPExcludedFramesCount = 0;
        std::vector<ULONGLONG> UC_TKIPLocalMICFailures;
        ULONGLONG UC_TKIPLocalMICFailuresCount = 0;
        std::vector<ULONGLONG> UC_TKIPReplays;
        ULONGLONG UC_TKIPReplaysCount = 0;
        std::vector<ULONGLONG> UC_TKIPICVErrors;
        ULONGLONG UC_TKIPICVErrorsCount = 0;
        std::vector<ULONGLONG> UC_CCMPReplays;
        ULONGLONG UC_CCMPReplaysCount = 0;
        std::vector<ULONGLONG> UC_CCMPDecryptErrors;
        ULONGLONG UC_CCMPDecryptErrorsCount = 0;
        std::vector<ULONGLONG> UC_WEPUndecryptablePackets;
        ULONGLONG UC_WEPUndecryptablePacketsCount = 0;
        std::vector<ULONGLONG> UC_WEPICVErrors;
        ULONGLONG UC_WEPICVErrorsCount = 0;
        std::vector<ULONGLONG> UC_DecryptSuccesses;
        ULONGLONG UC_DecryptSuccessesCount = 0;
        std::vector<ULONGLONG> UC_DecryptFailures;
        ULONGLONG UC_DecryptFailuresCount = 0;
        // WLAN_MAC_FRAME_STATISTICS (Multicast)
        std::vector<ULONGLONG> MC_TransmittedFrames;
        ULONGLONG MC_TransmittedFramesCount = 0;
        std::vector<ULONGLONG> MC_ReceivedFrames;
        ULONGLONG MC_ReceivedFramesCount = 0;
        std::vector<ULONGLONG> MC_WEPExcludedFrames;
        ULONGLONG MC_WEPExcludedFramesCount = 0;
        std::vector<ULONGLONG> MC_TKIPLocalMICFailures;
        ULONGLONG MC_TKIPLocalMICFailuresCount = 0;
        std::vector<ULONGLONG> MC_TKIPReplays;
        ULONGLONG MC_TKIPReplaysCount = 0;
        std::vector<ULONGLONG> MC_TKIPICVErrors;
        ULONGLONG MC_TKIPICVErrorsCount = 0;
        std::vector<ULONGLONG> MC_CCMPReplays;
        ULONGLONG MC_CCMPReplaysCount = 0;
        std::vector<ULONGLONG> MC_CCMPDecryptErrors;
        ULONGLONG MC_CCMPDecryptErrorsCount = 0;
        std::vector<ULONGLONG> MC_WEPUndecryptablePackets;
        ULONGLONG MC_WEPUndecryptablePacketsCount = 0;
        std::vector<ULONGLONG> MC_WEPICVErrors;
        ULONGLONG MC_WEPICVErrorsCount = 0;
        std::vector<ULONGLONG> MC_DecryptSuccesses;
        ULONGLONG MC_DecryptSuccessesCount = 0;
        std::vector<ULONGLONG> MC_DecryptFailures;
        ULONGLONG MC_DecryptFailuresCount = 0;

        // WLAN_PHY_FRAME_STATISTICS
        std::vector<ULONGLONG> TransmittedFrames;
        ULONGLONG TransmittedFramesCount = 0;
        std::vector<ULONGLONG> MulticastTransmittedFrames;
        ULONGLONG MulticastTransmittedFramesCount = 0;
        std::vector<ULONGLONG> FailedFrameTransmissions;
        ULONGLONG FailedFrameTransmissionsCount = 0;
        std::vector<ULONGLONG> RetriedFrameTransmissions;
        ULONGLONG RetriedFrameTransmissionsCount = 0;
        std::vector<ULONGLONG> MultipleRetriedFrameTransmissions;
        ULONGLONG MultipleRetriedFrameTransmissionsCount = 0;
        std::vector<ULONGLONG> MaxTXLifetimeExceededFrames;
        ULONGLONG MaxTXLifetimeExceededFramesCount = 0;
        std::vector<ULONGLONG> TransmittedFragments;
        ULONGLONG TransmittedFragmentsCount = 0;
        std::vector<ULONGLONG> RTSSuccesses;
        ULONGLONG RTSSuccessesCount = 0;
        std::vector<ULONGLONG> RTSFailures;
        ULONGLONG RTSFailuresCount = 0;
        std::vector<ULONGLONG> ACKFailures;
        ULONGLONG ACKFailuresCount = 0;
        std::vector<ULONGLONG> ReceivedFrames;
        ULONGLONG ReceivedFramesCount = 0;
        std::vector<ULONGLONG> MulticastReceivedFrames;
        ULONGLONG MulticastReceivedFramesCount = 0;
        std::vector<ULONGLONG> PromiscuousReceivedFrames;
        ULONGLONG PromiscuousReceivedFramesCount = 0;
        std::vector<ULONGLONG> MaxRXLifetimeExceededFrames;
        ULONGLONG MaxRXLifetimeExceededFramesCount = 0;
        std::vector<ULONGLONG> FrameDuplicates;
        ULONGLONG FrameDuplicatesCount = 0;
        std::vector<ULONGLONG> ReceivedFragments;
        ULONGLONG ReceivedFragmentsCount = 0;
        std::vector<ULONGLONG> PromiscuousReceivedFragments;
        ULONGLONG PromiscuousReceivedFragmentsCount = 0;
        std::vector<ULONGLONG> FCSErrors;
        ULONGLONG FCSErrorsCount = 0;
    };


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
            formattedString += (MssRcvd.empty()) ? L"," : L"," + std::to_wstring(MssRcvdCount);
            formattedString += (MssSent.empty()) ? L"," : L"," + std::to_wstring(MssSentCount);
            return formattedString;
        }
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
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&, const ULONG maxHistoryLength)
        {
            TCP_ESTATS_SYN_OPTS_ROS_v0 Ros;
            FillMemory(&Ros, sizeof Ros, -1);
            if (0 == GetPerConnectionStaticEstats(tcpRow, &Ros)) {

                if (IsRodValueValid(L"TcpConnectionEstatsSynOpts - MssRcvd", Ros.MssRcvd)) {
                    MssRcvd.push_back(Ros.MssRcvd - MssRcvdCount);
                    MssRcvdCount = Ros.MssRcvd;
                    // Enforce the max length of data history vector
                    if (MssRcvd.size() > maxHistoryLength) {
                        MssRcvd.erase(std::begin(MssRcvd));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSynOpts - MssSent", Ros.MssSent)) {
                    MssSent.push_back(Ros.MssSent - MssSentCount);
                    MssSentCount = Ros.MssSent;
                    // Enforce the max length of data history vector                        
                    if (MssSent.size() > maxHistoryLength) {
                        MssSent.erase(std::begin(MssSent));
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
            formattedString += (DataBytesIn.empty()) ? L"," : L"," + std::to_wstring(DataBytesInCount);
            formattedString += (DataBytesOut.empty()) ? L"," : L"," + std::to_wstring(DataBytesOutCount);
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
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&, const ULONG maxHistoryLength)
        {
            TCP_ESTATS_DATA_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsData>(tcpRow, &Rod)) {

                if (IsRodValueValid(L"TcpConnectionEstatsData - DataBytesIn", Rod.DataBytesIn)) {
                    DataBytesIn.push_back(Rod.DataBytesIn - DataBytesInCount);
                    DataBytesInCount = Rod.DataBytesIn;
                    // Enforce the max length of data history vector
                    if (DataBytesIn.size() > maxHistoryLength) {
                        DataBytesIn.erase(std::begin(DataBytesIn));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsData - DataBytesOut", Rod.DataBytesOut)) {
                    DataBytesOut.push_back(Rod.DataBytesOut - DataBytesOutCount);
                    DataBytesOutCount = Rod.DataBytesOut;
                    // Enforce the max length of data history vector
                    if (DataBytesOut.size() > maxHistoryLength) {
                        DataBytesOut.erase(std::begin(DataBytesOut));
                    }
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
            formattedString += (transitionsIntoReceiverLimited.empty()) ? L"," : L"," + std::to_wstring(transitionsIntoReceiverLimitedCount);
            formattedString += (transitionsIntoSenderLimited.empty()) ? L"," : L"," + std::to_wstring(transitionsIntoSenderLimitedCount);
            formattedString += (transitionsIntoCongestionLimited.empty()) ? L"," : L"," + std::to_wstring(transitionsIntoCongestionLimitedCount);
            formattedString += (bytesSentInReceiverLimited.empty()) ? L"," : L"," + std::to_wstring(bytesSentInReceiverLimitedCount);
            formattedString += (bytesSentInSenderLimited.empty()) ? L"," : L"," + std::to_wstring(bytesSentInSenderLimitedCount);
            formattedString += (bytesSentInCongestionLimited.empty()) ? L"," : L"," + std::to_wstring(bytesSentInCongestionLimitedCount);
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
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr, const ULONG maxHistoryLength)
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
                    // Enforce the max length of data history vector
                    if (conjestionWindow.size() > maxHistoryLength) {
                        conjestionWindow.erase(std::begin(conjestionWindow));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimBytesRwin", Rod.SndLimBytesRwin)) {
                    bytesSentInReceiverLimited.push_back(Rod.SndLimBytesRwin - bytesSentInReceiverLimitedCount);
                    bytesSentInReceiverLimitedCount = Rod.SndLimBytesRwin;
                    // Enforce the max length of data history vector
                    if (bytesSentInReceiverLimited.size() > maxHistoryLength) {
                        bytesSentInReceiverLimited.erase(std::begin(bytesSentInReceiverLimited));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimBytesSnd", Rod.SndLimBytesSnd)) {
                    bytesSentInSenderLimited.push_back(Rod.SndLimBytesSnd - bytesSentInSenderLimitedCount);
                    bytesSentInSenderLimitedCount = Rod.SndLimBytesSnd;
                    // Enforce the max length of data history vector
                    if (bytesSentInSenderLimited.size() > maxHistoryLength) {
                        bytesSentInSenderLimited.erase(std::begin(bytesSentInSenderLimited));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimBytesCwnd", Rod.SndLimBytesCwnd)) {
                    bytesSentInCongestionLimited.push_back(Rod.SndLimBytesCwnd - bytesSentInCongestionLimitedCount);
                    bytesSentInCongestionLimitedCount = Rod.SndLimBytesCwnd;
                    // Enforce the max length of data history vector
                    if (bytesSentInCongestionLimited.size() > maxHistoryLength) {
                        bytesSentInCongestionLimited.erase(std::begin(bytesSentInCongestionLimited));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimTransRwin", Rod.SndLimTransRwin)) {
                    transitionsIntoReceiverLimited.push_back(Rod.SndLimTransRwin - transitionsIntoReceiverLimitedCount);
                    transitionsIntoReceiverLimitedCount = Rod.SndLimTransRwin;
                    // Enforce the max length of data history vector
                    if (transitionsIntoReceiverLimited.size() > maxHistoryLength) {
                        transitionsIntoReceiverLimited.erase(std::begin(transitionsIntoReceiverLimited));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimTransSnd", Rod.SndLimTransSnd)) {
                    transitionsIntoSenderLimited.push_back(Rod.SndLimTransSnd - transitionsIntoSenderLimitedCount);
                    transitionsIntoSenderLimitedCount = Rod.SndLimTransSnd;
                    // Enforce the max length of data history vector
                    if (transitionsIntoSenderLimited.size() > maxHistoryLength) {
                        transitionsIntoSenderLimited.erase(std::begin(transitionsIntoSenderLimited));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsSndCong - SndLimTransCwnd", Rod.SndLimTransCwnd)) {
                    transitionsIntoCongestionLimited.push_back(Rod.SndLimTransCwnd - transitionsIntoCongestionLimitedCount);
                    transitionsIntoCongestionLimitedCount = Rod.SndLimTransCwnd;
                    // Enforce the max length of data history vector
                    if (transitionsIntoCongestionLimited.size() > maxHistoryLength) {
                        transitionsIntoCongestionLimited.erase(std::begin(transitionsIntoCongestionLimited));
                    }
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
            formattedString += (bytesRetrans.empty()) ? L"," : L"," + std::to_wstring(bytesRetransCount);
            formattedString += (dupAcksRcvd.empty()) ? L"," : L"," + std::to_wstring(dupAcksRcvdCount);
            formattedString += (sacksRcvd.empty()) ? L"," : L"," + std::to_wstring(sacksRcvdCount);
            formattedString += (congestionSignals.empty()) ? L"," : L"," + std::to_wstring(congestionSignalsCount);
            formattedString += (maxSegmentSize.empty()) ? L"," : L"," + std::to_wstring(maxSegmentSizeCount);
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
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr, const ULONG maxHistoryLength)
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
                    // Enforce the max length of data history vector
                    if (retransmitTimer.size() > maxHistoryLength) {
                        retransmitTimer.erase(std::begin(retransmitTimer));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - SmoothedRtt", Rod.SmoothedRtt)) {
                    roundTripTime.push_back(Rod.SmoothedRtt);
                    // Enforce the max length of data history vector
                    if (roundTripTime.size() > maxHistoryLength) {
                        roundTripTime.erase(std::begin(roundTripTime));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - BytesRetrans", Rod.BytesRetrans)) {
                    bytesRetrans.push_back(Rod.BytesRetrans - bytesRetransCount);
                    bytesRetransCount = Rod.BytesRetrans;
                    // Enforce the max length of data history vector
                    if (bytesRetrans.size() > maxHistoryLength) {
                        bytesRetrans.erase(std::begin(bytesRetrans));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - DupAcksIn", Rod.DupAcksIn)) {
                    dupAcksRcvd.push_back(Rod.DupAcksIn - dupAcksRcvdCount);
                    dupAcksRcvdCount = Rod.DupAcksIn;
                    // Enforce the max length of data history vector
                    if (dupAcksRcvd.size() > maxHistoryLength) {
                        dupAcksRcvd.erase(std::begin(dupAcksRcvd));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - SacksRcvd", Rod.SacksRcvd)) {
                    sacksRcvd.push_back(Rod.SacksRcvd - sacksRcvdCount);
                    sacksRcvdCount = Rod.SacksRcvd;
                    // Enforce the max length of data history vector
                    if (sacksRcvd.size() > maxHistoryLength) {
                        sacksRcvd.erase(std::begin(sacksRcvd));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - CongSignals", Rod.CongSignals)) {
                    congestionSignals.push_back(Rod.CongSignals - congestionSignalsCount);
                    congestionSignalsCount = Rod.CongSignals;
                    // Enforce the max length of data history vector
                    if (congestionSignals.size() > maxHistoryLength) {
                        congestionSignals.erase(std::begin(congestionSignals));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsPath - CurMss", Rod.CurMss)) {
                    maxSegmentSize.push_back(Rod.CurMss - maxSegmentSizeCount);
                    maxSegmentSizeCount = Rod.CurMss;
                    // Enforce the max length of data history vector
                    if (maxSegmentSize.size() > maxHistoryLength) {
                        maxSegmentSize.erase(std::begin(maxSegmentSize));
                    }
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
            formattedString += ((minReceiveWindow.empty()) || (minReceiveWindow.back() == InvalidLongEstatsValue)) ?
                L"(bad)," :
                ctl::ctString::format_string(L"%lu,", minReceiveWindow.back());

            formattedString += ((maxReceiveWindow.empty()) || (maxReceiveWindow.back() == InvalidLongEstatsValue)) ?
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
                L"(bad)" :
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
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr, const ULONG maxHistoryLength)
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
                    // Enforce the max length of data history vector
                    if (curReceiveWindow.size() > maxHistoryLength) {
                        curReceiveWindow.erase(std::begin(curReceiveWindow));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsRec - MinRwinSent", Rod.MinRwinSent)) {
                    minReceiveWindow.push_back(Rod.MinRwinSent);
                    // Enforce the max length of data history vector
                    if (minReceiveWindow.size() > maxHistoryLength) {
                        minReceiveWindow.erase(std::begin(minReceiveWindow));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsRec - MaxRwinSent", Rod.MaxRwinSent)) {
                    maxReceiveWindow.push_back(Rod.MaxRwinSent);
                    // Enforce the max length of data history vector
                    if (maxReceiveWindow.size() > maxHistoryLength) {
                        maxReceiveWindow.erase(std::begin(maxReceiveWindow));
                    }
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
            formattedString += ((minReceiveWindow.empty()) || (minReceiveWindow.back() == InvalidLongEstatsValue)) ?
                L"(bad)," :
                ctl::ctString::format_string(L"%lu,", minReceiveWindow.back());

            formattedString += ((maxReceiveWindow.empty()) || (maxReceiveWindow.back() == InvalidLongEstatsValue)) ?
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
                }

                if (calculatedMax == InvalidLongEstatsValue) {
                    calculatedMax = value;
                } else if (value > calculatedMax) {
                    calculatedMax = value;
                }
            }

            formattedString += (calculatedMin == InvalidLongEstatsValue) ?
                L"(bad)," :
                ctl::ctString::format_string(L"%lu,", calculatedMin);

            formattedString += (calculatedMax == InvalidLongEstatsValue) ?
                L"(bad)" :
                ctl::ctString::format_string(L"%lu", calculatedMax);

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
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr& localAddr, const ctl::ctSockaddr& remoteAddr, const ULONG maxHistoryLength)
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
                    // Enforce the max length of data history vector
                    if (curReceiveWindow.size() > maxHistoryLength) {
                        curReceiveWindow.erase(std::begin(curReceiveWindow));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsObsRec - MinRwinRcvd", Rod.MinRwinRcvd)) {
                    minReceiveWindow.push_back(Rod.MinRwinRcvd);
                    // Enforce the max length of data history vector
                    if (minReceiveWindow.size() > maxHistoryLength) {
                        minReceiveWindow.erase(std::begin(minReceiveWindow));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsObsRec - MaxRwinRcvd", Rod.MaxRwinRcvd)) {
                    maxReceiveWindow.push_back(Rod.MaxRwinRcvd);
                    // Enforce the max length of data history vector
                    if (maxReceiveWindow.size() > maxHistoryLength) {
                        maxReceiveWindow.erase(std::begin(maxReceiveWindow));
                    }
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
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr &, const ctl::ctSockaddr &, const ULONG maxHistoryLength)
        {
            TCP_ESTATS_BANDWIDTH_ROD_v0 Rod;
            FillMemory(&Rod, sizeof Rod, -1);
            if (0 == GetPerConnectionDynamicEstats(tcpRow, &Rod))
            {
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - OutboundBandwidth", Rod.OutboundBandwidth))
                {
                    outboundBandwidth.push_back(Rod.OutboundBandwidth);
                    // Enforce the max length of data history vector
                    if (outboundBandwidth.size() > maxHistoryLength) {
                        outboundBandwidth.erase(std::begin(outboundBandwidth));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - InboundBandwidth", Rod.InboundBandwidth))
                {
                    inboundBandwidth.push_back(Rod.InboundBandwidth);
                    // Enforce the max length of data history vector
                    if (inboundBandwidth.size() > maxHistoryLength) {
                        inboundBandwidth.erase(std::begin(inboundBandwidth));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - OutboundInstability", Rod.OutboundInstability))
                {
                    outboundInstability.push_back(Rod.OutboundInstability);
                    // Enforce the max length of data history vector
                    if (outboundInstability.size() > maxHistoryLength) {
                        outboundInstability.erase(std::begin(outboundInstability));
                    }
                }
                if (IsRodValueValid(L"TcpConnectionEstatsBandwidth - InboundInstability", Rod.InboundInstability))
                {
                    inboundInstability.push_back(Rod.InboundInstability);
                    // Enforce the max length of data history vector
                    if (inboundInstability.size() > maxHistoryLength) {
                        inboundInstability.erase(std::begin(inboundInstability));
                    }
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
        void UpdateData(const PTCPROW tcpRow, const ctl::ctSockaddr&, const ctl::ctSockaddr&, const ULONG maxHistoryLength)
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
            maxHistoryLength(0),
            localAddr(std::move(local_addr)),
            remoteAddr(std::move(remote_addr))
        {
        }

        explicit EstatsDataPoint(const PMIB_TCPROW pTcpRow, const ULONG maxHistoryLength) noexcept :  // NOLINT
            maxHistoryLength(maxHistoryLength),
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

        explicit EstatsDataPoint(const PMIB_TCP6ROW pTcpRow, const ULONG maxHistoryLength) noexcept :  // NOLINT
            maxHistoryLength(maxHistoryLength),
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
            data.UpdateData(tcpRow, localAddr, remoteAddr, maxHistoryLength);
        }

        ctl::ctSockaddr LocalAddr() const noexcept
        {
            return localAddr;
        }
        ctl::ctSockaddr RemoteAddr() const noexcept
        {
            return remoteAddr;
        }

        ULONG LatestCounter() const noexcept
        {
            return latestCounter;
        }

    private:
        const ULONG maxHistoryLength;
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
    ctsEstats(
        std::wstring dataDirectory,
        ULONG pollRateMS,
        ULONG maxHistoryLength,
        BOOLEAN trackWlanStats,
        std::set<std::wstring> *globalTrackedStats,
        std::set<std::wstring> *wlanTrackedStats,
        std::set<std::wstring> *detailTrackedStats,
        BOOLEAN livePrintGlobalStats,
        BOOLEAN livePrintWlanStats,
        BOOLEAN livePrintDetailStats)
        : dataDirectory(dataDirectory),
          pollRateMS(pollRateMS),
          maxHistoryLength(maxHistoryLength),
          trackWlanStats(trackWlanStats),
          globalTrackedStats(globalTrackedStats),
          wlanTrackedStats(wlanTrackedStats),
          detailTrackedStats(detailTrackedStats),
          printGlobal(livePrintGlobalStats),
          printWlan(livePrintWlanStats),
          printDetail(livePrintDetailStats),
          pathInfoWriter(std::wstring(dataDirectory + L"EstatsPathInfo.csv").c_str()),
          receiveWindowWriter(std::wstring(dataDirectory + L"EstatsReceiveWindow.csv").c_str()),
          senderCongestionWriter(std::wstring(dataDirectory + L"EstatsSenderCongestion.csv").c_str()),
          wlanConnectionInfoWriter(std::wstring(dataDirectory + L"WlanConnectionInfo.csv").c_str()),
          wlanMACStatsWriter(std::wstring(dataDirectory + L"WlanMACStats.csv").c_str()),
          wlanPhyStatsWriter(std::wstring(dataDirectory + L"WlanPhyStats.csv").c_str()),
          globalStatsWriter(std::wstring(dataDirectory + L"LiveData\\GlobalSummary_0.csv").c_str()),
          perConnectionStatsWriter(std::wstring(dataDirectory + L"LiveData\\DetailSummary_0.csv").c_str()),
          tcpTable(StartingTableSize)
    {}

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

            if(trackWlanStats) {
                wlanData.WriteConnectionInfoData(wlanConnectionInfoWriter);
                wlanData.WriteMACStatsData(wlanMACStatsWriter);
                wlanData.WritePhyStatsData(wlanPhyStatsWriter);
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

            if(trackWlanStats) {
                wlanConnectionInfoWriter.create_file(details::WLANDataTracking::PrintHeader());
                wlanMACStatsWriter.create_file(details::WLANDataTracking::PrintHeader());
                wlanPhyStatsWriter.create_file(details::WLANDataTracking::PrintHeader());

                // Initialize client handle
                DWORD dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
                if (dwResult != ERROR_SUCCESS)
                {
                    wprintf(L"WlanOpenHandle failed with error: %u\n", dwResult);
                    return dwResult;
                    // You can use FormatMessage here to find out why the function failed
                }
            }

            started = UpdateEstats();
        }
        catch (const std::exception& e) {
            wprintf(L"ctsEstats::Start exception: %ws\n", ctl::ctString::format_exception(e).c_str());
            started = false;
        }

        if (!started) {
            timer.stop_all_timers();
        }

        // Set up console formatting
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbiInfo = {0};
        GetConsoleScreenBufferInfo(hConsole, &csbiInfo);
        orig_wAttributes = csbiInfo.wAttributes;

        return started;
    }

private:
    ctl::ctThreadpoolTimer timer;

    // TCP Estats Tracking
    std::set<details::EstatsDataPoint<TcpConnectionEstatsSynOpts>> synOptsData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsData>> byteTrackingData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsPath>> pathInfoData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsRec>> localReceiveWindowData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsObsRec>> remoteReceiveWindowData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsSndCong>> senderCongestionData;
    std::set<details::EstatsDataPoint<TcpConnectionEstatsBandwidth>> bandwidthData;
    // WLAN Tracking
    details::WLANDataTracking wlanData;

    // Directory to save files in
    const std::wstring dataDirectory;

    // Old-style full-run-scope .csv writers
    // Estats
    ctsWriteDetails pathInfoWriter;
    ctsWriteDetails receiveWindowWriter;
    ctsWriteDetails senderCongestionWriter;
    // WLAN
    ctsWriteDetails wlanConnectionInfoWriter;
    ctsWriteDetails wlanMACStatsWriter;
    ctsWriteDetails wlanPhyStatsWriter;

    // WLAN-Specific
    const BOOLEAN trackWlanStats;
    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;

    // "Live" (per-poll) .csv writers
    ctsWriteDetails globalStatsWriter;
    ctsWriteDetails perConnectionStatsWriter;
    // Counters for live csv filenames
    ULONG globalFileNumber = 0;
    ULONG detailFileNumber = 0;


    // since updates are always serialized on a timer, just reuse the same buffer
    static const ULONG StartingTableSize = 4096;
    std::vector<char> tcpTable;
    ULONG tableCounter = 0;

    // Frequency (in milliseconds) of polling
    const ULONG pollRateMS;

    // Console output formatting
    HANDLE hConsole;
    WORD orig_wAttributes;
    const WORD BACKGROUND_MASK = 0x00F0;
    const USHORT dataColumnWidth = 12;
    const USHORT titleColumnWidth = 35;

    // Max number of values to keep in history
    const ULONG maxHistoryLength;

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


    // Lists of enabled live-tracked stats
    std::set<std::wstring>* globalTrackedStats;
    std::set<std::wstring>* wlanTrackedStats;
    std::set<std::wstring>* detailTrackedStats;

    const BOOLEAN printGlobal;
    const BOOLEAN printWlan;
    const BOOLEAN printDetail;

    // Statistics summary data structure
    typedef struct detailedStats {
        ULONG latestCounter = 0;
        size_t  samples = 0;
        ULONG64 min = ULONG_MAX;
        ULONG64 max = ULONG_MAX;
        DOUBLE mean = -0.00001;
        DOUBLE stddev = -0.00001;
        DOUBLE median = -0.00001;
        DOUBLE iqr = -0.00001;
    } DETAILED_STATS;
    // Statistics summary data structure for special-case signed data
    typedef struct detailedSignedStats {
        ULONG latestCounter = 0;
        size_t  samples = 0;
        LONG min = LONG_MAX;
        LONG max = LONG_MAX;
        DOUBLE mean = -0.00001;
        DOUBLE stddev = -0.00001;
        DOUBLE median = -0.00001;
        DOUBLE iqr = -0.00001;
    } DETAILED_SIGNED_STATS;
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
    DETAILED_SIGNED_STATS previousRSSISummary = {}; // Special case, RSSI is the only signed statistic

    template<typename T>
    DOUBLE PercentChange(T oldVal, T newVal) {
        if(oldVal == newVal) {
            return 0.0;
        }
        if ((oldVal == 0)) {
            return 1.0;
        }
        else if ((newVal == 0)) {
            return -1.0;
        }
        else if (newVal > oldVal) {
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

        ULONG globalLatestCounter = 0;

        for (const auto &entry : dataStructure)
        {
            std::vector<ULONG64> values = *(entry.GetData().at(statName));
            if (values.empty()) {continue;} // Ignore empty entries

            sort(std::begin(values), std::end(values));

            mins.push_back(*std::min_element(std::begin(values), std::end(values)));
            maxs.push_back(*std::max_element(std::begin(values), std::end(values)));
            means.push_back(std::get<0>(ctl::ctSampledStandardDeviation(std::begin(values), std::end(values))));
            medians.push_back(std::get<1>(ctl::ctInterquartileRange(std::begin(values), std::end(values))));

            // Update latest counter if this entry is the latest
            if (entry.LatestCounter() > globalLatestCounter) {
                    globalLatestCounter = entry.LatestCounter();
            }
        }

        // If no data was collected, return an empty struct
        if (mins.empty()) {return {};}

        auto mstddev_tuple = ctl::ctSampledStandardDeviation(std::begin(means), std::end(means));
        auto interquartile_tuple = ctl::ctInterquartileRange(std::begin(medians), std::end(medians));

        // Build summary struct
        DETAILED_STATS s = {
            globalLatestCounter,
            std::size(mins),
            *std::min_element(std::begin(mins), std::end(mins)),
            *std::max_element(std::begin(maxs), std::end(maxs)),
            std::get<0>(mstddev_tuple),
            std::get<1>(mstddev_tuple),
            std::get<1>(interquartile_tuple),
            std::get<2>(interquartile_tuple) - std::get<0>(interquartile_tuple)};

        // Build a struct marking %change of each value
        // Handle case where no previous summary exists
        DETAILED_STATS_PERCENT_CHANGE c;
        try {
            DETAILED_STATS s_prev = previousGlobalStatsSummaries.at(statName);
            c = {
                PercentChange(s_prev.samples, s.samples),
                PercentChange(s_prev.min, s.min),
                PercentChange(s_prev.max, s.max),
                PercentChange(s_prev.mean, s.mean),
                PercentChange(s_prev.stddev, s.stddev),
                PercentChange(s_prev.median, s.median),
                PercentChange(s_prev.iqr, s.iqr)
            };
        }
        catch (std::out_of_range&) {
            c = {};
        }

        // Update previous tracked with this new summary
        previousGlobalStatsSummaries.insert_or_assign(statName, s);

        return std::make_tuple(s, c);
    }


    // Generate a vector of DETAILED_STATS structs representing summaries for the given statistic for each connection.
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
                entry.LatestCounter(),
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
                    PercentChange(s_prev.samples, s.samples),
                    PercentChange(s_prev.min, s.min),
                    PercentChange(s_prev.max, s.max),
                    PercentChange(s_prev.mean, s.mean),
                    PercentChange(s_prev.stddev, s.stddev),
                    PercentChange(s_prev.median, s.median),
                    PercentChange(s_prev.iqr, s.iqr)
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

    // Get stats summaries for WLAN stats
    std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE> GatherWLANStatisticSummary(std::wstring statName) {
        DETAILED_STATS s;

        // Handle ULONG stats
        std::vector<std::wstring> ulongStats = {L"wlanSignalQuality", L"RxRate", L"TxRate", L"LinkQuality", L"ChCenterFrequency"};
        if (std::find(std::begin(ulongStats), std::end(ulongStats), statName) != std::end(ulongStats)) {
            std::vector<ULONG> values = *(wlanData.GetULONGNumericalData().at(statName));

            if (values.empty()) {return {};} // Ignore empty stats

            sort(std::begin(values), std::end(values));
            auto mstddev_tuple = ctl::ctSampledStandardDeviation(std::begin(values), std::end(values));
            auto interquartile_tuple = ctl::ctInterquartileRange(std::begin(values), std::end(values));

            s = {
                wlanData.LatestCounter(),
                std::size(values),
                *std::min_element(std::begin(values), std::end(values)),
                *std::max_element(std::begin(values), std::end(values)),
                std::get<0>(mstddev_tuple),
                std::get<1>(mstddev_tuple),
                std::get<1>(interquartile_tuple),
                std::get<2>(interquartile_tuple) - std::get<0>(interquartile_tuple)
            };
        }
        // Handle ULONGLONG stats (all others)
        else {
            std::vector<ULONGLONG> values = *(wlanData.GetULONGLONGNumericalData().at(statName));

            if (values.empty()) {return {};} // Ignore empty stats

            sort(std::begin(values), std::end(values));
            auto mstddev_tuple = ctl::ctSampledStandardDeviation(std::begin(values), std::end(values));
            auto interquartile_tuple = ctl::ctInterquartileRange(std::begin(values), std::end(values));

            s = {
                wlanData.LatestCounter(),
                std::size(values),
                *std::min_element(std::begin(values), std::end(values)),
                *std::max_element(std::begin(values), std::end(values)),
                std::get<0>(mstddev_tuple),
                std::get<1>(mstddev_tuple),
                std::get<1>(interquartile_tuple),
                std::get<2>(interquartile_tuple) - std::get<0>(interquartile_tuple)};
        }

        
        // Build a struct marking %change of each value
        // Handle case where no previous summary exists
        DETAILED_STATS_PERCENT_CHANGE c;
        try {
            DETAILED_STATS s_prev = previousGlobalStatsSummaries.at(statName);
            c = {
                PercentChange(s_prev.samples, s.samples),
                PercentChange(s_prev.min, s.min),
                PercentChange(s_prev.max, s.max),
                PercentChange(s_prev.mean, s.mean),
                PercentChange(s_prev.stddev, s.stddev),
                PercentChange(s_prev.median, s.median),
                PercentChange(s_prev.iqr, s.iqr)
            };
        }
        catch (std::out_of_range&) {
            c = {};
        }

        // Update previous tracked with this new summary
        previousGlobalStatsSummaries.insert_or_assign(statName, s);

        return std::make_tuple(s, c);
    }

    // Handle RSSI stat case (only one which is s signed type)
    std::tuple<DETAILED_SIGNED_STATS, DETAILED_STATS_PERCENT_CHANGE> GetWLANRSSISummary() {
        std::vector<LONG> values = *(wlanData.GetRSSIData());

        if (values.empty()) {return {};} // Ignore empty stats

        sort(std::begin(values), std::end(values));
        auto mstddev_tuple = ctl::ctSampledStandardDeviation(std::begin(values), std::end(values));
        auto interquartile_tuple = ctl::ctInterquartileRange(std::begin(values), std::end(values));

        DETAILED_SIGNED_STATS s = {
            wlanData.LatestCounter(),
            std::size(values),
            *std::min_element(std::begin(values), std::end(values)),
            *std::max_element(std::begin(values), std::end(values)),
            std::get<0>(mstddev_tuple),
            std::get<1>(mstddev_tuple),
            std::get<1>(interquartile_tuple),
            std::get<2>(interquartile_tuple) - std::get<0>(interquartile_tuple)
        };

        // Build a struct marking %change of each value
        DETAILED_STATS_PERCENT_CHANGE c;
        // Check for default values on both min/max fields to determine if there is a valid first previous summary
        if ((previousRSSISummary.min != LONG_MAX) || (previousRSSISummary.max != LONG_MAX)) {
            c = {
                PercentChange(previousRSSISummary.samples, s.samples),
                PercentChange(previousRSSISummary.min, s.min),
                PercentChange(previousRSSISummary.max, s.max),
                PercentChange(previousRSSISummary.mean, s.mean),
                PercentChange(previousRSSISummary.stddev, s.stddev),
                PercentChange(previousRSSISummary.median, s.median),
                PercentChange(previousRSSISummary.iqr, s.iqr)
            };
        }
        else {
            c = {};
        }

        // Update previous tracked with this new summary
        previousRSSISummary = s;

        return std::make_tuple(s, c);
    }

    void OpenAndStartGlobalStatSummaryCSV() {
        globalStatsWriter.setFilename(dataDirectory + L"LiveData\\GlobalSummary_" + std::to_wstring(globalFileNumber) + L".csv");
        globalFileNumber++;
        globalStatsWriter.create_file(std::wstring(L"GLobal Statistic,Min,Min %change,Mean,Mean %change,Max,Max %change,StdDev,StdDev %change,Median,Median %change,IQR,IQR %change"));
    }
    template<typename T>
    void SaveGlobalStatSummaryLineToCSV(std::wstring title, std::tuple<T, DETAILED_STATS_PERCENT_CHANGE> summary) {
        globalStatsWriter.write_row(
            title + L"," +
            std::to_wstring(std::get<0>(summary).min) + L"," + std::to_wstring(std::get<1>(summary).min * 100) + L"," +
            std::to_wstring(std::get<0>(summary).mean) + L"," + std::to_wstring(std::get<1>(summary).mean * 100) + L"," +
            std::to_wstring(std::get<0>(summary).max) + L"," + std::to_wstring(std::get<1>(summary).max * 100) + L"," +
            std::to_wstring(std::get<0>(summary).stddev) + L"," + std::to_wstring(std::get<1>(summary).stddev * 100) + L"," +
            std::to_wstring(std::get<0>(summary).median) + L"," + std::to_wstring(std::get<1>(summary).median * 100) + L"," +
            std::to_wstring(std::get<0>(summary).iqr) + L"," + std::to_wstring(std::get<1>(summary).iqr * 100)
        );
    }

    void OpenAndStartDetailStatSummaryCSV() {
        globalStatsWriter.setFilename(dataDirectory + L"LiveData\\DetailSummary_" + std::to_wstring(detailFileNumber) + L".csv");
        detailFileNumber++;
        globalStatsWriter.create_file(std::wstring(L"Samples,Min,Min %change,Mean,Mean %change,Max,Max %change,StdDev,StdDev %change,Median,Median %change,IQR,IQR %change"));
    }
    void SaveDetailStatHeaderLineToCSV(std::wstring title) {
        globalStatsWriter.write_empty_row();
        globalStatsWriter.write_row(title + L",Min,Min %change,Mean,Mean %change,Max,Max %change,StdDev,StdDev %change,Median,Median %change,IQR,IQR %change");
    }
    void SaveDetailStatSummaryLineToCSV(std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE> summary) {
        globalStatsWriter.write_row(
            std::to_wstring(std::get<0>(summary).samples) + L"," +
            std::to_wstring(std::get<0>(summary).min) + L"," + std::to_wstring(std::get<1>(summary).min * 100) + L"," +
            std::to_wstring(std::get<0>(summary).mean) + L"," + std::to_wstring(std::get<1>(summary).mean * 100) + L"," +
            std::to_wstring(std::get<0>(summary).max) + L"," + std::to_wstring(std::get<1>(summary).max * 100) + L"," +
            std::to_wstring(std::get<0>(summary).stddev) + L"," + std::to_wstring(std::get<1>(summary).stddev * 100) + L"," +
            std::to_wstring(std::get<0>(summary).median) + L"," + std::to_wstring(std::get<1>(summary).median * 100) + L"," +
            std::to_wstring(std::get<0>(summary).iqr) + L"," + std::to_wstring(std::get<1>(summary).iqr * 100)
        );
    }

    void ResetSetConsoleColor() {
        SetConsoleTextAttribute(hConsole, orig_wAttributes);
    }
    void SetConsoleColorConnectionStatus(BOOLEAN connectionOpen) {
        if (connectionOpen) {
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | (orig_wAttributes & BACKGROUND_MASK));
        }
        else {
            ResetSetConsoleColor();
        }
    }
    void SetConsoleColorFromPercentChange(DOUBLE percentChange) {
        if(percentChange <= -1.0) { // Blue BG
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_BLUE);
        }
        else if(percentChange < -0.25) { // Blue
            SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY | (orig_wAttributes & BACKGROUND_MASK));
        }
        else if(percentChange < -0.01) { // Cyan
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | (orig_wAttributes & BACKGROUND_MASK));
        }
        else if(percentChange < 0.0) { // Green
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY | (orig_wAttributes & BACKGROUND_MASK));
        }
        else if (percentChange == 0.0) { // White -- "No Change"
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | (orig_wAttributes & BACKGROUND_MASK));
        }
        else if (percentChange < 0.01) { // Yellow
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY | (orig_wAttributes & BACKGROUND_MASK));
        }
        else if (percentChange < 0.25) { // Magenta
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY | (orig_wAttributes & BACKGROUND_MASK));
        }
        else if (percentChange < 1.0) { // Red
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY | (orig_wAttributes & BACKGROUND_MASK));
        }
        else if (percentChange >= 1.0){ // Red BG
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_RED | (orig_wAttributes & BACKGROUND_MASK));
        }
        else { // Error state, should never happen
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_RED | BACKGROUND_GREEN);
        }
    }
    void PrintStat(ULONG64 stat, DOUBLE percentChange) {
        ResetSetConsoleColor();
        std::wcout << L" | ";

        SetConsoleColorFromPercentChange(percentChange);
        std::wcout << std::right << std::setw(dataColumnWidth) << std::setfill(L' ') << stat;
    }
    void PrintStat(LONG stat, DOUBLE percentChange) {
        ResetSetConsoleColor();
        std::wcout << L" | ";

        SetConsoleColorFromPercentChange(percentChange);
        std::wcout << std::right << std::setw(dataColumnWidth) << std::setfill(L' ') << stat;
    }
    void PrintStat(DOUBLE stat, DOUBLE percentChange) {
        ResetSetConsoleColor();
        std::wcout << L" | ";

        std::wcout.precision(2);
        SetConsoleColorFromPercentChange(percentChange);
        std::wcout << std::right << std::setw(dataColumnWidth) << std::setfill(L' ') << std::fixed << stat;
    }
    template<typename T>
    void PrintGlobalStatSummary(std::wstring title, std::tuple<T, DETAILED_STATS_PERCENT_CHANGE> summary) {
        SetConsoleColorConnectionStatus(std::get<0>(summary).latestCounter == tableCounter);
        std::wcout << std::left << std::setw(titleColumnWidth) << std::setfill(L' ') << title;

        PrintStat(std::get<0>(summary).min, std::get<1>(summary).min);
        PrintStat(std::get<0>(summary).mean, std::get<1>(summary).mean);
        PrintStat(std::get<0>(summary).max, std::get<1>(summary).max);
        PrintStat(std::get<0>(summary).stddev, std::get<1>(summary).stddev);
        PrintStat(std::get<0>(summary).median, std::get<1>(summary).median);
        PrintStat(std::get<0>(summary).iqr, std::get<1>(summary).iqr);

        ResetSetConsoleColor();
        std::wcout << L" |" << std::endl;
    }
    void PrintPerConnectionStatSummary(std::tuple<DETAILED_STATS, DETAILED_STATS_PERCENT_CHANGE> summary) {
        SetConsoleColorConnectionStatus(std::get<0>(summary).latestCounter == tableCounter);
        std::wcout << L"Samples: ";
        std::wcout << std::left << std::setw(titleColumnWidth) << std::setfill(L' ') << std::to_wstring(std::get<0>(summary).samples) + 
                                                                        ((std::get<0>(summary).samples == maxHistoryLength) ? std::wstring(L" (max)") : std::wstring(L""));

        PrintStat(std::get<0>(summary).min, std::get<1>(summary).min);
        PrintStat(std::get<0>(summary).mean, std::get<1>(summary).mean);
        PrintStat(std::get<0>(summary).max, std::get<1>(summary).max);
        PrintStat(std::get<0>(summary).stddev, std::get<1>(summary).stddev);
        PrintStat(std::get<0>(summary).median, std::get<1>(summary).median);
        PrintStat(std::get<0>(summary).iqr, std::get<1>(summary).iqr);

        ResetSetConsoleColor();
        std::wcout << L" |" << std::endl;
    }
    
    void PrintHeaderTitle(std::wstring title) {
        std::wcout << L" | " << std::right << std::setw(dataColumnWidth) << std::setfill(L' ') << title;
    }
    void PrintStdHeader(std::wstring firstColumnName) {
        PrintStdSeparator();
        std::wcout << std::left << std::setw(titleColumnWidth) << std::setfill(L' ') << firstColumnName;
            PrintHeaderTitle(L"Min");
            PrintHeaderTitle(L"Mean");
            PrintHeaderTitle(L"Max");
            PrintHeaderTitle(L"StdDev");
            PrintHeaderTitle(L"Median");
            PrintHeaderTitle(L"IQR");
        std::wcout << L" |" << std::endl;
        PrintStdSeparator();
    }
    void PrintStdSeparator() {
        std::wcout << std::wstring(titleColumnWidth + (6 * (dataColumnWidth + 3)), L'-') << "-+" << std::endl;
    }

    void clear_screen() { 
        COORD tl = {0,0};
        CONSOLE_SCREEN_BUFFER_INFO s;
        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);   
        GetConsoleScreenBufferInfo(console, &s);
        DWORD written, cells = s.dwSize.X * s.dwSize.Y;
        FillConsoleOutputCharacter(console, ' ', cells, tl, &written);
        FillConsoleOutputAttribute(console, s.wAttributes, cells, tl, &written);
        SetConsoleCursorPosition(console, tl);
    }

    void PrintDataUpdate() {
        // Do not do live updates if there are no tracked stats
        if (globalTrackedStats->empty() && detailTrackedStats->empty() && wlanTrackedStats->empty()) {return;}

        if (printGlobal || printDetail || printWlan) {
            clear_screen();
            // Print counter
            std::wcout << " / " << std::to_wstring(tableCounter - 1) << " \\" << std::endl;
        }


        // -- Global summary table --
        if (!globalTrackedStats->empty()) {
            if (printGlobal) {PrintStdHeader(L"GLobal Statistics");}
            OpenAndStartGlobalStatSummaryCSV();
            for (std::wstring stat : *globalTrackedStats)
            {
                auto detailedStatsSummary = GatherGlobalStatisticSummary(stat, trackedStatisticsDataTypes.at(stat));
                if (printGlobal) {PrintGlobalStatSummary(stat, detailedStatsSummary);}
                SaveGlobalStatSummaryLineToCSV(stat, detailedStatsSummary);
            }

            if (printGlobal) {
                PrintStdSeparator();
                std::wcout << std::endl;
            }
        }

        // -- WLAN table --
        if (trackWlanStats && !wlanTrackedStats->empty()) {
            if (printWlan) {PrintStdHeader(L"WLAN Information");}
            // Open the global stat summary csv if not created by the global stats loop
            if (globalTrackedStats->empty()) {
                OpenAndStartGlobalStatSummaryCSV();
            }
            for (std::wstring stat : *wlanTrackedStats)
            {
                if (stat == L"Rssi") {
                    auto detailedStatsSummary = GetWLANRSSISummary();
                    if (printWlan) {PrintGlobalStatSummary(stat, detailedStatsSummary);}
                    SaveGlobalStatSummaryLineToCSV(stat, detailedStatsSummary);  
                }
                else {
                    auto detailedStatsSummary = GatherWLANStatisticSummary(stat);
                    if (printWlan) {PrintGlobalStatSummary(stat, detailedStatsSummary);}
                    SaveGlobalStatSummaryLineToCSV(stat, detailedStatsSummary); 
                }
            }

            if (printWlan) {
                PrintStdSeparator();
                std::wcout << std::endl;
            }
        }

        // -- Detailed Per-Connection Results --
        if (!detailTrackedStats->empty()) {
            OpenAndStartDetailStatSummaryCSV();
            for (std::wstring stat : *detailTrackedStats)
            {
                if (printDetail) {PrintStdHeader(stat);}
                SaveDetailStatHeaderLineToCSV(stat);

                auto detailedStatsSummaries = GatherPerConnectionStatisticSummaries(stat, trackedStatisticsDataTypes.at(stat));

                for (auto summary : detailedStatsSummaries)
                {
                    if (printDetail) {PrintPerConnectionStatSummary(summary);}
                    SaveDetailStatSummaryLineToCSV(summary);
                }
                if (printDetail) {PrintStdSeparator();}
            }
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

            wlanData.UpdateData(tableCounter, maxHistoryLength, hClient);

            RemoveStaleDataPoints();
        }
        catch (const std::exception& e) {
            wprintf(L"ctsEstats::UpdateEstats exception: %ws\n", ctl::ctString::format_exception(e).c_str());
        }

        // Print to console and CSV's
        PrintDataUpdate();

        if (!accessDenied) {
            // schedule timer from this moment
            timer.schedule_singleton([this]() { UpdateEstats(); }, pollRateMS);
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
        const auto emplaceResults = data.emplace(tableEntry, maxHistoryLength);
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
            return dataPoint.LatestCounter() != tableCounter;
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

            const auto bandwidthInstance = bandwidthData.find(
                details::EstatsDataPoint<TcpConnectionEstatsBandwidth>(localAddr, remoteAddr));
            const auto fBandwidthInstanceFound = bandwidthInstance != bandwidthData.end();

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

            // Print bandwith data as well, only if available
            if (fSenderCongestionInstanceFound && fByteTrackingInstanceFound && (!fBandwidthInstanceFound))
            {
                senderCongestionWriter.write_row(
                    senderCongestionInstance->PrintAddresses() +
                    senderCongestionInstance->PrintData() +
                    byteTrackingInstance->PrintData());
            }
            if (fSenderCongestionInstanceFound && fByteTrackingInstanceFound && fBandwidthInstanceFound)
            {
                senderCongestionWriter.write_row(
                    senderCongestionInstance->PrintAddresses() +
                    senderCongestionInstance->PrintData() +
                    byteTrackingInstance->PrintData() +
                    bandwidthInstance->PrintData());
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
                return dataPoint.LatestCounter() != tableCounter;
            });
        } // while loop
    }
};
} // namespace
