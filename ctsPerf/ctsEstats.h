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

// os headers
#include <Windows.h>
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <Iphlpapi.h>
#include <Tcpestats.h>

// ctl headers
#include <ctSockaddr.hpp>

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
    ULONG SetEstats(_In_ PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_write_type pRw)
    {
        return ::SetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            reinterpret_cast<PUCHAR>(pRw), 0, static_cast<ULONG>(sizeof(*pRw)),
            0);
    }

    template <typename TCP_ESTATS_TYPE TcpType>
    ULONG GetReadOnlyStaticEstats(_In_ PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_static_type pRos)
    {
        return ::GetPerTcpConnectionEStats(
            tcpRow,
            TcpType,
            nullptr, 0, 0, // read-write information
            reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
            nullptr, 0, 0); // read-only dynamic information
    }

    template <typename TCP_ESTATS_TYPE TcpType>
    ULONG GetReadOnlyDynamicEstats(_In_ PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_dynamic_type pRod)
    {
        return ::GetPerTcpConnectionEStats(
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
        EstatsDataTracking(const EstatsDataTracking&) = delete;
        EstatsDataTracking& operator=(const EstatsDataTracking&) = delete;

        static LPWSTR PrintHeader();
        void PrintData();
        void StartTracking(_In_ PMIB_TCPROW tcpRow);
        void UpdateData(_In_ PMIB_TCPROW tcpRow);
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsSynOpts> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"Mss-Received, Mss-Sent";
        }
        std::wstring PrintData()
        {
            return std::to_wstring(MssRcvd) + L", " + std::to_wstring(MssSent);
        }
        void StartTracking()
        {
            return; // always on
        }
        void UpdateData(_In_ PMIB_TCPROW tcpRow)
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
    class EstatsDataTracking<TcpConnectionEstatsSndCong> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData()
        {
            return L"";
        }
        void StartTracking(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_SND_CONG_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            if (0 == SetEstats<TcpConnectionEstatsSndCong>(tcpRow, &Rw)) {
                fEnabled = true;
            }
        }
        void UpdateData(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_SND_CONG_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsSndCong>(tcpRow, &Rod)) {
                rod_data.push_back(Rod);
            }
        }

    private:
        std::vector<TCP_ESTATS_SND_CONG_ROD_v0> rod_data;
        bool fEnabled = false;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsPath> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData()
        {
            return L"";
        }
        void StartTracking(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_PATH_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            if (0 == SetEstats<TcpConnectionEstatsPath>(tcpRow, &Rw)) {
                fEnabled = true;
            }
        }
        void UpdateData(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_PATH_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsPath>(tcpRow, &Rod)) {
                rod_data.push_back(Rod);
            }
        }

    private:
        std::vector<TCP_ESTATS_PATH_ROD_v0> rod_data;
        bool fEnabled = false;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsRec> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData()
        {
            return L"";
        }
        void StartTracking(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_REC_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            if (0 == SetEstats<TcpConnectionEstatsRec>(tcpRow, &Rw)) {
                fEnabled = true;
            }
        }
        void UpdateData(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_REC_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsRec>(tcpRow, &Rod)) {
                rod_data.push_back(Rod);
            }
        }

    private:
        std::vector<TCP_ESTATS_REC_ROD_v0> rod_data;
        bool fEnabled = false;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsObsRec> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData()
        {
            return L"";
        }
        void StartTracking(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_OBS_REC_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            if (0 == SetEstats<TcpConnectionEstatsObsRec>(tcpRow, &Rw)) {
                fEnabled = true;
            }
        }
        void UpdateData(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_OBS_REC_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsObsRec>(tcpRow, &Rod)) {
                rod_data.push_back(Rod);
            }
        }

    private:
        std::vector<TCP_ESTATS_OBS_REC_ROD_v0> rod_data;
        bool fEnabled = false;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsBandwidth> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData()
        {
            return L"";
        }
        void StartTracking(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_BANDWIDTH_RW_v0 Rw;
            Rw.EnableCollectionInbound = TcpBoolOptEnabled;
            Rw.EnableCollectionOutbound = TcpBoolOptEnabled;
            if (0 == SetEstats<TcpConnectionEstatsBandwidth>(tcpRow, &Rw)) {
                fEnabled = true;
            }
        }
        void UpdateData(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_BANDWIDTH_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsBandwidth>(tcpRow, &Rod)) {
                rod_data.push_back(Rod);
            }
        }

    private:
        std::vector<TCP_ESTATS_BANDWIDTH_ROD_v0> rod_data;
        bool fEnabled = false;
    };

    template <>
    class EstatsDataTracking<TcpConnectionEstatsFineRtt> {
    public:
        static LPWSTR PrintHeader()
        {
            return L"";
        }
        std::wstring PrintData()
        {
            return L"";
        }
        void StartTracking(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_FINE_RTT_RW_v0 Rw;
            Rw.EnableCollection = TRUE;
            if (0 == SetEstats<TcpConnectionEstatsFineRtt>(tcpRow, &Rw)) {
                fEnabled = true;
            }
        }
        void UpdateData(_In_ PMIB_TCPROW tcpRow)
        {
            TCP_ESTATS_FINE_RTT_ROD_v0 Rod;
            ZeroMemory(&Rod, sizeof(Rod));
            if (0 == GetReadOnlyDynamicEstats<TcpConnectionEstatsFineRtt>(tcpRow, &Rod)) {
                rod_data.push_back(Rod);
            }
        }

    private:
        std::vector<TCP_ESTATS_FINE_RTT_ROD_v0> rod_data;
        bool fEnabled = false;
    };


    struct EstatsDataPoint {
        ctl::ctSockaddr localAddr;
        ctl::ctSockaddr remoteAddr;
    };
} // namespace

//
// ctsEstats algorithm
// - enumerate TCP connections:
//   - call SetPerTcpConnectionEStats for each
//   - keep each connection in a vector
// - establish a timer ever N seconds
// - when timer fires:
//   - enumerate TCP connections again
//   - if the connection is on our vector:
//     - call GetPerTcpConnectionEstats
//   - if the connection is not in our vector:
//     - call SetPerTcpConnectionEStats
//     - add it to our vector
//   - if a connection was reported as closed:
//     - call SetPerTcpConnectionEStats
//     - write its data to file
//     - remove it from the vector
//   - if a connection in the vector was not found
//     - write its data to file
//     - remove it from the vector
//
namespace ctsPerf {
} // namespace