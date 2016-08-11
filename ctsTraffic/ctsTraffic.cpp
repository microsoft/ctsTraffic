/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include "targetver.h"

// CRT headers
#include <stdio.h>
#include <exception>
// os headers
#include <Windows.h>
// ctl headers
#include <ctException.hpp>
#include <ctThreadPoolTimer.hpp>
// local headers
#include "ctsConfig.h"
#include "ctsSocketBroker.h"

using namespace ctsTraffic;
using namespace ctl;
using namespace std;


// global ptr for easing debugging
ctsSocketBroker* g_SocketBroker = nullptr;

BOOL WINAPI CtrlBreakHandlerRoutine(DWORD)
{
    // handle all exit types - notify config that it's time to shutdown
    ctsConfig::Shutdown();
    return TRUE;
}

int
__cdecl wmain(_In_ int argc, _In_reads_z_(argc) const wchar_t** argv)
{
    WSADATA wsadata;
    int wsError = ::WSAStartup(WINSOCK_VERSION, &wsadata);
    if (wsError != 0) {
        DWORD gle = ::WSAGetLastError();
        ::wprintf(L"ctsTraffic failed at WSAStartup [%u]\n", gle);
        return gle;
    }

    try {
        if (!ctsConfig::Startup(argc, argv)) {
            ctsConfig::Shutdown();
            return ERROR_INVALID_DATA;
        }
    }
    catch (const ctsSafeIntException& e) {
        ctsConfig::PrintErrorInfoOverride(L"Invalid parameters : %s\n", ctsPrintSafeIntException(e));
        ctsConfig::PrintUsage();
        ctsConfig::Shutdown();
        return ERROR_INVALID_DATA;
    }
    catch (const invalid_argument& e) {
        ctsConfig::PrintErrorInfoOverride(L"Invalid argument specified: %S", e.what());
        ctsConfig::PrintUsage();
        ctsConfig::Shutdown();
        return ERROR_INVALID_DATA;
    }
    catch (const exception& e) {
        ctsConfig::PrintExceptionOverride(e);
        ctsConfig::PrintUsage();
        ctsConfig::Shutdown();
        return ERROR_INVALID_DATA;
    }

    try {
        if (!::SetConsoleCtrlHandler(CtrlBreakHandlerRoutine, TRUE)) {
            throw ctException(::GetLastError(), L"SetConsoleCtrlHandler", false);
        }

        ctsConfig::PrintSettings();
        ctsConfig::PrintLegend();

        // set the start timer as close as possible to the start of the engine
        ctsConfig::Settings->StartTimeMilliseconds = ctTimer::snap_qpc_as_msec();
        std::shared_ptr<ctsSocketBroker> broker(std::make_shared<ctsSocketBroker>());
        g_SocketBroker = broker.get();
        broker->start();

        ctThreadpoolTimer status_timer;
        status_timer.schedule_reoccuring(ctsConfig::PrintStatusUpdate, 0LL, ctsConfig::Settings->StatusUpdateFrequencyMilliseconds);
        if (!broker->wait(ctsConfig::Settings->TimeLimit > 0 ? ctsConfig::Settings->TimeLimit : INFINITE)) {
            ctsConfig::PrintSummary(L"\n ** Timelimit of %lu reached **\n", static_cast<unsigned long>(ctsConfig::Settings->TimeLimit));
        }
    }
    catch (const ctsSafeIntException& e) {
        ctsConfig::PrintErrorInfoOverride(L"ctsTraffic failed when converting integers : %s\n", ctsPrintSafeIntException(e));
        ctsConfig::Shutdown();
        return ERROR_INVALID_DATA;
    }
    catch (const ctException& e) {
        ctsConfig::PrintException(e);
        ctsConfig::Shutdown();
        return (e.why() == 0) ? ERROR_CANCELLED : e.why();
    }
    catch (const bad_alloc&) {
        ctsConfig::PrintErrorInfo(L"[%.3f] ctsTraffic failed: Out of Memory", ctsConfig::GetStatusTimeStamp());
        ctsConfig::Shutdown();
        return ERROR_OUTOFMEMORY;
    }
    catch (const exception& e) {
        ctsConfig::PrintErrorInfo(L"[%.3f] ctsTraffic failed: %S", ctsConfig::GetStatusTimeStamp(), e.what());
        ctsConfig::Shutdown();
        return ERROR_CANCELLED;
    }

    auto total_time_run = ctTimer::snap_qpc_as_msec() - ctsConfig::Settings->StartTimeMilliseconds;

    // write out the final status update
    ctsConfig::PrintStatusUpdate();

    ctsConfig::Shutdown();

    ctsConfig::PrintSummary(
        L"\n\n"
        L"  Historic Connection Statistics (all connections over the complete lifetime)  \n"
        L"-------------------------------------------------------------------------------\n"
        L"  SuccessfulConnections [%lld]   NetworkErrors [%lld]   ProtocolErrors [%lld]\n",
        ctsConfig::Settings->ConnectionStatusDetails.successful_completion_count.get(),
        ctsConfig::Settings->ConnectionStatusDetails.connection_error_count.get(),
        ctsConfig::Settings->ConnectionStatusDetails.protocol_error_count.get());

    if (ctsConfig::Settings->Protocol == ctsConfig::ProtocolType::TCP) {
        ctsConfig::PrintSummary(
            L"\n"
            L"  Total Bytes Recv : %lld\n"
            L"  Total Bytes Sent : %lld\n",
            ctsConfig::Settings->TcpStatusDetails.bytes_recv.get(),
            ctsConfig::Settings->TcpStatusDetails.bytes_sent.get());
    } else {
        // currently don't track UDP server stats
        if (!ctsConfig::IsListening()) {
            ctsConfig::PrintSummary(
                L"\n"
                L"  Total Bytes Recv : %lld\n"
                L"  Total Successful Frames : %lld\n"
                L"  Total Dropped Frames : %lld\n"
                L"  Total Duplicate Frames : %lld\n"
                L"  Total Error Frames : %lld\n",
                ctsConfig::Settings->UdpStatusDetails.bits_received.get() / 8LL,
                ctsConfig::Settings->UdpStatusDetails.successful_frames.get(),
                ctsConfig::Settings->UdpStatusDetails.dropped_frames.get(),
                ctsConfig::Settings->UdpStatusDetails.duplicate_frames.get(),
                ctsConfig::Settings->UdpStatusDetails.error_frames.get());
        }
    }
    ctsConfig::PrintSummary(
        L"  Total Time : %lld ms.\n",
        static_cast<long long>(total_time_run));

    long long error_count =
        ctsConfig::Settings->ConnectionStatusDetails.connection_error_count.get() +
        ctsConfig::Settings->ConnectionStatusDetails.protocol_error_count.get();
    if (error_count > MAXINT) {
        error_count = MAXINT;
    }
    return static_cast<int>(error_count);
}
