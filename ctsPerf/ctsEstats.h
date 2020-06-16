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

// wil headers
#include <wil/resource.h>
#include <wil/win32_helpers.h>

// ctl headers
#include <ctString.hpp>
#include <ctSockaddr.hpp>

namespace ctsPerf
{

    namespace Details
    {
        template <TCP_ESTATS_TYPE TcpType>
        struct EstatsTypeConverter {};

        template <>
        struct EstatsTypeConverter<TcpConnectionEstatsSynOpts>
        {
            typedef void* read_write_type;
            typedef TCP_ESTATS_SYN_OPTS_ROS_v0 read_only_static_type;
            typedef void* read_only_dynamic_type;
        };

        template<>
        struct EstatsTypeConverter<TcpConnectionEstatsData>
        {
            typedef TCP_ESTATS_DATA_RW_v0 read_write_type;
            typedef void* read_only_static_type;
            typedef TCP_ESTATS_DATA_ROD_v0 read_only_dynamic_type;
        };

        template<>
        struct EstatsTypeConverter<TcpConnectionEstatsSndCong>
        {
            typedef TCP_ESTATS_SND_CONG_RW_v0 read_write_type;
            typedef TCP_ESTATS_SND_CONG_ROS_v0 read_only_static_type;
            typedef TCP_ESTATS_SND_CONG_ROD_v0 read_only_dynamic_type;
        };

        template <>
        struct EstatsTypeConverter<TcpConnectionEstatsPath>
        {
            typedef TCP_ESTATS_PATH_RW_v0 read_write_type;
            typedef void* read_only_static_type;
            typedef TCP_ESTATS_PATH_ROD_v0 read_only_dynamic_type;
        };

        template <>
        struct EstatsTypeConverter<TcpConnectionEstatsSendBuff>
        {
            typedef TCP_ESTATS_SEND_BUFF_RW_v0 read_write_type;
            typedef void* read_only_static_type;
            typedef TCP_ESTATS_SEND_BUFF_ROD_v0 read_only_dynamic_type;
        };

        template <>
        struct EstatsTypeConverter<TcpConnectionEstatsRec>
        {
            typedef TCP_ESTATS_REC_RW_v0 read_write_type;
            typedef void* read_only_static_type;
            typedef TCP_ESTATS_REC_ROD_v0 read_only_dynamic_type;
        };

        template <>
        struct EstatsTypeConverter<TcpConnectionEstatsObsRec>
        {
            typedef TCP_ESTATS_OBS_REC_RW_v0 read_write_type;
            typedef void* read_only_static_type;
            typedef TCP_ESTATS_OBS_REC_ROD_v0 read_only_dynamic_type;
        };

        template <>
        struct EstatsTypeConverter<TcpConnectionEstatsBandwidth>
        {
            typedef TCP_ESTATS_BANDWIDTH_RW_v0 read_write_type;
            typedef void* read_only_static_type;
            typedef TCP_ESTATS_BANDWIDTH_ROD_v0 read_only_dynamic_type;
        };

        template <>
        struct EstatsTypeConverter<TcpConnectionEstatsFineRtt>
        {
            typedef TCP_ESTATS_FINE_RTT_RW_v0 read_write_type;
            typedef void* read_only_static_type;
            typedef TCP_ESTATS_FINE_RTT_ROD_v0 read_only_dynamic_type;
        };

        template <TCP_ESTATS_TYPE TcpType>
        void SetPerConnectionEstats(const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_write_type* pRw)
        {
            const auto err = ::SetPerTcpConnectionEStats(
                tcpRow,
                TcpType,
                reinterpret_cast<PUCHAR>(pRw), 0, static_cast<ULONG>(sizeof(*pRw)),
                0);
            if (err != 0)
            {
                throw ctl::ctException(err, L"SetPerTcpConnectionEStats", false);
            }
        }
        template <TCP_ESTATS_TYPE TcpType>
        void SetPerConnectionEstats(const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_write_type* pRw)
        {
            const auto err = ::SetPerTcp6ConnectionEStats(
                tcpRow,
                TcpType,
                reinterpret_cast<PUCHAR>(pRw), 0, static_cast<ULONG>(sizeof(*pRw)),
                0);
            if (err != 0)
            {
                throw ctl::ctException(err, L"SetPerTcp6ConnectionEStats", false);
            }
        }

        // TcpConnectionEstatsSynOpts is unique in that there isn't a RW type to query for
        inline ULONG GetPerConnectionStaticEstats(const PMIB_TCPROW tcpRow, TCP_ESTATS_SYN_OPTS_ROS_v0* pRos) noexcept
        {
            return ::GetPerTcpConnectionEStats(
                tcpRow,
                TcpConnectionEstatsSynOpts,
                nullptr, 0, 0, // read-write information
                reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
                nullptr, 0, 0); // read-only dynamic information
        }
        template <TCP_ESTATS_TYPE TcpType>
        ULONG GetPerConnectionStaticEstats(const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_static_type* pRos) noexcept
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
        inline ULONG GetPerConnectionStaticEstats(const PMIB_TCP6ROW tcpRow, TCP_ESTATS_SYN_OPTS_ROS_v0* pRos) noexcept
        {
            return ::GetPerTcp6ConnectionEStats(
                tcpRow,
                TcpConnectionEstatsSynOpts,
                nullptr, 0, 0, // read-write information
                reinterpret_cast<PUCHAR>(pRos), 0, static_cast<ULONG>(sizeof(*pRos)), // read-only static information
                nullptr, 0, 0); // read-only dynamic information
        }
        template <TCP_ESTATS_TYPE TcpType>
        ULONG GetPerConnectionStaticEstats(const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_static_type* pRos) noexcept
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


        template <TCP_ESTATS_TYPE TcpType>
        ULONG GetPerConnectionDynamicEstats(const PMIB_TCPROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_dynamic_type* pRod) noexcept
        {
            typename EstatsTypeConverter<TcpType>::read_write_type rw{};

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
        template <TCP_ESTATS_TYPE TcpType>
        ULONG GetPerConnectionDynamicEstats(const PMIB_TCP6ROW tcpRow, typename EstatsTypeConverter<TcpType>::read_only_dynamic_type* pRod) noexcept
        {
            typename EstatsTypeConverter<TcpType>::read_write_type rw{};

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
        class EstatsDataTracking
        {
        private:
            EstatsDataTracking() = default;
            ~EstatsDataTracking() = default;
        public:
            EstatsDataTracking(const EstatsDataTracking&) = delete;
            EstatsDataTracking& operator=(const EstatsDataTracking&) = delete;
            EstatsDataTracking(EstatsDataTracking&&) = delete;
            EstatsDataTracking& operator=(EstatsDataTracking&&) = delete;

            static PCWSTR PrintHeader() = delete;

            void PrintData() const = delete;

            template <typename PTCPROW>
            void StartTracking(PTCPROW tcpRow) const = delete;

            template <typename PTCPROW>
            void UpdateData(PTCPROW tcpRow) = delete;
        };

        template <>
        class EstatsDataTracking<TcpConnectionEstatsSynOpts>
        {
        public:
            static PCWSTR PrintHeader() noexcept
            {
                return L"Mss-Received,Mss-Sent";
            }

            [[nodiscard]] std::wstring PrintData() const
            {
                return L"," + std::to_wstring(m_mssRcvd) + L"," + std::to_wstring(m_mssSent);
            }

            template <typename PTCPROW>
            // ReSharper disable once CppMemberFunctionMayBeStatic
            void StartTracking(const PTCPROW) const noexcept
            {
                // always on
            }
            template <typename PTCPROW>
            void UpdateData(const PTCPROW tcpRow) noexcept
            {
                if (m_mssRcvd == 0)
                {
                    TCP_ESTATS_SYN_OPTS_ROS_v0 ros{};
                    if (0 == GetPerConnectionStaticEstats(tcpRow, &ros))
                    {
                        m_mssRcvd = ros.MssRcvd;
                        m_mssSent = ros.MssSent;
                    }
                }
            }

        private:
            ULONG m_mssRcvd = 0;
            ULONG m_mssSent = 0;
        };

        template <>
        class EstatsDataTracking<TcpConnectionEstatsData>
        {
        public:
            static PCWSTR PrintHeader() noexcept
            {
                return L"Bytes-In,Bytes-Out";
            }

            [[nodiscard]] std::wstring PrintData() const
            {
                return L"," + std::to_wstring(m_dataBytesIn) + L"," + std::to_wstring(m_dataBytesOut);
            }

            template <typename PTCPROW>
            void StartTracking(const PTCPROW tcpRow) const
            {
                TCP_ESTATS_DATA_RW_v0 rw{};
                rw.EnableCollection = TRUE;
                SetPerConnectionEstats<TcpConnectionEstatsData>(tcpRow, &rw);
            }
            template <typename PTCPROW>
            void UpdateData(const PTCPROW tcpRow) noexcept
            {
                TCP_ESTATS_DATA_ROD_v0 rod{};
                if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsData>(tcpRow, &rod))
                {
                    m_dataBytesIn = rod.DataBytesIn;
                    m_dataBytesOut = rod.DataBytesOut;
                }
            }

        private:
            ULONG64 m_dataBytesIn = 0;
            ULONG64 m_dataBytesOut = 0;
        };

        template <>
        class EstatsDataTracking<TcpConnectionEstatsSndCong>
        {
        public:
            static PCWSTR PrintHeader() noexcept
            {
                return L"CongWin(mean),CongWin(stddev),"
                    L"XIntoReceiverLimited,XIntoSenderLimited,XIntoCongestionLimited,"
                    L"BytesSentRecvLimited,BytesSentSenderLimited,BytesSentCongLimited";
            }

            [[nodiscard]] std::wstring PrintData() const
            {
                return
                    ctsWriteDetails::PrintMeanStdDev(m_conjestionWindows) +
                    ctl::ctString::ctFormatString(
                        L",%lu,%lu,%lu,%Iu,%Iu,%Iu",
                        m_transitionsIntoReceiverLimited,
                        m_transitionsIntoSenderLimited,
                        m_transitionsIntoCongestionLimited,
                        m_bytesSentInReceiverLimited,
                        m_bytesSentInSenderLimited,
                        m_bytesSentInCongestionLimited);
            }

            template <typename PTCPROW>
            void StartTracking(const PTCPROW tcpRow) const
            {
                TCP_ESTATS_SND_CONG_RW_v0 rw{};
                rw.EnableCollection = TRUE;
                SetPerConnectionEstats<TcpConnectionEstatsSndCong>(tcpRow, &rw);
            }
            template <typename PTCPROW>
            void UpdateData(const PTCPROW tcpRow)
            {
                TCP_ESTATS_SND_CONG_ROD_v0 rod{};
                if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsSndCong>(tcpRow, &rod))
                {
                    m_conjestionWindows.push_back(rod.CurCwnd);
                    m_bytesSentInReceiverLimited = rod.SndLimBytesRwin;
                    m_bytesSentInSenderLimited = rod.SndLimBytesSnd;
                    m_bytesSentInCongestionLimited = rod.SndLimBytesCwnd;
                    m_transitionsIntoReceiverLimited = rod.SndLimTransRwin;
                    m_transitionsIntoSenderLimited = rod.SndLimTransSnd;
                    m_transitionsIntoCongestionLimited = rod.SndLimTransCwnd;
                }
            }

        private:
            std::vector<ULONG> m_conjestionWindows;

            SIZE_T m_bytesSentInReceiverLimited = 0;
            SIZE_T m_bytesSentInSenderLimited = 0;
            SIZE_T m_bytesSentInCongestionLimited = 0;

            ULONG m_transitionsIntoReceiverLimited = 0;
            ULONG m_transitionsIntoSenderLimited = 0;
            ULONG m_transitionsIntoCongestionLimited = 0;
        };

        template <>
        class EstatsDataTracking<TcpConnectionEstatsPath>
        {
        public:
            static PCWSTR PrintHeader() noexcept
            {
                return L"BytesRetrans,DupeAcks,SelectiveAcks,CongSignals,MaxSegSize,"
                    L"RetransTimer(mean),RetransTimer(stddev),"
                    L"RTT(mean),Rtt(stddev)";
            }

            [[nodiscard]] std::wstring PrintData() const
            {
                return ctl::ctString::ctFormatString(
                    L",%lu,%lu,%lu,%lu,%lu",
                    m_bytesRetrans,
                    m_dupAcksRcvd,
                    m_sacksRcvd,
                    m_congestionSignals,
                    m_maxSegmentSize) +
                    ctsWriteDetails::PrintMeanStdDev(m_retransmitTimer) +
                    ctsWriteDetails::PrintMeanStdDev(m_roundTripTime);
            }

            template <typename PTCPROW>
            void StartTracking(const PTCPROW tcpRow) const
            {
                TCP_ESTATS_PATH_RW_v0 rw{};
                rw.EnableCollection = TRUE;
                SetPerConnectionEstats<TcpConnectionEstatsPath>(tcpRow, &rw);
            }
            template <typename PTCPROW>
            void UpdateData(const PTCPROW tcpRow)
            {
                TCP_ESTATS_PATH_ROD_v0 rod{};
                if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsPath>(tcpRow, &rod))
                {
                    m_retransmitTimer.push_back(rod.CurRto);
                    m_roundTripTime.push_back(rod.SmoothedRtt);
                    m_bytesRetrans = rod.BytesRetrans;
                    m_dupAcksRcvd = rod.DupAcksIn;
                    m_sacksRcvd = rod.SacksRcvd;
                    m_congestionSignals = rod.CongSignals;
                    m_maxSegmentSize = rod.CurMss;
                }
            }

        private:
            std::vector<ULONG> m_retransmitTimer;
            std::vector<ULONG> m_roundTripTime;
            ULONG m_bytesRetrans = 0;
            ULONG m_dupAcksRcvd = 0;
            ULONG m_sacksRcvd = 0;
            ULONG m_congestionSignals = 0;
            ULONG m_maxSegmentSize = 0;
        };

        template <>
        class EstatsDataTracking<TcpConnectionEstatsRec>
        {
        public:
            static PCWSTR PrintHeader() noexcept
            {
                return L"LocalRecvWin(min),LocalRecvWin(max),LocalRecvWin(calculated-min),LocalRecvWin(calculated-max),LocalRecvWin(calculated-mean),LocalRecvWin(calculated-stddev)";
            }

            [[nodiscard]] std::wstring PrintData() const
            {
                std::wstring formattedString(L",");
                formattedString += ctl::ctString::ctFormatString(L"%lu,", m_minReceiveWindow);
                formattedString += ctl::ctString::ctFormatString(L"%lu,", m_maxReceiveWindow);

                ULONG calculatedMin = ULONG_MAX;
                ULONG calculatedMax = 0;
                for (const auto& value : m_receiveWindow)
                {
                    if (value < calculatedMin)
                    {
                        calculatedMin = value;
                    }
                    if (value > calculatedMax)
                    {
                        calculatedMax = value;
                    }
                }
                formattedString += ctl::ctString::ctFormatString(L"%lu,", calculatedMin);
                formattedString += ctl::ctString::ctFormatString(L"%lu", calculatedMax);

                return
                    formattedString +
                    ctsWriteDetails::PrintMeanStdDev(m_receiveWindow);
            }

            template <typename PTCPROW>
            void StartTracking(const PTCPROW tcpRow) const
            {
                TCP_ESTATS_REC_RW_v0 rw{};
                rw.EnableCollection = TRUE;
                SetPerConnectionEstats<TcpConnectionEstatsRec>(tcpRow, &rw);
            }

            template <typename PTCPROW>
            void UpdateData(const PTCPROW tcpRow)
            {
                TCP_ESTATS_REC_ROD_v0 rod{};
                if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsRec>(tcpRow, &rod))
                {
                    m_receiveWindow.push_back(rod.CurRwinSent);
                    m_minReceiveWindow = rod.MinRwinSent;
                    m_maxReceiveWindow = rod.MaxRwinSent;
                }
            }

        private:
            std::vector<ULONG> m_receiveWindow;
            ULONG m_minReceiveWindow = 0;
            ULONG m_maxReceiveWindow = 0;
        };

        template <>
        class EstatsDataTracking<TcpConnectionEstatsObsRec>
        {
        public:
            static PCWSTR PrintHeader() noexcept
            {
                return L"RemoteRecvWin(min),RemoteRecvWin(max),RemoteRecvWin(calculated-min),RemoteRecvWin(calculated-max),RemoteRecvWin(calculated-mean),RemoteRecvWin(calculated-stddev)";
            }

            [[nodiscard]] std::wstring PrintData() const
            {
                std::wstring formattedString(L",");
                formattedString += ctl::ctString::ctFormatString(L"%lu,", m_minReceiveWindow);
                formattedString += ctl::ctString::ctFormatString(L"%lu,", m_maxReceiveWindow);

                ULONG calculatedMin = ULONG_MAX;
                ULONG calculatedMax = 0;
                for (const auto& value : m_receiveWindow)
                {
                    if (value < calculatedMin)
                    {
                        calculatedMin = value;
                    }
                    if (value > calculatedMax)
                    {
                        calculatedMax = value;
                    }
                }

                formattedString += ctl::ctString::ctFormatString(L"%lu,", calculatedMin);
                formattedString += ctl::ctString::ctFormatString(L"%lu", calculatedMax);

                return
                    formattedString +
                    ctsWriteDetails::PrintMeanStdDev(m_receiveWindow);
            }

            template <typename PTCPROW>
            void StartTracking(const PTCPROW tcpRow) const
            {
                TCP_ESTATS_OBS_REC_RW_v0 rw{};
                rw.EnableCollection = TRUE;
                SetPerConnectionEstats<TcpConnectionEstatsObsRec>(tcpRow, &rw);
            }
            template <typename PTCPROW>
            void UpdateData(const PTCPROW tcpRow)
            {
                TCP_ESTATS_OBS_REC_ROD_v0 rod{};
                if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsObsRec>(tcpRow, &rod))
                {
                    m_receiveWindow.push_back(rod.CurRwinRcvd);
                    m_minReceiveWindow = rod.MinRwinRcvd;
                    m_maxReceiveWindow = rod.MaxRwinRcvd;
                }
            }

        private:
            std::vector<ULONG> m_receiveWindow;
            ULONG m_minReceiveWindow = 0;
            ULONG m_maxReceiveWindow = 0;
        };

        template <>
        class EstatsDataTracking<TcpConnectionEstatsBandwidth>
        {
        public:
            static PCWSTR PrintHeader() noexcept
            {
                return L"";
            }
            // ReSharper disable once CppMemberFunctionMayBeStatic
            [[nodiscard]] std::wstring PrintData() const
            {
                return L"";
            }

            template <typename PTCPROW>
            void StartTracking(const PTCPROW tcpRow) const
            {
                TCP_ESTATS_BANDWIDTH_RW_v0 rw{};
                rw.EnableCollectionInbound = TcpBoolOptEnabled;
                rw.EnableCollectionOutbound = TcpBoolOptEnabled;
                SetPerConnectionEstats<TcpConnectionEstatsBandwidth>(tcpRow, &rw);
            }
            template <typename PTCPROW>
            void UpdateData(const PTCPROW tcpRow)
            {
                TCP_ESTATS_BANDWIDTH_ROD_v0 rod{};
                if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsBandwidth>(tcpRow, &rod))
                {
                    // store data from this instance
                }
            }
        };

        template <>
        class EstatsDataTracking<TcpConnectionEstatsFineRtt>
        {
        public:
            static PCWSTR PrintHeader() noexcept
            {
                return L"";
            }
            // ReSharper disable once CppMemberFunctionMayBeStatic
            [[nodiscard]] std::wstring PrintData() const
            {
                return L"";
            }

            template <typename PTCPROW>
            void StartTracking(const PTCPROW tcpRow) const
            {
                TCP_ESTATS_FINE_RTT_RW_v0 rw{};
                rw.EnableCollection = TRUE;
                SetPerConnectionEstats<TcpConnectionEstatsFineRtt>(tcpRow, &rw);
            }
            template <typename PTCPROW>
            void UpdateData(const PTCPROW tcpRow)
            {
                TCP_ESTATS_FINE_RTT_ROD_v0 rod{};
                if (0 == GetPerConnectionDynamicEstats<TcpConnectionEstatsFineRtt>(tcpRow, &rod))
                {
                    // store data from this instance
                }
            }
        };

        template <TCP_ESTATS_TYPE TcpType>
        class EstatsDataPoint
        {
        public:
            static PCWSTR PrintAddressHeader() noexcept
            {
                return L"LocalAddress,RemoteAddress";
            }
            static PCWSTR PrintHeader() noexcept
            {
                return EstatsDataTracking<TcpType>::PrintHeader();
            }

            EstatsDataPoint(ctl::ctSockaddr local_addr, ctl::ctSockaddr remote_addr) noexcept :
                m_localAddr(std::move(local_addr)),
                m_remoteAddr(std::move(remote_addr))
            {
            }

            explicit EstatsDataPoint(MIB_TCPROW const* pTcpRow) noexcept :
                m_localAddr(AF_INET),
                m_remoteAddr(AF_INET)
            {
                m_localAddr.SetAddress(
                    reinterpret_cast<const PIN_ADDR>(const_cast<DWORD*>(&pTcpRow->dwLocalAddr)));
                m_localAddr.SetPort(
                    static_cast<unsigned short>(pTcpRow->dwLocalPort),
                    ctl::ByteOrder::NetworkOrder);

                m_remoteAddr.SetAddress(
                    reinterpret_cast<const PIN_ADDR>(const_cast<DWORD*>(&pTcpRow->dwRemoteAddr)));
                m_remoteAddr.SetPort(
                    static_cast<unsigned short>(pTcpRow->dwRemotePort),
                    ctl::ByteOrder::NetworkOrder);
            }

            explicit EstatsDataPoint(MIB_TCP6ROW const* pTcpRow) noexcept :
                m_localAddr(AF_INET6),
                m_remoteAddr(AF_INET6)
            {
                m_localAddr.SetAddress(&pTcpRow->LocalAddr);
                m_localAddr.SetPort(
                    static_cast<unsigned short>(pTcpRow->dwLocalPort),
                    ctl::ByteOrder::NetworkOrder);

                m_remoteAddr.SetAddress(&pTcpRow->RemoteAddr);
                m_remoteAddr.SetPort(
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
                if (m_localAddr < rhs.m_localAddr)
                {
                    return true;
                }
                if (m_localAddr == rhs.m_localAddr &&
                    m_remoteAddr < rhs.m_remoteAddr)
                {
                    return true;
                }
                return false;
            }
            bool operator==(const EstatsDataPoint<TcpType>& rhs) const noexcept
            {
                return (m_localAddr == rhs.m_localAddr) &&
                    (m_remoteAddr == rhs.m_remoteAddr);
            }

            std::wstring PrintAddresses() const
            {
                WCHAR local_string[ctl::IpStringMaxLength];
                (void)m_localAddr.WriteCompleteAddress(local_string);
                WCHAR remote_string[ctl::IpStringMaxLength];
                (void)m_remoteAddr.WriteCompleteAddress(remote_string);

                return ctl::ctString::ctFormatString(
                    L"%ws,%ws",
                    local_string,
                    remote_string);
            }
            std::wstring PrintData() const
            {
                return m_data.PrintData();
            }

            template <typename T>
            void StartTracking(T tcpRow) const noexcept
            {
                m_data.StartTracking(tcpRow);
            }

            template <typename T>
            void UpdateData(T tcpRow, ULONG currentCounter) const
            {
                m_latestCounter = currentCounter;
                m_data.UpdateData(tcpRow);
            }

            ctl::ctSockaddr LocalAddr() const noexcept
            {
                return m_localAddr;
            }
            ctl::ctSockaddr RemoteAddr() const noexcept
            {
                return m_remoteAddr;
            }

            ULONG LastestCounter() const noexcept
            {
                return m_latestCounter;
            }

        private:
            ctl::ctSockaddr m_localAddr;
            ctl::ctSockaddr m_remoteAddr;
            // the tracking object must be mutable because EstatsDataPoint instances
            // are stored in a std::set container, and access to objects in a std::set
            // must be const (since you are not allowed to modify std::set objects in-place)
            mutable EstatsDataTracking<TcpType> m_data;
            mutable ULONG m_latestCounter = 0;
        };
    } // namespace

    class ctsEstats
    {
    public:
        ctsEstats() :
            m_pathInfoWriter(L"EstatsPathInfo.csv"),
            m_receiveWindowWriter(L"EstatsReceiveWindow.csv"),
            m_senderCongestionWriter(L"EstatsSenderCongestion.csv"),
            m_tcpTable(c_StartingTableSize)
        {
            m_timer.reset(CreateThreadpoolTimer(TimerCallback, this, nullptr));
            THROW_LAST_ERROR_IF(!m_timer);
        }
        ctsEstats(const ctsEstats&) = delete;
        ctsEstats& operator=(const ctsEstats&) = delete;
        ctsEstats(ctsEstats&&) = delete;
        ctsEstats& operator=(ctsEstats&&) = delete;

        ~ctsEstats() noexcept
        {
            FlagTimerStopping();
            m_timer.reset();

            try
            {
                for (const auto& entry : m_pathInfoData)
                {
                    m_pathInfoWriter.write_row(entry.PrintAddresses() + entry.PrintData());
                }

                for (const auto& entry : m_localReceiveWindowData)
                {
                    const Details::EstatsDataPoint<TcpConnectionEstatsObsRec> matchingData(
                        entry.LocalAddr(),
                        entry.RemoteAddr());

                    const auto foundEntry = m_remoteReceiveWindowData.find(matchingData);
                    if (foundEntry != m_remoteReceiveWindowData.end())
                    {
                        m_receiveWindowWriter.write_row(entry.PrintAddresses() + entry.PrintData() + foundEntry->PrintData());
                    }
                }

                for (const auto& entry : m_senderCongestionData)
                {
                    const Details::EstatsDataPoint<TcpConnectionEstatsData> matchingData(
                        entry.LocalAddr(),
                        entry.RemoteAddr());

                    const auto foundEntry = m_byteTrackingData.find(matchingData);
                    if (foundEntry != m_byteTrackingData.end())
                    {
                        m_senderCongestionWriter.write_row(entry.PrintAddresses() + entry.PrintData() + foundEntry->PrintData());
                    }
                }
            }
            catch (const std::exception& e)
            {
                wprintf(L"~Estats exception: %ws\n", ctl::ctString::ctFormatException(e).c_str());
            }
        }

        bool start() noexcept
        {
            ResetTimerFlag();

            auto started = false;
            try
            {
                m_pathInfoWriter.create_file(
                    std::wstring(Details::EstatsDataPoint<TcpConnectionEstatsPath>::PrintAddressHeader()) +
                    L"," + Details::EstatsDataPoint<TcpConnectionEstatsPath>::PrintHeader());
                m_receiveWindowWriter.create_file(
                    std::wstring(Details::EstatsDataPoint<TcpConnectionEstatsRec>::PrintAddressHeader()) +
                    L"," + Details::EstatsDataPoint<TcpConnectionEstatsRec>::PrintHeader() +
                    L"," + Details::EstatsDataPoint<TcpConnectionEstatsObsRec>::PrintHeader());
                m_senderCongestionWriter.create_file(
                    std::wstring(Details::EstatsDataPoint<TcpConnectionEstatsSndCong>::PrintAddressHeader()) +
                    L"," + Details::EstatsDataPoint<TcpConnectionEstatsSndCong>::PrintHeader() +
                    L"," + Details::EstatsDataPoint<TcpConnectionEstatsData>::PrintHeader());

                started = UpdateEstats();
            }
            catch (const std::exception& e)
            {
                wprintf(L"ctsEstats::Start exception: %ws\n", ctl::ctString::ctFormatException(e).c_str());
                started = false;
            }

            if (!started)
            {
                FlagTimerStopping();
                m_timer.reset();
            }

            return started;
        }

    private:
        wil::unique_threadpool_timer m_timer;
        wil::critical_section m_timerLock;
        bool m_timersStopping = false;

        std::set<Details::EstatsDataPoint<TcpConnectionEstatsSynOpts>> m_synOptsData;
        std::set<Details::EstatsDataPoint<TcpConnectionEstatsData>> m_byteTrackingData;
        std::set<Details::EstatsDataPoint<TcpConnectionEstatsPath>> m_pathInfoData;
        std::set<Details::EstatsDataPoint<TcpConnectionEstatsRec>> m_localReceiveWindowData;
        std::set<Details::EstatsDataPoint<TcpConnectionEstatsObsRec>> m_remoteReceiveWindowData;
        std::set<Details::EstatsDataPoint<TcpConnectionEstatsSndCong>> m_senderCongestionData;

        ctsWriteDetails m_pathInfoWriter;
        ctsWriteDetails m_receiveWindowWriter;
        ctsWriteDetails m_senderCongestionWriter;

        // since updates are always serialized on a timer, just reuse the same buffer
        const ULONG c_StartingTableSize = 4096;
        std::vector<char> m_tcpTable;
        ULONG m_tableCounter = 0;
        const DWORD OneSecondTimeoutMs = 1000;
        FILETIME m_timerInterval = wil::filetime::from_int64(-1 * wil::filetime_duration::one_millisecond * OneSecondTimeoutMs);

        static void NTAPI TimerCallback(PTP_CALLBACK_INSTANCE, PVOID pContext, PTP_TIMER) noexcept
        {
            auto* pThis = static_cast<ctsEstats*>(pContext);
            pThis->UpdateEstats();
        }

        void ResetTimerFlag() noexcept
        {
            auto lock = m_timerLock.lock();
            m_timersStopping = false;
        }

        void FlagTimerStopping() noexcept
        {
            auto lock = m_timerLock.lock();
            m_timersStopping = true;
        }

        void ScheduleTimer() noexcept
        {
            auto lock = m_timerLock.lock();
            if (!m_timersStopping)
            {
                SetThreadpoolTimer(m_timer.get(), &m_timerInterval, 0, 0);
            }
        }

        bool UpdateEstats() noexcept try
        {
            bool accessDenied = false;
            ++m_tableCounter;
            try
            {
                // IPv4
                RefreshIPv4Data();
                auto* const pIpv4TcpTable = reinterpret_cast<PMIB_TCPTABLE>(&m_tcpTable[0]);
                for (unsigned count = 0; count < pIpv4TcpTable->dwNumEntries; ++count)
                {
                    auto* const tableEntry = &pIpv4TcpTable->table[count];
                    if (tableEntry->dwState == MIB_TCP_STATE_LISTEN ||
                        tableEntry->dwState == MIB_TCP_STATE_TIME_WAIT ||
                        tableEntry->dwState == MIB_TCP_STATE_DELETE_TCB)
                    {
                        continue;
                    }

                    try
                    {
                        UpdateDataPoints(m_synOptsData, tableEntry);
                        UpdateDataPoints(m_byteTrackingData, tableEntry);
                        UpdateDataPoints(m_pathInfoData, tableEntry);
                        UpdateDataPoints(m_localReceiveWindowData, tableEntry);
                        UpdateDataPoints(m_remoteReceiveWindowData, tableEntry);
                        UpdateDataPoints(m_senderCongestionData, tableEntry);
                    }
                    catch (const ctl::ctException& e)
                    {
                        if (e.why() == ERROR_ACCESS_DENIED)
                        {
                            accessDenied = true;
                            throw;
                        }
                    }
                }

                // IPv6
                RefreshIPv6Data();
                auto* const pIpv6TcpTable = reinterpret_cast<PMIB_TCP6TABLE>(&m_tcpTable[0]);
                for (unsigned count = 0; count < pIpv6TcpTable->dwNumEntries; ++count)
                {
                    auto* const tableEntry = &pIpv6TcpTable->table[count];
                    if (tableEntry->State == MIB_TCP_STATE_LISTEN ||
                        tableEntry->State == MIB_TCP_STATE_TIME_WAIT ||
                        tableEntry->State == MIB_TCP_STATE_DELETE_TCB)
                    {
                        continue;
                    }

                    try
                    {
                        UpdateDataPoints(m_synOptsData, tableEntry);
                        UpdateDataPoints(m_byteTrackingData, tableEntry);
                        UpdateDataPoints(m_pathInfoData, tableEntry);
                        UpdateDataPoints(m_localReceiveWindowData, tableEntry);
                        UpdateDataPoints(m_remoteReceiveWindowData, tableEntry);
                        UpdateDataPoints(m_senderCongestionData, tableEntry);
                    }
                    catch (const ctl::ctException& e)
                    {
                        if (e.why() == ERROR_ACCESS_DENIED)
                        {
                            accessDenied = true;
                            throw;
                        }
                    }
                }

                RemoveStaleDataPoints();
            }
            catch (const std::exception& e)
            {
                wprintf(L"ctsEstats::UpdateEstats exception: %ws\n", ctl::ctString::ctFormatException(e).c_str());
            }

            if (!accessDenied)
            {
                // schedule timer from this moment
                ScheduleTimer();
            }

            return !accessDenied;
        }
        catch (const std::exception& e)
        {
            wprintf(L"ctsEstats::UpdateEstats exception: %ws\n", ctl::ctString::ctFormatException(e).c_str());
            return false;
        }

        void RefreshIPv4Data()
        {
            m_tcpTable.resize(m_tcpTable.capacity());
            auto table_size = static_cast<DWORD>(m_tcpTable.size());
            ZeroMemory(&m_tcpTable[0], table_size);

            ULONG error = ::GetTcpTable(
                reinterpret_cast<PMIB_TCPTABLE>(&m_tcpTable[0]),
                &table_size,
                FALSE); // no need to sort them
            if (ERROR_INSUFFICIENT_BUFFER == error)
            {
                m_tcpTable.resize(table_size);
                error = ::GetTcpTable(
                    reinterpret_cast<PMIB_TCPTABLE>(&m_tcpTable[0]),
                    &table_size,
                    FALSE); // no need to sort them
            }
            if (error != ERROR_SUCCESS)
            {
                throw ctl::ctException(error, L"GetTcpTable", L"ctsEstats", false);
            }
        }
        void RefreshIPv6Data()
        {
            m_tcpTable.resize(m_tcpTable.capacity());
            auto table_size = static_cast<DWORD>(m_tcpTable.size());
            ZeroMemory(&m_tcpTable[0], table_size);

            ULONG error = ::GetTcp6Table(
                reinterpret_cast<PMIB_TCP6TABLE>(&m_tcpTable[0]),
                &table_size,
                FALSE); // no need to sort them
            if (ERROR_INSUFFICIENT_BUFFER == error)
            {
                m_tcpTable.resize(table_size);
                error = ::GetTcp6Table(
                    reinterpret_cast<PMIB_TCP6TABLE>(&m_tcpTable[0]),
                    &table_size,
                    FALSE); // no need to sort them
            }
            if (error != ERROR_SUCCESS)
            {
                throw ctl::ctException(error, L"GetTcp6Table", L"ctsEstats", false);
            }
        }

        template <TCP_ESTATS_TYPE TcpType, typename Mibtype>
        void UpdateDataPoints(std::set<Details::EstatsDataPoint<TcpType>>& data, Mibtype tableEntry)
        {
            const auto emplaceResults = data.emplace(tableEntry);
            // first == iterator inserted
            // second == bool if inserted
            if (emplaceResults.second)
            {
                emplaceResults.first->StartTracking(tableEntry);
            }
            emplaceResults.first->UpdateData(tableEntry, m_tableCounter);
        }

        void RemoveStaleDataPoints()
        {
            // walk the set of synOptsData. If an address wasn't found to have been updated
            // with the latest data, then we'll remove that tuple (local address + remote address)
            // from all the data sets and finish printing their rows
            auto foundInstance = std::find_if(
                std::begin(m_synOptsData),
                std::end(m_synOptsData),
                [&](const Details::EstatsDataPoint<TcpConnectionEstatsSynOpts>& dataPoint) noexcept
            {
                return dataPoint.LastestCounter() != m_tableCounter;
            });

            while (foundInstance != std::end(m_synOptsData))
            {
                const ctl::ctSockaddr localAddr(foundInstance->LocalAddr());
                const ctl::ctSockaddr remoteAddr(foundInstance->RemoteAddr());

                const auto synOptsInstance = foundInstance;
                const auto byteTrackingInstance = m_byteTrackingData.find(
                    Details::EstatsDataPoint<TcpConnectionEstatsData>(localAddr, remoteAddr));
                const auto fByteTrackingInstanceFound = byteTrackingInstance != m_byteTrackingData.end();

                const auto pathInfoInstance = m_pathInfoData.find(
                    Details::EstatsDataPoint<TcpConnectionEstatsPath>(localAddr, remoteAddr));
                const auto fPathInfoInstanceFound = pathInfoInstance != m_pathInfoData.end();

                const auto localReceiveWindowInstance = m_localReceiveWindowData.find(
                    Details::EstatsDataPoint<TcpConnectionEstatsRec>(localAddr, remoteAddr));
                const auto fLocalReceiveWindowInstanceFound = localReceiveWindowInstance != m_localReceiveWindowData.end();

                const auto remoteReceiveWindowInstance = m_remoteReceiveWindowData.find(
                    Details::EstatsDataPoint<TcpConnectionEstatsObsRec>(localAddr, remoteAddr));
                const auto fRemoteReceiveWindowInstanceFound = remoteReceiveWindowInstance != m_remoteReceiveWindowData.end();

                const auto senderCongestionInstance = m_senderCongestionData.find(
                    Details::EstatsDataPoint<TcpConnectionEstatsSndCong>(localAddr, remoteAddr));
                const auto fSenderCongestionInstanceFound = senderCongestionInstance != m_senderCongestionData.end();

                if (fPathInfoInstanceFound)
                {
                    m_pathInfoWriter.write_row(
                        pathInfoInstance->PrintAddresses() +
                        pathInfoInstance->PrintData());
                }

                if (fLocalReceiveWindowInstanceFound && fRemoteReceiveWindowInstanceFound)
                {
                    m_receiveWindowWriter.write_row(
                        localReceiveWindowInstance->PrintAddresses() +
                        localReceiveWindowInstance->PrintData() +
                        remoteReceiveWindowInstance->PrintData());
                }

                if (fSenderCongestionInstanceFound && fByteTrackingInstanceFound)
                {
                    m_senderCongestionWriter.write_row(
                        senderCongestionInstance->PrintAddresses() +
                        senderCongestionInstance->PrintData() +
                        byteTrackingInstance->PrintData());
                }

                m_synOptsData.erase(synOptsInstance);
                if (fByteTrackingInstanceFound)
                {
                    m_byteTrackingData.erase(byteTrackingInstance);
                }
                if (fLocalReceiveWindowInstanceFound)
                {
                    m_localReceiveWindowData.erase(localReceiveWindowInstance);
                }
                if (fRemoteReceiveWindowInstanceFound)
                {
                    m_remoteReceiveWindowData.erase(remoteReceiveWindowInstance);
                }
                if (fSenderCongestionInstanceFound)
                {
                    m_senderCongestionData.erase(senderCongestionInstance);
                }

                // update the while loop variable
                foundInstance = std::find_if(
                    std::begin(m_synOptsData),
                    std::end(m_synOptsData),
                    [&](const Details::EstatsDataPoint<TcpConnectionEstatsSynOpts>& dataPoint) noexcept
                {
                    return dataPoint.LastestCounter() != m_tableCounter;
                });
            } // while loop
        }
    };
} // namespace
