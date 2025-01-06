/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// ReSharper disable once CppUnusedIncludeDirective
#include "targetver.h"

// CRT headers
#include <algorithm>
#include <exception>
// os headers
#include <Windows.h>
// local headers
#include "ctsConfig.h"
#include "ctsSocketBroker.h"
// wil headers
#include <wil/stl.h>

using namespace ctsTraffic;
using namespace ctl;
using namespace std;


// global ptr for easing debugging
static ctsSocketBroker* g_socketBroker = nullptr;

static BOOL WINAPI CtrlBreakHandlerRoutine(DWORD) noexcept
{
    // handle all exit types - notify config that it's time to shut down
    ctsConfig::PrintSummary(L"\n  **** ctrl-break hit -- shutting down ****\n");
    Shutdown(ctsConfig::ExitProcessType::Rude);
    return TRUE;
}

int __cdecl wmain(int argc, _In_reads_z_(argc) const wchar_t** argv)
{
    WSADATA wsadata{};
    auto err = WSAStartup(WINSOCK_VERSION, &wsadata);
    if (err != 0)
    {
        wprintf(L"ctsTraffic failed at WSAStartup [%d]\n", err);
        return err;
    }

    try
    {
        if (!ctsConfig::Startup(argc, argv))
        {
            Shutdown(ctsConfig::ExitProcessType::Rude);
            err = ERROR_INVALID_DATA;
        }
    }
    catch (const invalid_argument& e)
    {
        ctsConfig::PrintErrorInfoOverride(wil::str_printf<std::wstring>(L"Invalid argument specified: %hs", e.what()).c_str());
        Shutdown(ctsConfig::ExitProcessType::Rude);
        err = ERROR_INVALID_DATA;
    }
    catch (const exception& e)
    {
        ctsConfig::PrintExceptionOverride(e.what());
        Shutdown(ctsConfig::ExitProcessType::Rude);
        err = ERROR_INVALID_DATA;
    }

    if (err == ERROR_INVALID_DATA)
    {
        wprintf(
            L"\n"
            L"For more information on command line options, specify -Help\n"
            L"ctsTraffic.exe -Help:[tcp] [udp] [logging] [advanced]\n"
            L"   -help:tcp : prints usage for TCP options\n"
            L"   -help:udp : prints usage for UDP options\n"
            L"   -help:logging : prints usage for logging options\n"
            L"   -help:advanced : prints the usage for advanced and experimental options\n"
            L"\n\n");
        return err;
    }

    try
    {
        if (!SetConsoleCtrlHandler(CtrlBreakHandlerRoutine, TRUE))
        {
            THROW_WIN32_MSG(GetLastError(), "SetConsoleCtrlHandler");
        }

        ctsConfig::PrintSettings();
        ctsConfig::PrintLegend();

        // set the start timer as close as possible to the start of the engine
        ctsConfig::g_configSettings->StartTimeMilliseconds = ctTimer::snap_qpc_as_msec();
        const auto broker(std::make_shared<ctsSocketBroker>());
        g_socketBroker = broker.get();
        broker->Start();

        wil::unique_threadpool_timer statusTimer;
        if (ctsConfig::g_configSettings->StatusUpdateFrequencyMilliseconds > 0)
        {
            statusTimer.reset(CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) { ctsConfig::PrintStatusUpdate(); }, nullptr, nullptr));
            THROW_LAST_ERROR_IF(!statusTimer);
            FILETIME zeroFiletime{};
            SetThreadpoolTimer(statusTimer.get(), &zeroFiletime, ctsConfig::g_configSettings->StatusUpdateFrequencyMilliseconds, 0);
        }

        if (!broker->Wait(ctsConfig::g_configSettings->TimeLimit > 0 ? ctsConfig::g_configSettings->TimeLimit : INFINITE))
        {
            ctsConfig::PrintSummary(L"\n  **** Time-limit of %lu reached ****\n", ctsConfig::g_configSettings->TimeLimit);
        }

        if (ctsConfig::g_configSettings->PauseAtEnd > 0)
        {
            // stop all status updates being printed to the console and pause before destroying the broker object
            statusTimer.reset();
            ctsConfig::PrintSummary(L"\n  **** Pausing-At-End for %lu milliseconds ****\n", ctsConfig::g_configSettings->PauseAtEnd);
            Sleep(ctsConfig::g_configSettings->PauseAtEnd);
        }
    }
    catch (const wil::ResultException& e)
    {
        ctsConfig::PrintExceptionOverride(e.what());
        Shutdown(ctsConfig::ExitProcessType::Rude);
        return e.GetErrorCode();
    }
    catch (const bad_alloc&)
    {
        ctsConfig::PrintErrorInfoOverride(L"ctsTraffic failed: Out of Memory");
        Shutdown(ctsConfig::ExitProcessType::Rude);
        return ERROR_OUTOFMEMORY;
    }
    catch (const exception& e)
    {
        ctsConfig::PrintErrorInfoOverride(wil::str_printf<std::wstring>(L"ctsTraffic failed: %hs", e.what()).c_str());
        Shutdown(ctsConfig::ExitProcessType::Rude);
        return ERROR_CANCELLED;
    }

    const auto totalTimeRun = ctTimer::snap_qpc_as_msec() - ctsConfig::g_configSettings->StartTimeMilliseconds;

    // write out the final status update
    ctsConfig::PrintStatusUpdate();

    Shutdown(ctsConfig::ExitProcessType::Normal);

    ctsConfig::PrintSummary(
        L"\n\n"
        L"  Historic Connection Statistics (all connections over the complete lifetime)  \n"
        L"-------------------------------------------------------------------------------\n"
        L"  SuccessfulConnections [%lld]   NetworkErrors [%lld]   ProtocolErrors [%lld]\n",
        ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.GetValue(),
        ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.GetValue(),
        ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.GetValue());

    if (ctsConfig::g_configSettings->Protocol == ctsConfig::ProtocolType::TCP)
    {
        ctsConfig::PrintSummary(
            L"\n"
            L"  Total Bytes Recv : %lld\n"
            L"  Total Bytes Sent : %lld\n",
            ctsConfig::g_configSettings->TcpStatusDetails.m_bytesRecv.GetValue(),
            ctsConfig::g_configSettings->TcpStatusDetails.m_bytesSent.GetValue());
    }
    else
    {
        // currently don't track UDP server stats
        if (!ctsConfig::IsListening())
        {
            const auto successfulFrames = ctsConfig::g_configSettings->UdpStatusDetails.m_successfulFrames.GetValue();
            const auto droppedFrames = ctsConfig::g_configSettings->UdpStatusDetails.m_droppedFrames.GetValue();
            const auto duplicateFrames = ctsConfig::g_configSettings->UdpStatusDetails.m_duplicateFrames.GetValue();
            const auto errorFrames = ctsConfig::g_configSettings->UdpStatusDetails.m_errorFrames.GetValue();

            const auto totalFrames =
                successfulFrames +
                droppedFrames +
                duplicateFrames +
                errorFrames;
            ctsConfig::PrintSummary(
                L"\n"
                L"  Total Bytes Recv : %lld\n"
                L"  Total Successful Frames : %lld (%f)\n"
                L"  Total Dropped Frames : %lld (%f)\n"
                L"  Total Duplicate Frames : %lld (%f)\n"
                L"  Total Error Frames : %lld (%f)\n",
                ctsConfig::g_configSettings->UdpStatusDetails.m_bitsReceived.GetValue() / 8LL,
                successfulFrames,
                totalFrames > 0 ? static_cast<double>(successfulFrames) / static_cast<double>(totalFrames) * 100.0 : 0.0,
                droppedFrames,
                totalFrames > 0 ? static_cast<double>(droppedFrames) / static_cast<double>(totalFrames) * 100.0 : 0.0,
                duplicateFrames,
                totalFrames > 0 ? static_cast<double>(duplicateFrames) / static_cast<double>(totalFrames) * 100.0 : 0.0,
                errorFrames,
                totalFrames > 0 ? static_cast<double>(errorFrames) / static_cast<double>(totalFrames) * 100.0 : 0.0);
        }
    }
    ctsConfig::PrintSummary(
        L"  Total Time : %lld ms.\n", totalTimeRun);

    int64_t errorCount =
        ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.GetValue() +
        ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.GetValue();

    errorCount = std::min<int64_t>(errorCount, MAXINT);
    return static_cast<int>(errorCount);
}
