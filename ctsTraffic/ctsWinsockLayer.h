/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <memory>
#include <functional>

// using wil::network to pull in all necessary networking headers
#include <wil/network.h>

// project headers
#include "ctsIOTask.hpp"
#include "ctsSocket.h"

// ReSharper disable CppInconsistentNaming

//
// These functions encapsulate making Winsock API calls
// - primarily facilitating unit testing of interface logic that calls through Winsock
//   but also simplifying the logic behind the code to make reasoning over the code more straight forward
//

// this is only defined in Windows 10 RS2 and later
#ifndef SIO_TCP_INFO
#define SIO_TCP_INFO _WSAIORW(IOC_VENDOR,39)
#endif

namespace ctsTraffic
{
    // this is only defined in the public header for Windows 10 RS2 and later
    enum TCPSTATE : std::uint8_t
    {
        TCPSTATE_CLOSED,
        TCPSTATE_LISTEN,
        TCPSTATE_SYN_SENT,
        TCPSTATE_SYN_RCVD,
        TCPSTATE_ESTABLISHED,
        TCPSTATE_FIN_WAIT_1,
        TCPSTATE_FIN_WAIT_2,
        TCPSTATE_CLOSE_WAIT,
        TCPSTATE_CLOSING,
        TCPSTATE_LAST_ACK,
        TCPSTATE_TIME_WAIT,
        TCPSTATE_MAX
    };

    // this is only defined in the public header for Windows 10 RS2 and later
    struct TCP_INFO_v0
    {
        TCPSTATE State;
        ULONG Mss;
        ULONG64 ConnectionTimeMs;
        BOOLEAN TimestampsEnabled;
        ULONG RttUs;
        ULONG MinRttUs;
        ULONG BytesInFlight;
        ULONG Cwnd;
        ULONG SndWnd;
        ULONG RcvWnd;
        ULONG RcvBuf;
        ULONG64 BytesOut;
        ULONG64 BytesIn;
        ULONG BytesReordered;
        ULONG BytesRetrans;
        ULONG FastRetrans;
        ULONG DupAcksIn;
        ULONG TimeoutEpisodes;
        UCHAR SynRetrans;
    };

    // this is only defined in the public header for Windows 10 RS5 and later
    struct TCP_INFO_v1
    {
        TCPSTATE State;
        ULONG Mss;
        ULONG64 ConnectionTimeMs;
        BOOLEAN TimestampsEnabled;
        ULONG RttUs;
        ULONG MinRttUs;
        ULONG BytesInFlight;
        ULONG Cwnd;
        ULONG SndWnd;
        ULONG RcvWnd;
        ULONG RcvBuf;
        ULONG64 BytesOut;
        ULONG64 BytesIn;
        ULONG BytesReordered;
        ULONG BytesRetrans;
        ULONG FastRetrans;
        ULONG DupAcksIn;
        ULONG TimeoutEpisodes;
        UCHAR SynRetrans;

        //
        // Info about the limiting factor in send throughput.
        //
        // States:
        // -Rwin: peer's receive window.
        // -Cwnd: congestion window.
        // -Snd: app not writing enough data to its socket.
        //
        // Per-state statistics:
        // -Trans: number of transitions into the state.
        // -Time: time spent in the state in milliseconds.
        // -Bytes: number of bytes sent while in the state.
        //
        // These fields match those in TCP_ESTATS_SND_CONG_ROD.
        //
        ULONG SndLimTransRwin;
        ULONG SndLimTimeRwin;
        ULONG64 SndLimBytesRwin;
        ULONG SndLimTransCwnd;
        ULONG SndLimTimeCwnd;
        ULONG64 SndLimBytesCwnd;
        ULONG SndLimTransSnd;
        ULONG SndLimTimeSnd;
        ULONG64 SndLimBytesSnd;
    };

    struct wsIOResult
    {
        uint32_t m_errorCode = 0;
        DWORD m_bytesTransferred = 0;

        wsIOResult() noexcept = default;

        explicit wsIOResult(uint32_t error) noexcept :
            m_errorCode(error)
        {
        }
    };

    wsIOResult ctsWSARecvFrom(
        const std::shared_ptr<ctsSocket>& sharedSocket,
        SOCKET socket,
        const ctsTask& task,
        std::function<void(OVERLAPPED*)>&& callback) noexcept;

    wsIOResult ctsWSASendTo(
        const std::shared_ptr<ctsSocket>& sharedSocket,
        SOCKET socket,
        const ctsTask& task,
        std::function<void(OVERLAPPED*)>&& callback) noexcept;

    // Set LINGER options to force an RST when the socket is closed
    wsIOResult ctsSetLingerToResetSocket(SOCKET socket) noexcept;
}
