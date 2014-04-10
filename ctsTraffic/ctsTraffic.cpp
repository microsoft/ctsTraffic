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
#include <wchar.h>
// OS headers
#include <Windows.h>
#include <algorithm>

// ctl headers
#include <ctException.hpp>
#include <ctString.hpp>
#include <ctThreadPoolTimer.hpp>

// local headers
#include "ctsConfig.h"
#include "ctsSocketBroker.h"

using namespace ctsTraffic;
using namespace ctl;

// 'main' signature found without threading model.
#pragma warning(disable:4447)

// global ptr for easing debugging
ctsSocketBroker* g_SocketBroker = nullptr;

BOOL WINAPI CtrlBreakHandlerRoutine(DWORD)
{
    // handle all exit types - notify config that it's time to shutdown
    ctsConfig::Shutdown();
    return TRUE;
}

int
__cdecl wmain(_In_ int argc, _In_reads_z_(argc) wchar_t** argv)
{
    WSADATA wsadata;
    int wsError = ::WSAStartup(WINSOCK_VERSION, &wsadata);
    if (wsError != 0) {
        DWORD gle = ::WSAGetLastError();
        wprintf(L"ctsTraffic failed at WSAStartup [%u]\n", gle);
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
        ctsTraffic::ctsConfig::PrintUsage();
        ctsConfig::Shutdown();
        return ERROR_INVALID_DATA;
    }
    catch (const std::invalid_argument& e) {
        ctsConfig::PrintErrorInfoOverride(L"Invalid argument specified: %S", e.what());
        ctsTraffic::ctsConfig::PrintUsage();
        ctsConfig::Shutdown();
        return ERROR_INVALID_DATA;
    }
    catch (const std::exception& e) {
        ctsConfig::PrintExceptionOverride(e);
        ctsTraffic::ctsConfig::PrintUsage();
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
        ctsConfig::Settings->StartTimeMilliseconds = ctl::ctTimer::snap_qpc_msec();
        ctsSocketBroker broker;
        g_SocketBroker = &broker;

        ctl::ctThreadpoolTimer status_timer;
        status_timer.schedule_reoccuring(ctsConfig::PrintStatusUpdate, 0LL, ctsConfig::Settings->StatusUpdateFrequencyMilliseconds);
        if (!broker.wait(ctsConfig::Settings->TimeLimit > 0 ? ctsConfig::Settings->TimeLimit : INFINITE)) {
            throw std::runtime_error("Timelimit exceeded : aborting");
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
    catch (const std::bad_alloc&) {
        ctsConfig::PrintErrorInfo(L"ctsTraffic failed: Out of Memory");
        return ERROR_OUTOFMEMORY;
    }
    catch (const std::exception& e) {
        ctsConfig::PrintErrorInfo(L"ctsTraffic failed: %S", e.what());
        ctsConfig::Shutdown();
        return ERROR_CANCELLED;
    }

    // write out the final status update
    ctsConfig::PrintStatusUpdate();

    ctsConfig::Shutdown();

    ctsConfig::PrintSummary(
        L"\n\n"
        L"  Historic Connection Statistics (all connections over the complete lifetime)  \n"
        L"-------------------------------------------------------------------------------\n"
        L"SuccessfulConnections [%lld]   NetworkErrors [%lld]   ProtocolErrors [%lld]\n",
        ctsConfig::Settings->HistoricConnectionDetails.successful_connections.get(),
        ctsConfig::Settings->HistoricConnectionDetails.connection_errors.get(),
        ctsConfig::Settings->HistoricConnectionDetails.protocol_errors.get());

    long long error_count =
        ctsConfig::Settings->HistoricConnectionDetails.connection_errors.get() +
        ctsConfig::Settings->HistoricConnectionDetails.protocol_errors.get();
    if (error_count > MAXINT) {
        error_count = MAXINT;
    }
    return static_cast<int>(error_count);
}

#pragma warning(default:4447)
