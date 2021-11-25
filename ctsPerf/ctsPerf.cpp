/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
// ReSharper disable CppClangTidyClangDiagnosticExitTimeDestructors
#include <cstdio>
#include <cwchar>
#include <vector>
#include <string>
#include <memory>
// os headers
#include <Windows.h>
#include <WinSock2.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
// ctl headers
#include <ctString.hpp>
#include <ctWmiInitialize.hpp>
#include <ctWmiPerformance.hpp>
// project headers
#include "ctsWriteDetails.h"
#include "ctsEstats.h"

using namespace std;
using namespace ctl;

HANDLE g_break = nullptr;
const ctWmiService* g_wmi = nullptr;

BOOL WINAPI BreakHandlerRoutine(DWORD) noexcept
{
    // regardless of the break type, signal to exit
    ::SetEvent(g_break);
    return TRUE;
}

constexpr PCWSTR c_usageStatement =
    L"ctsPerf.exe usage::\n"
    L" #### <time to run (in seconds)>  [default is 60 seconds]\n"
	L" -Networking [will enable performance and reliability related Network counters]\n"
	L" -Estats [will enable ESTATS tracking for all TCP connections]\n"
	L" -MeanOnly  [will save memory by not storing every data point, only a sum and mean\n"
    L"\n"
    L" [optionally the specific interface description can be specified\n"
    L"  by default *all* interface counters are collected]\n"
    L"  note: the Interface Description can be found from the powershell cmdlet Get-NetAdapter\n"
    L"        or by running ctsPerf.exe and viewing the names from the log file\n"
    L"  -InterfaceDescription:##########\n"
    L"\n"
    L" [optionally one of two process identifiers]\n"
    L"  by default is no process tracking\n"
    L"  -process:<process name>\n"
    L"  -pid:<process id>\n"
    L"\n\n"
    L"For example:\n"
    L"> ctsPerf.exe\n"
    L"  -- will capture processor and memory counters for the default 60 seconds\n"
    L"\n"
    L"> ctsPerf.exe -Networking\n"
    L"  -- will capture processor, memory, network adapter, network interface, IP, TCP, and UDP counters\n"
    L"\n"
    L"> ctsPerf.exe 300 -process:outlook.exe\n"
    L"  -- will capture processor and memory + process counters for outlook.exe for 300 seconds"
    L"\n"
    L"> ctsPerf.exe -pid:2048\n"
    L"  -- will capture processor and memory + process counters for process id 2048 for 60 seconds"
    L"\n";

// 0 is a possible process ID
constexpr DWORD c_uninitializedProcessId = 0xffffffff;

ctWmiPerformance InstantiateProcessorCounters();
ctWmiPerformance InstantiateMemoryCounters();
ctWmiPerformance InstantiateNetworkAdapterCounters(const std::wstring& trackInterfaceDescription);
ctWmiPerformance InstantiateNetworkInterfaceCounters(const std::wstring& trackInterfaceDescription);
ctWmiPerformance InstantiateIPCounters();
ctWmiPerformance InstantiateTCPCounters();
ctWmiPerformance InstantiateUDPCounters();
ctWmiPerformance InstantiatePerProcessByNameCounters(const std::wstring& trackProcess);
ctWmiPerformance InstantiatePerProcessByPIDCounters(DWORD processId);

void DeleteProcessorCounters() noexcept;
void DeleteMemoryCounters() noexcept;
void DeleteNetworkAdapterCounters() noexcept;
void DeleteNetworkInterfaceCounters() noexcept;
void DeleteIPCounters() noexcept;
void DeleteTCPCounters() noexcept;
void DeleteUDPCounters() noexcept;
void DeletePerProcessCounters() noexcept;

void DeleteAllCounters() noexcept
{
    DeleteProcessorCounters();
    DeleteMemoryCounters();
    DeleteNetworkAdapterCounters();
    DeleteNetworkInterfaceCounters();
    DeleteIPCounters();
    DeleteTCPCounters();
    DeleteUDPCounters();
    DeletePerProcessCounters();
}

void ProcessProcessorCounters(ctsPerf::ctsWriteDetails& writer);
void ProcessMemoryCounters(ctsPerf::ctsWriteDetails& writer);
void ProcessNetworkAdapterCounters(ctsPerf::ctsWriteDetails& writer);
void ProcessNetworkInterfaceCounters(ctsPerf::ctsWriteDetails& writer);
void ProcessIPCounters(ctsPerf::ctsWriteDetails& writer);
void ProcessTCPCounters(ctsPerf::ctsWriteDetails& writer);
void ProcessUDPCounters(ctsPerf::ctsWriteDetails& writer);
void ProcessPerProcessCounters(const wstring& trackProcess, DWORD processId, ctsPerf::ctsWriteDetails& writer);

PCWSTR g_fileName = L"ctsPerf.csv";
PCWSTR g_networkingFilename = L"ctsNetworking.csv";
PCWSTR g_processFilename = L"ctsPerProcess.csv";

bool g_meanOnly = false;

int __cdecl wmain(_In_ int argc, _In_reads_z_(argc) const wchar_t** argv)
{
    WSADATA wsadata;
    const auto wsError = ::WSAStartup(WINSOCK_VERSION, &wsadata);
    if (wsError != 0)
    {
        ::wprintf(L"ctsPerf failed at WSAStartup [%d]\n", wsError);
        return wsError;
    }

    // create a notification event to signal if the user wants to exit early
    g_break = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (g_break == nullptr)
    {
        const auto gle = ::GetLastError();
        wprintf(L"Out of resources -- cannot initialize (CreateEvent) (%u)\n", gle);
        return gle;
    }

    if (!::SetConsoleCtrlHandler(BreakHandlerRoutine, TRUE))
    {
        const auto gle = ::GetLastError();
        wprintf(L"Out of resources -- cannot initialize (SetConsoleCtrlHandler) (%u)\n", gle);
        return gle;
    }

    auto trackNetworking = false;
    auto trackEstats = false;

    wstring trackInterfaceDescription;
    wstring trackProcess;
    auto processId = c_uninitializedProcessId;
    DWORD timeToRunMs = 60000; // default to 60 seconds

    for (DWORD argCount = argc; argCount > 1; --argCount)
    {
        if (ctString::ctOrdinalStartsWithCaseInsensative(argv[argCount - 1], L"-process:"))
        {
            trackProcess = argv[argCount - 1];

            // strip off the "process:" preface to the string
            const auto endOfToken = ranges::find(trackProcess, L':');
            trackProcess.erase(trackProcess.begin(), endOfToken + 1);

            // the performance counter does not look at the extension, so remove .exe if it's there
            if (ctString::ctOrdinalEndsWithCaseInsensative(trackProcess, L".exe"))
            {
                trackProcess.erase(trackProcess.end() - 4, trackProcess.end());
            }
            if (trackProcess.empty())
            {
                wprintf(L"Incorrect option: %ws\n", argv[argCount - 1]);
                wprintf(c_usageStatement);
                return 1;
            }

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[argCount - 1], L"-pid:"))
        {
            wstring pidString(argv[argCount - 1]);

            // strip off the "pid:" preface to the string
            const auto endOfToken = ranges::find(pidString, L':');
            pidString.erase(pidString.begin(), endOfToken + 1);

            // the user could have specified zero, which happens to be what is returned from wcstoul on error
            if (pidString == L"0")
            {
                processId = 0;

            }
            else
            {
                processId = ::wcstoul(pidString.c_str(), nullptr, 10);
                if (processId == 0 || processId == ULONG_MAX)
                {
                    wprintf(L"Incorrect option: %ws\n", argv[argCount - 1]);
                    wprintf(c_usageStatement);
                    return 1;
                }
            }

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[argCount - 1], L"-estats"))
        {
            trackEstats = true;

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[argCount - 1], L"-Networking"))
        {
            trackNetworking = true;

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[argCount - 1], L"-InterfaceDescription:"))
        {
            trackInterfaceDescription = argv[argCount - 1];

            // strip off the "-InterfaceDescription:" preface to the string
            const auto endOfToken = ranges::find(trackInterfaceDescription, L':');
            trackInterfaceDescription.erase(trackInterfaceDescription.begin(), endOfToken + 1);

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[argCount - 1], L"-MeanOnly"))
        {
            g_meanOnly = true;

        }
        else
        {
            const auto timeToRun = wcstoul(argv[argCount - 1], nullptr, 10);
            if (timeToRun == 0 || timeToRun == ULONG_MAX)
            {
                wprintf(L"Incorrect option: %ws\n", argv[argCount - 1]);
                wprintf(c_usageStatement);
                return 1;
            }
            timeToRunMs = timeToRun * 1000;
            if (timeToRunMs < timeToRun)
            {
                wprintf(L"Incorrect option: %ws\n", argv[argCount - 1]);
                wprintf(c_usageStatement);
                return 1;
            }
        }
    }

    const auto trackPerProcess = !trackProcess.empty() || processId != c_uninitializedProcessId;

    if (timeToRunMs <= 5000)
    {
        wprintf(L"ERROR: Must run over 5 seconds to have enough samples for analysis\n");
        wprintf(c_usageStatement);
        return 1;
    }

    try
    {
        ctsPerf::ctsEstats estats;
        if (trackEstats)
        {
            if (estats.start())
            {
                wprintf(L"Enabling ESTATS\n");
            }
            else
            {
                wprintf(L"ESTATS cannot be started - verify running as Administrator\n");
                return 1;
            }
        }

        wprintf(L"Instantiating WMI Performance objects (this can take a few seconds)\n");
        const auto coInit = wil::CoInitializeEx();
        const ctWmiService wmi(L"root\\cimv2");
        g_wmi = &wmi;

        auto deleteAllCounters = wil::scope_exit([&]() noexcept { DeleteAllCounters(); });

        ctsPerf::ctsWriteDetails cpuwriter(g_fileName);
        cpuwriter.CreateFile(g_meanOnly);

        ctsPerf::ctsWriteDetails networkWriter(g_networkingFilename);
        if (trackNetworking)
        {
            networkWriter.CreateFile(g_meanOnly);
        }

        ctsPerf::ctsWriteDetails processWriter(g_processFilename);
        if (trackPerProcess)
        {
            processWriter.CreateFile(g_meanOnly);
        }

        wprintf(L".");

        // create a perf counter objects to maintain these counters
        std::vector<ctWmiPerformance> performanceVector;

        performanceVector.emplace_back(InstantiateProcessorCounters());
        performanceVector.emplace_back(InstantiateMemoryCounters());

        if (trackNetworking)
        {
            performanceVector.emplace_back(InstantiateNetworkAdapterCounters(trackInterfaceDescription));
            performanceVector.emplace_back(InstantiateNetworkInterfaceCounters(trackInterfaceDescription));
            performanceVector.emplace_back(InstantiateIPCounters());
            performanceVector.emplace_back(InstantiateTCPCounters());
            performanceVector.emplace_back(InstantiateUDPCounters());
        }

        if (!trackProcess.empty())
        {
            performanceVector.emplace_back(InstantiatePerProcessByNameCounters(trackProcess));

        }
        else if (processId != c_uninitializedProcessId)
        {
            performanceVector.emplace_back(InstantiatePerProcessByPIDCounters(processId));
        }

        wprintf(L"\nStarting counters : will run for %lu seconds\n (hit ctrl-c to exit early) ...\n\n", timeToRunMs / 1000UL);
        for (auto& perfObject : performanceVector)
        {
            perfObject.start_all_counters(1000);
        }

        ::WaitForSingleObject(g_break, timeToRunMs);

        wprintf(L"Stopping counters ....\n\n");
        for (auto& perfObject : performanceVector)
        {
            perfObject.stop_all_counters();
        }

        ProcessProcessorCounters(cpuwriter);
        ProcessMemoryCounters(cpuwriter);

        if (trackNetworking)
        {
            ProcessNetworkAdapterCounters(networkWriter);
            ProcessNetworkInterfaceCounters(networkWriter);
            ProcessIPCounters(networkWriter);
            ProcessTCPCounters(networkWriter);
            ProcessUDPCounters(networkWriter);
        }

        if (trackPerProcess)
        {
            ProcessPerProcessCounters(trackProcess, processId, processWriter);
        }
    }
    catch (const wil::ResultException& e)
    {
        wprintf(L"ctsPerf exception: %hs\n", e.what());
        return 1;
    }
    catch (const exception& e)
    {
        wprintf(L"ctsPerf exception: %hs\n", e.what());
        return 1;
    }

    CloseHandle(g_break);

    return 0;
}


/****************************************************************************************************/
/*                                         Processor                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_processorTime;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_processorPercentOfMax;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_processorPercentDpcTime;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_processorDpcsQueuedPerSecond;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_processorPercentPrivilegedTime;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_processorPercentUserTime;
ctWmiPerformance InstantiateProcessorCounters()
{
    ctWmiPerformance performanceCounter(*g_wmi);

    // create objects for system counters we care about
    g_processorTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Processor,
        L"PercentProcessorTime",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_processorTime);
    wprintf(L".");

    g_processorPercentOfMax = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::Processor,
        L"PercentofMaximumFrequency",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_processorPercentOfMax);
    wprintf(L".");

	g_processorPercentDpcTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
		ctWmiEnumClassName::Processor,
		L"PercentDPCTime",
		g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performanceCounter.add_counter(g_processorPercentDpcTime);
	wprintf(L".");

	g_processorDpcsQueuedPerSecond = ctCreatePerfCounter<ULONG>(
        *g_wmi,
		ctWmiEnumClassName::Processor,
		L"DPCsQueuedPersec",
		g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performanceCounter.add_counter(g_processorDpcsQueuedPerSecond);
	wprintf(L".");

	g_processorPercentPrivilegedTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
		ctWmiEnumClassName::Processor,
		L"PercentPrivilegedTime",
		g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performanceCounter.add_counter(g_processorPercentPrivilegedTime);
	wprintf(L".");

	g_processorPercentUserTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
		ctWmiEnumClassName::Processor,
		L"PercentUserTime",
		g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performanceCounter.add_counter(g_processorPercentUserTime);
	wprintf(L".");

    return performanceCounter;
}
void DeleteProcessorCounters() noexcept
{
    g_processorTime.reset();
    g_processorPercentOfMax.reset();
	g_processorPercentDpcTime.reset();
	g_processorDpcsQueuedPerSecond.reset();
	g_processorPercentPrivilegedTime.reset();
	g_processorPercentUserTime.reset();
}
void ProcessProcessorCounters(ctsPerf::ctsWriteDetails& writer)
{
    ctWmiEnumerate enumProcessors(*g_wmi);
    enumProcessors.query(L"SELECT * FROM Win32_PerfFormattedData_Counters_ProcessorInformation");
    if (enumProcessors.begin() == enumProcessors.end()) {
        throw exception("Unable to find any processors to report on - querying Win32_PerfFormattedData_Counters_ProcessorInformation returned nothing");
    }
	vector<ULONGLONG> ullData;
	vector<ULONG> ulData;

	for (const auto& processor : enumProcessors) {
		wstring name;
		processor.get(L"Name", &name);

		// processor name strings look like "0,1" when there are more than one cores
		// need to replace the comma so the csv will print correctly
		writer.WriteRow(
			wil::str_printf<std::wstring>(
				L"Processor %ws",
				ctString::ctReplaceAllCopy(name, L",", L" - ").c_str()));

        const auto processor_range = g_processorTime->reference_range(name.c_str());
		vector<ULONGLONG> processorTimeVector(processor_range.first, processor_range.second);

        const auto percent_range = g_processorPercentOfMax->reference_range(name.c_str());
		vector<ULONG> processorPercentVector(percent_range.first, percent_range.second);

        const auto percent_dpc_time_range = g_processorPercentDpcTime->reference_range(name.c_str());
        const auto dpcs_queued_per_second_range = g_processorDpcsQueuedPerSecond->reference_range(name.c_str());
        const auto processor_percent_privileged_time_range = g_processorPercentPrivilegedTime->reference_range(name.c_str());
        const auto processor_percent_user_time_range = g_processorPercentUserTime->reference_range(name.c_str());

		if (g_meanOnly) {
		    // ReSharper disable once CppUseAuto
			vector<ULONGLONG> normalizedProcessorTime(processorTimeVector);

			// convert to a percentage
			auto calculatedProcessorTime = static_cast<double>(processorTimeVector[3]) / 100.0;
			calculatedProcessorTime *= processorPercentVector[3] / 100.0;
			normalizedProcessorTime[3] = static_cast<ULONGLONG>(calculatedProcessorTime * 100UL);

			writer.WriteMean(
				L"Processor",
				L"Raw CPU Usage",
				processorTimeVector);

			writer.WriteMean(
				L"Processor",
				L"Normalized CPU Usage (Raw * PercentofMaximumFrequency)",
				normalizedProcessorTime);

			ullData.assign(percent_dpc_time_range.first, percent_dpc_time_range.second);
			writer.WriteMean(
				L"Processor",
				L"Percent DPC Time",
				ullData);

			ulData.assign(dpcs_queued_per_second_range.first, dpcs_queued_per_second_range.second);
			writer.WriteMean(
				L"Processor",
				L"DPCs Queued Per Second",
				ulData);

			ullData.assign(processor_percent_privileged_time_range.first, processor_percent_privileged_time_range.second);
			writer.WriteMean(
				L"Processor",
				L"Percent Privileged Time",
				ullData);

			ullData.assign(processor_percent_user_time_range.first, processor_percent_user_time_range.second);
			writer.WriteMean(
				L"Processor",
				L"Percent User Time",
				ullData);
		}
		else {
			vector<ULONG> normalizedProcessorTime;
			// produce the raw % as well as the 'normalized' % based off of the PercentofMaximumFrequency
			auto percentageIterator(processorPercentVector.begin());
			for (const auto& processorData : processorTimeVector) {
				// convert to a percentage
				auto calculatedProcessorTime = static_cast<double>(processorData) / 100.0;
				calculatedProcessorTime *= *percentageIterator / 100.0;

				normalizedProcessorTime.push_back(static_cast<ULONG>(calculatedProcessorTime * 100UL));
				++percentageIterator;
			}

			writer.WriteDetails(
				L"Processor",
				L"Raw CPU Usage",
				processorTimeVector);

			writer.WriteDetails(
				L"Processor",
				L"Normalized CPU Usage (Raw * PercentofMaximumFrequency)",
				normalizedProcessorTime);

			ullData.assign(percent_dpc_time_range.first, percent_dpc_time_range.second);
			writer.WriteDetails(
				L"Processor",
				L"Percent DPC Time",
				ullData);

			ulData.assign(dpcs_queued_per_second_range.first, dpcs_queued_per_second_range.second);
			writer.WriteDetails(
				L"Processor",
				L"DPCs Queued Per Second",
				ulData);

			ullData.assign(processor_percent_privileged_time_range.first, processor_percent_privileged_time_range.second);
			writer.WriteDetails(
				L"Processor",
				L"Percent Privileged Time",
				ullData);

			ullData.assign(processor_percent_user_time_range.first, processor_percent_user_time_range.second);
			writer.WriteDetails(
				L"Processor",
				L"Percent User Time",
				ullData);
		}
	}

	writer.WriteEmptyRow();
}

/****************************************************************************************************/
/*                                            Memory                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_pagedPoolBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_nonPagedPoolBytes;
ctWmiPerformance InstantiateMemoryCounters()
{
    ctWmiPerformance performanceCounter(*g_wmi);
    g_pagedPoolBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Memory,
        L"PoolPagedBytes",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_pagedPoolBytes);
    wprintf(L".");

    g_nonPagedPoolBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Memory,
        L"PoolNonpagedBytes",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_nonPagedPoolBytes);
    wprintf(L".");

    return performanceCounter;
}
void DeleteMemoryCounters() noexcept
{
    g_pagedPoolBytes.reset();
    g_nonPagedPoolBytes.reset();
}
void ProcessMemoryCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;
    const auto paged_pool_range = g_pagedPoolBytes->reference_range();
    const auto non_paged_pool_range = g_nonPagedPoolBytes->reference_range();

    if (g_meanOnly) {
        ullData.assign(paged_pool_range.first, paged_pool_range.second);
        writer.WriteMean(
            L"Memory",
            L"PoolPagedBytes",
            ullData);

        ullData.assign(non_paged_pool_range.first, non_paged_pool_range.second);
        writer.WriteMean(
            L"Memory",
            L"PoolNonpagedBytes",
            ullData);
    } else {
        ullData.assign(paged_pool_range.first, paged_pool_range.second);
        writer.WriteDetails(
            L"Memory",
            L"PoolPagedBytes",
            ullData);

        ullData.assign(non_paged_pool_range.first, non_paged_pool_range.second);
        writer.WriteDetails(
            L"Memory",
            L"PoolNonpagedBytes",
            ullData);
    }
}

/****************************************************************************************************/
/*                                     NetworkAdapter                                               */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkAdapterTotalBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkAdapterOffloadedConnections;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkAdapterPacketsOutboundDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkAdapterPacketsOutboundErrors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkAdapterPacketsReceivedDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkAdapterPacketsReceivedErrors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkAdapterPacketsPerSecond;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkAdapterActiveRscConnections;
ctWmiPerformance InstantiateNetworkAdapterCounters(const std::wstring& trackInterfaceDescription)
{
    ctWmiPerformance performanceCounter(*g_wmi);

    g_networkAdapterTotalBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"BytesTotalPersec",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        g_networkAdapterTotalBytes->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkAdapterTotalBytes);
    wprintf(L".");

    g_networkAdapterOffloadedConnections = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"OffloadedConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkAdapterOffloadedConnections->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkAdapterOffloadedConnections);
    wprintf(L".");

    g_networkAdapterPacketsOutboundDiscarded = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkAdapterPacketsOutboundDiscarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkAdapterPacketsOutboundDiscarded);
    wprintf(L".");

    g_networkAdapterPacketsOutboundErrors = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsOutboundErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkAdapterPacketsOutboundErrors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkAdapterPacketsOutboundErrors);
    wprintf(L".");

    g_networkAdapterPacketsReceivedDiscarded = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkAdapterPacketsReceivedDiscarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkAdapterPacketsReceivedDiscarded);
    wprintf(L".");

    g_networkAdapterPacketsReceivedErrors = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkAdapterPacketsReceivedErrors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkAdapterPacketsReceivedErrors);
    wprintf(L".");

    g_networkAdapterPacketsPerSecond = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsPersec",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        g_networkAdapterPacketsPerSecond->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkAdapterPacketsPerSecond);
    wprintf(L".");

    g_networkAdapterActiveRscConnections = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"TCPActiveRSCConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkAdapterActiveRscConnections->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkAdapterActiveRscConnections);
    wprintf(L".");

    return performanceCounter;
}
void DeleteNetworkAdapterCounters() noexcept
{
    g_networkAdapterTotalBytes.reset();
    g_networkAdapterOffloadedConnections.reset();
    g_networkAdapterPacketsOutboundDiscarded.reset();
    g_networkAdapterPacketsOutboundErrors.reset();
    g_networkAdapterPacketsReceivedDiscarded.reset();
    g_networkAdapterPacketsReceivedErrors.reset();
    g_networkAdapterPacketsPerSecond.reset();
    g_networkAdapterActiveRscConnections.reset();
}
void ProcessNetworkAdapterCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;

    // there is no great way to find the 'Name' for each network interface tracked
    // - it is not guaranteed to match anything from NetAdapter or NetIPInteface
    // - making a single query directly here to at least get the names
    ctWmiEnumerate enumAdapter(*g_wmi);
    enumAdapter.query(L"SELECT * FROM Win32_PerfFormattedData_Tcpip_NetworkAdapter");
    if (enumAdapter.begin() == enumAdapter.end()) {
        throw exception("Unable to find an adapter to report on - querying Win32_PerfFormattedData_Tcpip_NetworkAdapter returned nothing");
    }

	writer.WriteRow(L"NetworkAdapter");
    for (const auto& adapter : enumAdapter) {
        wstring name;
        adapter.get(L"Name", &name);

        auto networkRange = g_networkAdapterPacketsPerSecond->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        if (g_meanOnly) {
            writer.WriteMean(
                L"NetworkAdapter",
                wil::str_printf<std::wstring>(
                    L"PacketsPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.WriteDetails(
                L"NetworkAdapter",
                wil::str_printf<std::wstring>(
                    L"PacketsPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        }
        networkRange = g_networkAdapterTotalBytes->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        if (g_meanOnly) {
            writer.WriteMean(
                L"NetworkAdapter",
                wil::str_printf<std::wstring>(
                    L"BytesTotalPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.WriteDetails(
                L"NetworkAdapter",
                wil::str_printf<std::wstring>(
                    L"BytesTotalPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        }

        networkRange = g_networkAdapterOffloadedConnections->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"OffloadedConnections for interface %ws",
                name.c_str()).c_str(),
            ullData);

        networkRange = g_networkAdapterActiveRscConnections->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"TCPActiveRSCConnections for interface %ws",
                name.c_str()).c_str(),
            ullData);

        networkRange = g_networkAdapterPacketsOutboundDiscarded->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"PacketsOutboundDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        networkRange = g_networkAdapterPacketsOutboundErrors->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"PacketsOutboundErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

        networkRange = g_networkAdapterPacketsReceivedDiscarded->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        networkRange = g_networkAdapterPacketsReceivedErrors->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

		writer.WriteEmptyRow();
    }
}

/****************************************************************************************************/
/*                                     NetworkInterface                                             */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkInterfaceTotalBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkInterfacePacketsOutboundDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkInterfacePacketsOutboundErrors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkInterfacePacketsReceivedDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkInterfacePacketsReceivedErrors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_networkInterfacePacketsReceivedUnknown;
ctWmiPerformance InstantiateNetworkInterfaceCounters(const std::wstring& trackInterfaceDescription)
{
    ctWmiPerformance performanceCounter(*g_wmi);

    g_networkInterfaceTotalBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"BytesTotalPerSec",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        g_networkInterfaceTotalBytes->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkInterfaceTotalBytes);
    wprintf(L".");

    g_networkInterfacePacketsOutboundDiscarded = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkInterfacePacketsOutboundDiscarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkInterfacePacketsOutboundDiscarded);
    wprintf(L".");

    g_networkInterfacePacketsOutboundErrors = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsOutboundErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkInterfacePacketsOutboundErrors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkInterfacePacketsOutboundErrors);
    wprintf(L".");

    g_networkInterfacePacketsReceivedDiscarded = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkInterfacePacketsReceivedDiscarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkInterfacePacketsReceivedDiscarded);
    wprintf(L".");

    g_networkInterfacePacketsReceivedErrors = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkInterfacePacketsReceivedErrors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkInterfacePacketsReceivedErrors);
    wprintf(L".");

    g_networkInterfacePacketsReceivedUnknown = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsReceivedUnknown",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        g_networkInterfacePacketsReceivedUnknown->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performanceCounter.add_counter(g_networkInterfacePacketsReceivedUnknown);
    wprintf(L".");

    return performanceCounter;
}
void DeleteNetworkInterfaceCounters() noexcept
{
    g_networkInterfaceTotalBytes.reset();
    g_networkInterfacePacketsOutboundDiscarded.reset();
    g_networkInterfacePacketsOutboundErrors.reset();
    g_networkInterfacePacketsReceivedDiscarded.reset();
    g_networkInterfacePacketsReceivedErrors.reset();
    g_networkInterfacePacketsReceivedUnknown.reset();
}
void ProcessNetworkInterfaceCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;

    // there is no great way to find the 'Name' for each network interface tracked
    // - it is not guaranteed to match anything from NetAdapter or NetIPInterface
    // - making a single query directly here to at least get the names
    ctWmiEnumerate enumAdapter(*g_wmi);
    enumAdapter.query(L"SELECT * FROM Win32_PerfFormattedData_Tcpip_NetworkInterface");
    if (enumAdapter.begin() == enumAdapter.end()) {
        throw exception("Unable to find an adapter to report on - querying Win32_PerfFormattedData_Tcpip_NetworkInterface returned nothing");
    }

	writer.WriteRow(L"NetworkInterface");
    for (const auto& adapter : enumAdapter) {
        wstring name;
        adapter.get(L"Name", &name);

        const auto byte_range = g_networkInterfaceTotalBytes->reference_range(name.c_str());
        ullData.assign(byte_range.first, byte_range.second);
        if (g_meanOnly) {
            writer.WriteMean(
                L"NetworkInterface",
                wil::str_printf<std::wstring>(
                    L"BytesTotalPerSec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.WriteDetails(
                L"NetworkInterface",
                wil::str_printf<std::wstring>(
                    L"BytesTotalPerSec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        }
        auto networkRange = g_networkInterfacePacketsOutboundDiscarded->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsOutboundDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        networkRange = g_networkInterfacePacketsOutboundErrors->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsOutboundErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

        networkRange = g_networkInterfacePacketsReceivedDiscarded->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        networkRange = g_networkInterfacePacketsReceivedErrors->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

        networkRange = g_networkInterfacePacketsReceivedUnknown->reference_range(name.c_str());
        ullData.assign(networkRange.first, networkRange.second);
        writer.WriteDifference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedUnknown for interface %ws",
                name.c_str()).c_str(),
            ullData);

		writer.WriteEmptyRow();
    }
}


/****************************************************************************************************/
/*                                        TCPIP IPv4                                                */
/*                                        TCPIP IPv6                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv4OutboundDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv4OutboundNoRoute;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv4ReceivedAddressErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv4ReceivedDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv4ReceivedHeaderErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv4ReceivedUnknownProtocol;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv4FragmentReassemblyFailures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv4FragmentationFailures;

shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv6OutboundDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv6OutboundNoRoute;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv6ReceivedAddressErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv6ReceivedDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv6ReceivedHeaderErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv6ReceivedUnknownProtocol;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv6FragmentReassemblyFailures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipIpv6FragmentationFailures;
ctWmiPerformance InstantiateIPCounters()
{
    ctWmiPerformance performanceCounter(*g_wmi);

    g_tcpipIpv4OutboundDiscarded = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv4OutboundDiscarded);
    wprintf(L".");

    g_tcpipIpv4OutboundNoRoute = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsOutboundNoRoute",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv4OutboundNoRoute);
    wprintf(L".");

    g_tcpipIpv4ReceivedAddressErrors = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedAddressErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv4ReceivedAddressErrors);
    wprintf(L".");

    g_tcpipIpv4ReceivedDiscarded = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv4ReceivedDiscarded);
    wprintf(L".");

    g_tcpipIpv4ReceivedHeaderErrors = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedHeaderErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv4ReceivedHeaderErrors);
    wprintf(L".");

    g_tcpipIpv4ReceivedUnknownProtocol = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedUnknownProtocol",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv4ReceivedUnknownProtocol);
    wprintf(L".");

    g_tcpipIpv4FragmentReassemblyFailures = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"FragmentReassemblyFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv4FragmentReassemblyFailures);
    wprintf(L".");

    g_tcpipIpv4FragmentationFailures = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"FragmentationFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv4FragmentationFailures);
    wprintf(L".");

    g_tcpipIpv6OutboundDiscarded = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv6OutboundDiscarded);
    wprintf(L".");

    g_tcpipIpv6OutboundNoRoute = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsOutboundNoRoute",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv6OutboundNoRoute);
    wprintf(L".");

    g_tcpipIpv6ReceivedAddressErrors = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedAddressErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv6ReceivedAddressErrors);
    wprintf(L".");

    g_tcpipIpv6ReceivedDiscarded = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv6ReceivedDiscarded);
    wprintf(L".");

    g_tcpipIpv6ReceivedHeaderErrors = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedHeaderErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv6ReceivedHeaderErrors);
    wprintf(L".");

    g_tcpipIpv6ReceivedUnknownProtocol = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedUnknownProtocol",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv6ReceivedUnknownProtocol);
    wprintf(L".");

    g_tcpipIpv6FragmentReassemblyFailures = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"FragmentReassemblyFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv6FragmentReassemblyFailures);
    wprintf(L".");

    g_tcpipIpv6FragmentationFailures = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"FragmentationFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipIpv6FragmentationFailures);
    wprintf(L".");

    return performanceCounter;
}
void DeleteIPCounters() noexcept
{
    g_tcpipIpv4OutboundDiscarded.reset();
    g_tcpipIpv4OutboundNoRoute.reset();
    g_tcpipIpv4ReceivedAddressErrors.reset();
    g_tcpipIpv4ReceivedDiscarded.reset();
    g_tcpipIpv4ReceivedHeaderErrors.reset();
    g_tcpipIpv4ReceivedUnknownProtocol.reset();
    g_tcpipIpv4FragmentReassemblyFailures.reset();
    g_tcpipIpv4FragmentationFailures.reset();

    g_tcpipIpv6OutboundDiscarded.reset();
    g_tcpipIpv6OutboundNoRoute.reset();
    g_tcpipIpv6ReceivedAddressErrors.reset();
    g_tcpipIpv6ReceivedDiscarded.reset();
    g_tcpipIpv6ReceivedHeaderErrors.reset();
    g_tcpipIpv6ReceivedUnknownProtocol.reset();
    g_tcpipIpv6FragmentReassemblyFailures.reset();
    g_tcpipIpv6FragmentationFailures.reset();
}
void ProcessIPCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONG> ulData;

	writer.WriteRow(L"TCPIP - IPv4");

    auto networkLossRange = g_tcpipIpv4OutboundDiscarded->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv4",
        L"DatagramsOutboundDiscarded",
        ulData);

    networkLossRange = g_tcpipIpv4OutboundNoRoute->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv4",
        L"DatagramsOutboundNoRoute",
        ulData);

    networkLossRange = g_tcpipIpv4ReceivedAddressErrors->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedAddressErrors",
        ulData);

    networkLossRange = g_tcpipIpv4ReceivedDiscarded->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedDiscarded",
        ulData);

    networkLossRange = g_tcpipIpv4ReceivedHeaderErrors->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedHeaderErrors",
        ulData);

    networkLossRange = g_tcpipIpv4ReceivedUnknownProtocol->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedUnknownProtocol",
        ulData);

    networkLossRange = g_tcpipIpv4FragmentReassemblyFailures->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv4",
        L"FragmentReassemblyFailures",
        ulData);

    networkLossRange = g_tcpipIpv4FragmentationFailures->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv4",
        L"FragmentationFailures",
        ulData);

    networkLossRange = g_tcpipIpv6OutboundDiscarded->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv6",
        L"DatagramsOutboundDiscarded",
        ulData);

    networkLossRange = g_tcpipIpv6OutboundNoRoute->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv6",
        L"DatagramsOutboundNoRoute",
        ulData);

    networkLossRange = g_tcpipIpv6ReceivedAddressErrors->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedAddressErrors",
        ulData);

    networkLossRange = g_tcpipIpv6ReceivedDiscarded->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedDiscarded",
        ulData);

    networkLossRange = g_tcpipIpv6ReceivedHeaderErrors->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedHeaderErrors",
        ulData);

    networkLossRange = g_tcpipIpv6ReceivedUnknownProtocol->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedUnknownProtocol",
        ulData);

    networkLossRange = g_tcpipIpv6FragmentReassemblyFailures->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv6",
        L"FragmentReassemblyFailures",
        ulData);

    networkLossRange = g_tcpipIpv6FragmentationFailures->reference_range();
    ulData.assign(networkLossRange.first, networkLossRange.second);
    writer.WriteDifference(
        L"TCPIP - IPv6",
        L"FragmentationFailures",
        ulData);

	writer.WriteEmptyRow();
}


/****************************************************************************************************/
/*                                        TCPIP TCPv4                                                */
/*                                        TCPIP TCPv6                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipTcpv4ConnectionsEstablished;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipTcpv6ConnectionsEstablished;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipTcpv4ConnectionFailures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipTcpv6ConnectionFailures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipTcpv4ConnectionsReset;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipTcpv6ConnectionsReset;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_winsockBspRejectedConnections;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_winsockBspRejectedConnectionsPerSec;
ctWmiPerformance InstantiateTCPCounters()
{
    ctWmiPerformance performanceCounter(*g_wmi);

    g_tcpipTcpv4ConnectionsEstablished = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipTcpv4,
        L"ConnectionsEstablished",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_tcpipTcpv4ConnectionsEstablished);
    wprintf(L".");

    g_tcpipTcpv6ConnectionsEstablished = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipTcpv6,
        L"ConnectionsEstablished",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_tcpipTcpv6ConnectionsEstablished);
    wprintf(L".");

    g_tcpipTcpv4ConnectionFailures = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipTcpv4,
        L"ConnectionFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipTcpv4ConnectionFailures);
    wprintf(L".");

    g_tcpipTcpv6ConnectionFailures = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipTcpv6,
        L"ConnectionFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipTcpv6ConnectionFailures);
    wprintf(L".");

    g_tcpipTcpv4ConnectionsReset = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipTcpv4,
        L"ConnectionsReset",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipTcpv4ConnectionsReset);
    wprintf(L".");

    g_tcpipTcpv6ConnectionsReset = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipTcpv6,
        L"ConnectionsReset",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipTcpv6ConnectionsReset);
    wprintf(L".");

    g_winsockBspRejectedConnections = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"RejectedConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_winsockBspRejectedConnections);
    wprintf(L".");

    g_winsockBspRejectedConnectionsPerSec = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"RejectedConnectionsPersec",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_winsockBspRejectedConnectionsPerSec);
    wprintf(L".");

    return performanceCounter;
}
void DeleteTCPCounters() noexcept
{
    g_tcpipTcpv4ConnectionsEstablished.reset();
    g_tcpipTcpv6ConnectionsEstablished.reset();
    g_tcpipTcpv4ConnectionFailures.reset();
    g_tcpipTcpv6ConnectionFailures.reset();
    g_tcpipTcpv4ConnectionsReset.reset();
    g_tcpipTcpv6ConnectionsReset.reset();
    g_winsockBspRejectedConnections.reset();
    g_winsockBspRejectedConnectionsPerSec.reset();
}
void ProcessTCPCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONG> ulData;

	writer.WriteRow(L"TCPIP - TCPv4");
    auto networkRange = g_tcpipTcpv4ConnectionsEstablished->reference_range();
    ulData.assign(networkRange.first, networkRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            L"TCPIP - TCPv4",
            L"ConnectionsEstablished",
            ulData);
    } else {
        writer.WriteDetails(
            L"TCPIP - TCPv4",
            L"ConnectionsEstablished",
            ulData);
    }

    networkRange = g_tcpipTcpv6ConnectionsEstablished->reference_range();
    ulData.assign(networkRange.first, networkRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            L"TCPIP - TCPv6",
            L"ConnectionsEstablished",
            ulData);
    }
    else {
        writer.WriteDetails(
            L"TCPIP - TCPv6",
            L"ConnectionsEstablished",
            ulData);
    }

    networkRange = g_tcpipTcpv4ConnectionFailures->reference_range();
    ulData.assign(networkRange.first, networkRange.second);
    writer.WriteDifference(
        L"TCPIP - TCPv4",
        L"ConnectionFailures",
        ulData);

    networkRange = g_tcpipTcpv6ConnectionFailures->reference_range();
    ulData.assign(networkRange.first, networkRange.second);
    writer.WriteDifference(
        L"TCPIP - TCPv6",
        L"ConnectionFailures",
        ulData);

    networkRange = g_tcpipTcpv4ConnectionsReset->reference_range();
    ulData.assign(networkRange.first, networkRange.second);
    writer.WriteDifference(
        L"TCPIP - TCPv4",
        L"ConnectionsReset",
        ulData);

    networkRange = g_tcpipTcpv6ConnectionsReset->reference_range();
    ulData.assign(networkRange.first, networkRange.second);
    writer.WriteDifference(
        L"TCPIP - TCPv6",
        L"ConnectionsReset",
        ulData);

    networkRange = g_winsockBspRejectedConnections->reference_range();
    ulData.assign(networkRange.first, networkRange.second);
    writer.WriteDifference(
        L"Winsock",
        L"RejectedConnections",
        ulData);

    networkRange = g_winsockBspRejectedConnectionsPerSec->reference_range();
    ulData.assign(networkRange.first, networkRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            L"Winsock",
            L"RejectedConnectionsPersec",
            ulData);
    } else {
        writer.WriteDetails(
            L"Winsock",
            L"RejectedConnectionsPersec",
            ulData);
    }

	writer.WriteEmptyRow();
}


/****************************************************************************************************/
/*                                        TCPIP UDPv4                                                */
/*                                        TCPIP UDPv6                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipUdpv4NoportPerSec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipUdpv4ReceivedErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipUdpv4DatagramsPerSec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipUdpv6NoportPerSec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipUdpv6ReceivedErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_tcpipUdpv6DatagramsPerSec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_winsockBspDroppedDatagrams;
shared_ptr<ctWmiPerformanceCounter<ULONG>> g_winsockBspDroppedDatagramsPerSecond;
ctWmiPerformance InstantiateUDPCounters()
{
    ctWmiPerformance performanceCounter(*g_wmi);

    g_tcpipUdpv4NoportPerSec = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipUdpv4,
        L"DatagramsNoPortPersec",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_tcpipUdpv4NoportPerSec);
    wprintf(L".");

    g_tcpipUdpv4ReceivedErrors = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipUdpv4,
        L"DatagramsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipUdpv4ReceivedErrors);
    wprintf(L".");

    g_tcpipUdpv4DatagramsPerSec = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipUdpv4,
        L"DatagramsPersec",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_tcpipUdpv4DatagramsPerSec);
    wprintf(L".");

    g_tcpipUdpv6NoportPerSec = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipUdpv6,
        L"DatagramsNoPortPersec",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_tcpipUdpv6NoportPerSec);
    wprintf(L".");

    g_tcpipUdpv6ReceivedErrors = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipUdpv6,
        L"DatagramsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_tcpipUdpv6ReceivedErrors);
    wprintf(L".");

    g_tcpipUdpv6DatagramsPerSec = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::TcpipUdpv6,
        L"DatagramsPersec",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_tcpipUdpv6DatagramsPerSec);
    wprintf(L".");

    g_winsockBspDroppedDatagrams = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"DroppedDatagrams",
        ctWmiPerformanceCollectionType::FirstLast);
    performanceCounter.add_counter(g_winsockBspDroppedDatagrams);
    wprintf(L".");

    g_winsockBspDroppedDatagramsPerSecond = ctCreatePerfCounter<ULONG>(
        *g_wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"DroppedDatagramsPersec",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performanceCounter.add_counter(g_winsockBspDroppedDatagramsPerSecond);
    wprintf(L".");

    return performanceCounter;
}
void DeleteUDPCounters() noexcept
{
    g_tcpipUdpv4NoportPerSec.reset();
    g_tcpipUdpv4ReceivedErrors.reset();
    g_tcpipUdpv4DatagramsPerSec.reset();
    g_tcpipUdpv6NoportPerSec.reset();
    g_tcpipUdpv6ReceivedErrors.reset();
    g_tcpipUdpv6DatagramsPerSec.reset();
    g_winsockBspDroppedDatagrams.reset();
    g_winsockBspDroppedDatagramsPerSecond.reset();
}
void ProcessUDPCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONG> ulData;

	writer.WriteRow(L"TCPIP - UDPv4");

    auto udpRange = g_tcpipUdpv4NoportPerSec->reference_range();
    ulData.assign(udpRange.first, udpRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            L"TCPIP - UDPv4",
            L"DatagramsNoPortPersec",
            ulData);
    } else {
        writer.WriteDetails(
            L"TCPIP - UDPv4",
            L"DatagramsNoPortPersec",
            ulData);
    }

    udpRange = g_tcpipUdpv4DatagramsPerSec->reference_range();
    ulData.assign(udpRange.first, udpRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            L"TCPIP - UDPv4",
            L"DatagramsPersec",
            ulData);
    } else {
        writer.WriteDetails(
            L"TCPIP - UDPv4",
            L"DatagramsPersec",
            ulData);
    }

	udpRange = g_tcpipUdpv4ReceivedErrors->reference_range();
	ulData.assign(udpRange.first, udpRange.second);
	writer.WriteDifference(
		L"TCPIP - UDPv4",
		L"DatagramsReceivedErrors",
		ulData);

	writer.WriteEmptyRow();
	writer.WriteRow(L"TCPIP - UDPv6");

	udpRange = g_tcpipUdpv6NoportPerSec->reference_range();
    ulData.assign(udpRange.first, udpRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            L"TCPIP - UDPv6",
            L"DatagramsNoPortPersec",
            ulData);
    } else {
        writer.WriteDetails(
            L"TCPIP - UDPv6",
            L"DatagramsNoPortPersec",
            ulData);
    }

    udpRange = g_tcpipUdpv6DatagramsPerSec->reference_range();
    ulData.assign(udpRange.first, udpRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            L"TCPIP - UDPv6",
            L"DatagramsPersec",
            ulData);
    } else {
        writer.WriteDetails(
            L"TCPIP - UDPv6",
            L"DatagramsPersec",
            ulData);
    }

    udpRange = g_tcpipUdpv6ReceivedErrors->reference_range();
    ulData.assign(udpRange.first, udpRange.second);
    writer.WriteDifference(
        L"TCPIP - UDPv6",
        L"DatagramsReceivedErrors",
        ulData);

	writer.WriteEmptyRow();
	writer.WriteRow(L"Winsock Datagrams");

    udpRange = g_winsockBspDroppedDatagrams->reference_range();
    ulData.assign(udpRange.first, udpRange.second);
    writer.WriteDifference(
        L"Winsock",
        L"DroppedDatagrams",
        ulData);

    udpRange = g_winsockBspDroppedDatagramsPerSecond->reference_range();
    ulData.assign(udpRange.first, udpRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            L"Winsock",
            L"DroppedDatagramsPersec",
            ulData);
    } else {
        writer.WriteDetails(
            L"Winsock",
            L"DroppedDatagramsPersec",
            ulData);
    }

	writer.WriteEmptyRow();
}


shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_perProcessPrivilegedTime;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_perProcessProcessorTime;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_perProcessUserTime;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_perProcessPrivateBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_perProcessVirtualBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> g_perProcessWorkingSet;
ctWmiPerformance InstantiatePerProcessByNameCounters(const std::wstring& trackProcess)
{
    ctWmiPerformance performanceCounter(*g_wmi);

    // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
    g_perProcessPrivilegedTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"PercentPrivilegedTime",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessPrivilegedTime->add_filter(L"Name", trackProcess.c_str());
    performanceCounter.add_counter(g_perProcessPrivilegedTime);
    wprintf(L".");

    g_perProcessProcessorTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"PercentProcessorTime",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessProcessorTime->add_filter(L"Name", trackProcess.c_str());
    performanceCounter.add_counter(g_perProcessProcessorTime);
    wprintf(L".");

    g_perProcessUserTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"PercentUserTime",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessUserTime->add_filter(L"Name", trackProcess.c_str());
    performanceCounter.add_counter(g_perProcessUserTime);
    wprintf(L".");

    g_perProcessPrivateBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"PrivateBytes",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessPrivateBytes->add_filter(L"Name", trackProcess.c_str());
    performanceCounter.add_counter(g_perProcessPrivateBytes);
    wprintf(L".");

    g_perProcessVirtualBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"VirtualBytes",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessVirtualBytes->add_filter(L"Name", trackProcess.c_str());
    performanceCounter.add_counter(g_perProcessVirtualBytes);
    wprintf(L".");

    g_perProcessWorkingSet = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"WorkingSet",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessWorkingSet->add_filter(L"Name", trackProcess.c_str());
    performanceCounter.add_counter(g_perProcessWorkingSet);
    wprintf(L".");

    return performanceCounter;
}
ctWmiPerformance InstantiatePerProcessByPIDCounters(const DWORD processId)
{
    ctWmiPerformance performanceCounter(*g_wmi);

    // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
    g_perProcessPrivilegedTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"PercentPrivilegedTime",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessPrivilegedTime->add_filter(L"IDProcess", processId);
    performanceCounter.add_counter(g_perProcessPrivilegedTime);
    wprintf(L".");

    g_perProcessProcessorTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"PercentProcessorTime",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessProcessorTime->add_filter(L"IDProcess", processId);
    performanceCounter.add_counter(g_perProcessProcessorTime);
    wprintf(L".");

    g_perProcessUserTime = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"PercentUserTime",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessUserTime->add_filter(L"IDProcess", processId);
    performanceCounter.add_counter(g_perProcessUserTime);
    wprintf(L".");

    g_perProcessPrivateBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"PrivateBytes",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessPrivateBytes->add_filter(L"IDProcess", processId);
    performanceCounter.add_counter(g_perProcessPrivateBytes);
    wprintf(L".");

    g_perProcessVirtualBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"VirtualBytes",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessVirtualBytes->add_filter(L"IDProcess", processId);
    performanceCounter.add_counter(g_perProcessVirtualBytes);
    wprintf(L".");

    g_perProcessWorkingSet = ctCreatePerfCounter<ULONGLONG>(
        *g_wmi,
        ctWmiEnumClassName::Process,
        L"WorkingSet",
        g_meanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    g_perProcessWorkingSet->add_filter(L"IDProcess", processId);
    performanceCounter.add_counter(g_perProcessWorkingSet);
    wprintf(L".");

    return performanceCounter;
}
void DeletePerProcessCounters() noexcept
{
    g_perProcessPrivilegedTime.reset();
    g_perProcessProcessorTime.reset();
    g_perProcessUserTime.reset();
    g_perProcessPrivateBytes.reset();
    g_perProcessVirtualBytes.reset();
    g_perProcessWorkingSet.reset();
}
void ProcessPerProcessCounters(const wstring& trackProcess, const DWORD processId, ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;

    wstring counterClassname;
    if (!trackProcess.empty()) {
        wstring fullname(trackProcess);
        fullname += L".exe";
        counterClassname = wil::str_printf<std::wstring>(L"Process (%ws)", fullname.c_str());

    } else {
        counterClassname = wil::str_printf<std::wstring>(L"Process (pid %u)", processId);
    }

    auto perProcessRange = g_perProcessPrivilegedTime->reference_range();
    ullData.assign(perProcessRange.first, perProcessRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            counterClassname.c_str(),
            L"PercentPrivilegedTime",
            ullData);
    } else {
        writer.WriteDetails(
            counterClassname.c_str(),
            L"PercentPrivilegedTime",
            ullData);
    }

    perProcessRange = g_perProcessProcessorTime->reference_range();
    ullData.assign(perProcessRange.first, perProcessRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            counterClassname.c_str(),
            L"PercentProcessorTime",
            ullData);
    } else {
        writer.WriteDetails(
            counterClassname.c_str(),
            L"PercentProcessorTime",
            ullData);
    }

    perProcessRange = g_perProcessUserTime->reference_range();
    ullData.assign(perProcessRange.first, perProcessRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            counterClassname.c_str(),
            L"PercentUserTime",
            ullData);
    } else {
        writer.WriteDetails(
            counterClassname.c_str(),
            L"PercentUserTime",
            ullData);
    }

    perProcessRange = g_perProcessPrivateBytes->reference_range();
    ullData.assign(perProcessRange.first, perProcessRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            counterClassname.c_str(),
            L"PrivateBytes",
            ullData);
    } else {
        writer.WriteDetails(
            counterClassname.c_str(),
            L"PrivateBytes",
            ullData);
    }

    perProcessRange = g_perProcessVirtualBytes->reference_range();
    ullData.assign(perProcessRange.first, perProcessRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            counterClassname.c_str(),
            L"VirtualBytes",
            ullData);
    } else {
        writer.WriteDetails(
            counterClassname.c_str(),
            L"VirtualBytes",
            ullData);
    }

    perProcessRange = g_perProcessWorkingSet->reference_range();
    ullData.assign(perProcessRange.first, perProcessRange.second);
    if (g_meanOnly) {
        writer.WriteMean(
            counterClassname.c_str(),
            L"WorkingSet",
            ullData);
    } else {
        writer.WriteDetails(
            counterClassname.c_str(),
            L"WorkingSet",
            ullData);
    }
}
