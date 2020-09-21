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

HANDLE g_Break = nullptr;
const ctWmiService* g_Wmi = nullptr;

BOOL WINAPI BreakHandlerRoutine(DWORD) noexcept
{
    // regardless of the break type, signal to exit
    ::SetEvent(g_Break);
    return TRUE;
}

static const PCWSTR UsageStatement =
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
constexpr DWORD UninitializedProcessId = 0xffffffff;

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

PCWSTR g_FileName = L"ctsPerf.csv";
PCWSTR g_NetworkingFilename = L"ctsNetworking.csv";
PCWSTR g_ProcessFilename = L"ctsPerProcess.csv";

bool g_MeanOnly = false;

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
    g_Break = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (g_Break == nullptr)
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
    auto processId = UninitializedProcessId;
    DWORD timeToRunMs = 60000; // default to 60 seconds

    for (DWORD arg_count = argc; arg_count > 1; --arg_count)
    {
        if (ctString::ctOrdinalStartsWithCaseInsensative(argv[arg_count - 1], L"-process:"))
        {
            trackProcess = argv[arg_count - 1];

            // strip off the "process:" preface to the string
            const auto endOfToken = find(trackProcess.begin(), trackProcess.end(), L':');
            trackProcess.erase(trackProcess.begin(), endOfToken + 1);

            // the performance counter does not look at the extension, so remove .exe if it's there
            if (ctString::ctOrdinalEndsWithCaseInsensative(trackProcess, L".exe"))
            {
                trackProcess.erase(trackProcess.end() - 4, trackProcess.end());
            }
            if (trackProcess.empty())
            {
                wprintf(L"Incorrect option: %ws\n", argv[arg_count - 1]);
                wprintf(UsageStatement);
                return 1;
            }

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[arg_count - 1], L"-pid:"))
        {
            wstring pidString(argv[arg_count - 1]);

            // strip off the "pid:" preface to the string
            const auto endOfToken = find(pidString.begin(), pidString.end(), L':');
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
                    wprintf(L"Incorrect option: %ws\n", argv[arg_count - 1]);
                    wprintf(UsageStatement);
                    return 1;
                }
            }

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[arg_count - 1], L"-estats"))
        {
            trackEstats = true;

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[arg_count - 1], L"-Networking"))
        {
            trackNetworking = true;

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[arg_count - 1], L"-InterfaceDescription:"))
        {
            trackInterfaceDescription = argv[arg_count - 1];

            // strip off the "-InterfaceDescription:" preface to the string
            const auto endOfToken = find(trackInterfaceDescription.begin(), trackInterfaceDescription.end(), L':');
            trackInterfaceDescription.erase(trackInterfaceDescription.begin(), endOfToken + 1);

        }
        else if (ctString::ctOrdinalStartsWithCaseInsensative(argv[arg_count - 1], L"-MeanOnly"))
        {
            g_MeanOnly = true;

        }
        else
        {
            const auto timeToRun = wcstoul(argv[arg_count - 1], nullptr, 10);
            if (timeToRun == 0 || timeToRun == ULONG_MAX)
            {
                wprintf(L"Incorrect option: %ws\n", argv[arg_count - 1]);
                wprintf(UsageStatement);
                return 1;
            }
            timeToRunMs = timeToRun * 1000;
            if (timeToRunMs < timeToRun)
            {
                wprintf(L"Incorrect option: %ws\n", argv[arg_count - 1]);
                wprintf(UsageStatement);
                return 1;
            }
        }
    }

    const auto trackPerProcess = !trackProcess.empty() || processId != UninitializedProcessId;

    if (timeToRunMs <= 5000)
    {
        wprintf(L"ERROR: Must run over 5 seconds to have enough samples for analysis\n");
        wprintf(UsageStatement);
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
        g_Wmi = &wmi;

        auto deleteAllCounters = wil::scope_exit([&]() noexcept { DeleteAllCounters(); });

        ctsPerf::ctsWriteDetails cpuwriter(g_FileName);
        cpuwriter.create_file(g_MeanOnly);

        ctsPerf::ctsWriteDetails networkWriter(g_NetworkingFilename);
        if (trackNetworking)
        {
            networkWriter.create_file(g_MeanOnly);
        }

        ctsPerf::ctsWriteDetails processWriter(g_ProcessFilename);
        if (trackPerProcess)
        {
            processWriter.create_file(g_MeanOnly);
        }

        wprintf(L".");

        // create a perf counter objects to maintain these counters
        std::vector<ctWmiPerformance> performance_vector;

        performance_vector.emplace_back(InstantiateProcessorCounters());
        performance_vector.emplace_back(InstantiateMemoryCounters());

        if (trackNetworking)
        {
            performance_vector.emplace_back(InstantiateNetworkAdapterCounters(trackInterfaceDescription));
            performance_vector.emplace_back(InstantiateNetworkInterfaceCounters(trackInterfaceDescription));
            performance_vector.emplace_back(InstantiateIPCounters());
            performance_vector.emplace_back(InstantiateTCPCounters());
            performance_vector.emplace_back(InstantiateUDPCounters());
        }

        if (!trackProcess.empty())
        {
            performance_vector.emplace_back(InstantiatePerProcessByNameCounters(trackProcess));

        }
        else if (processId != UninitializedProcessId)
        {
            performance_vector.emplace_back(InstantiatePerProcessByPIDCounters(processId));
        }

        wprintf(L"\nStarting counters : will run for %lu seconds\n (hit ctrl-c to exit early) ...\n\n", static_cast<DWORD>(timeToRunMs / 1000UL));
        for (auto& perf_object : performance_vector)
        {
            perf_object.start_all_counters(1000);
        }

        ::WaitForSingleObject(g_Break, timeToRunMs);

        wprintf(L"Stopping counters ....\n\n");
        for (auto& perf_object : performance_vector)
        {
            perf_object.stop_all_counters();
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

    CloseHandle(g_Break);

    return 0;
}


/****************************************************************************************************/
/*                                         Processor                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processorTime;
shared_ptr<ctWmiPerformanceCounter<ULONG>> processorPercentOfMax;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processorPercentDpcTime;
shared_ptr<ctWmiPerformanceCounter<ULONG>> processorDpcsQueuedPerSecond;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processorPercentPrivilegedTime;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processorPercentUserTime;
ctWmiPerformance InstantiateProcessorCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);

    // create objects for system counters we care about
    processorTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Processor,
        L"PercentProcessorTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(processorTime);
    wprintf(L".");

    processorPercentOfMax = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::Processor,
        L"PercentofMaximumFrequency",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(processorPercentOfMax);
    wprintf(L".");

	processorPercentDpcTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
		ctWmiEnumClassName::Processor,
		L"PercentDPCTime",
		g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performance_counter.add_counter(processorPercentDpcTime);
	wprintf(L".");

	processorDpcsQueuedPerSecond = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
		ctWmiEnumClassName::Processor,
		L"DPCsQueuedPersec",
		g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performance_counter.add_counter(processorDpcsQueuedPerSecond);
	wprintf(L".");

	processorPercentPrivilegedTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
		ctWmiEnumClassName::Processor,
		L"PercentPrivilegedTime",
		g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performance_counter.add_counter(processorPercentPrivilegedTime);
	wprintf(L".");

	processorPercentUserTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
		ctWmiEnumClassName::Processor,
		L"PercentUserTime",
		g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performance_counter.add_counter(processorPercentUserTime);
	wprintf(L".");

    return performance_counter;
}
void DeleteProcessorCounters() noexcept
{
    processorTime.reset();
    processorPercentOfMax.reset();
	processorPercentDpcTime.reset();
	processorDpcsQueuedPerSecond.reset();
	processorPercentPrivilegedTime.reset();
	processorPercentUserTime.reset();
}
void ProcessProcessorCounters(ctsPerf::ctsWriteDetails& writer)
{
    ctWmiEnumerate enumProcessors(*g_Wmi);
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
		writer.write_row(
			wil::str_printf<std::wstring>(
				L"Processor %ws",
				ctString::ctReplaceAllCopy(name, L",", L" - ").c_str()));

        const auto processor_range = processorTime->reference_range(name.c_str());
		vector<ULONGLONG> processor_time_vector(processor_range.first, processor_range.second);

        const auto percent_range = processorPercentOfMax->reference_range(name.c_str());
		vector<ULONG> processor_percent_vector(percent_range.first, percent_range.second);

        const auto percent_dpc_time_range = processorPercentDpcTime->reference_range(name.c_str());
        const auto dpcs_queued_per_second_range = processorDpcsQueuedPerSecond->reference_range(name.c_str());
        const auto processor_percent_privileged_time_range = processorPercentPrivilegedTime->reference_range(name.c_str());
        const auto processor_percent_user_time_range = processorPercentUserTime->reference_range(name.c_str());

		if (g_MeanOnly) {
		    // ReSharper disable once CppUseAuto
			vector<ULONGLONG> normalized_processor_time(processor_time_vector);

			// convert to a percentage
			auto calculated_processor_time = processor_time_vector[3] / 100.0;
			calculated_processor_time *= processor_percent_vector[3] / 100.0;
			normalized_processor_time[3] = static_cast<ULONG>(calculated_processor_time * 100UL);

			writer.write_mean(
				L"Processor",
				L"Raw CPU Usage",
				processor_time_vector);

			writer.write_mean(
				L"Processor",
				L"Normalized CPU Usage (Raw * PercentofMaximumFrequency)",
				normalized_processor_time);

			ullData.assign(percent_dpc_time_range.first, percent_dpc_time_range.second);
			writer.write_mean(
				L"Processor",
				L"Percent DPC Time",
				ullData);

			ulData.assign(dpcs_queued_per_second_range.first, dpcs_queued_per_second_range.second);
			writer.write_mean(
				L"Processor",
				L"DPCs Queued Per Second",
				ulData);

			ullData.assign(processor_percent_privileged_time_range.first, processor_percent_privileged_time_range.second);
			writer.write_mean(
				L"Processor",
				L"Percent Privileged Time",
				ullData);

			ullData.assign(processor_percent_user_time_range.first, processor_percent_user_time_range.second);
			writer.write_mean(
				L"Processor",
				L"Percent User Time",
				ullData);
		}
		else {
			vector<ULONG> normalized_processor_time;
			// produce the raw % as well as the 'normalized' % based off of the PercentofMaximumFrequency
			auto percentage_iterator(processor_percent_vector.begin());
			for (const auto& processor_data : processor_time_vector) {
				// convert to a percentage
				auto calculated_processor_time = processor_data / 100.0;
				calculated_processor_time *= *percentage_iterator / 100.0;

				normalized_processor_time.push_back(static_cast<ULONG>(calculated_processor_time * 100UL));
				++percentage_iterator;
			}

			writer.write_details(
				L"Processor",
				L"Raw CPU Usage",
				processor_time_vector);

			writer.write_details(
				L"Processor",
				L"Normalized CPU Usage (Raw * PercentofMaximumFrequency)",
				normalized_processor_time);

			ullData.assign(percent_dpc_time_range.first, percent_dpc_time_range.second);
			writer.write_details(
				L"Processor",
				L"Percent DPC Time",
				ullData);

			ulData.assign(dpcs_queued_per_second_range.first, dpcs_queued_per_second_range.second);
			writer.write_details(
				L"Processor",
				L"DPCs Queued Per Second",
				ulData);

			ullData.assign(processor_percent_privileged_time_range.first, processor_percent_privileged_time_range.second);
			writer.write_details(
				L"Processor",
				L"Percent Privileged Time",
				ullData);

			ullData.assign(processor_percent_user_time_range.first, processor_percent_user_time_range.second);
			writer.write_details(
				L"Processor",
				L"Percent User Time",
				ullData);
		}
	}

	writer.write_empty_row();
}

/****************************************************************************************************/
/*                                            Memory                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> pagedPoolBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> nonPagedPoolBytes;
ctWmiPerformance InstantiateMemoryCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);
    pagedPoolBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Memory,
        L"PoolPagedBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(pagedPoolBytes);
    wprintf(L".");

    nonPagedPoolBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Memory,
        L"PoolNonpagedBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(nonPagedPoolBytes);
    wprintf(L".");

    return performance_counter;
}
void DeleteMemoryCounters() noexcept
{
    pagedPoolBytes.reset();
    nonPagedPoolBytes.reset();
}
void ProcessMemoryCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;
    const auto paged_pool_range = pagedPoolBytes->reference_range();
    const auto non_paged_pool_range = nonPagedPoolBytes->reference_range();

    if (g_MeanOnly) {
        ullData.assign(paged_pool_range.first, paged_pool_range.second);
        writer.write_mean(
            L"Memory",
            L"PoolPagedBytes",
            ullData);

        ullData.assign(non_paged_pool_range.first, non_paged_pool_range.second);
        writer.write_mean(
            L"Memory",
            L"PoolNonpagedBytes",
            ullData);
    } else {
        ullData.assign(paged_pool_range.first, paged_pool_range.second);
        writer.write_details(
            L"Memory",
            L"PoolPagedBytes",
            ullData);

        ullData.assign(non_paged_pool_range.first, non_paged_pool_range.second);
        writer.write_details(
            L"Memory",
            L"PoolNonpagedBytes",
            ullData);
    }
}

/****************************************************************************************************/
/*                                     NetworkAdapter                                               */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkAdapterTotalBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkAdapterOffloadedConnections;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkAdapterPacketsOutboundDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkAdapterPacketsOutboundErrors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkAdapterPacketsReceivedDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkAdapterPacketsReceivedErrors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkAdapterPacketsPerSecond;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkAdapterActiveRscConnections;
ctWmiPerformance InstantiateNetworkAdapterCounters(const std::wstring& trackInterfaceDescription)
{
    ctWmiPerformance performance_counter(*g_Wmi);

    networkAdapterTotalBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"BytesTotalPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        networkAdapterTotalBytes->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkAdapterTotalBytes);
    wprintf(L".");

    networkAdapterOffloadedConnections = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"OffloadedConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkAdapterOffloadedConnections->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkAdapterOffloadedConnections);
    wprintf(L".");

    networkAdapterPacketsOutboundDiscarded = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkAdapterPacketsOutboundDiscarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkAdapterPacketsOutboundDiscarded);
    wprintf(L".");

    networkAdapterPacketsOutboundErrors = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsOutboundErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkAdapterPacketsOutboundErrors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkAdapterPacketsOutboundErrors);
    wprintf(L".");

    networkAdapterPacketsReceivedDiscarded = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkAdapterPacketsReceivedDiscarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkAdapterPacketsReceivedDiscarded);
    wprintf(L".");

    networkAdapterPacketsReceivedErrors = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkAdapterPacketsReceivedErrors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkAdapterPacketsReceivedErrors);
    wprintf(L".");

    networkAdapterPacketsPerSecond = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        networkAdapterPacketsPerSecond->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkAdapterPacketsPerSecond);
    wprintf(L".");

    networkAdapterActiveRscConnections = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"TCPActiveRSCConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkAdapterActiveRscConnections->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkAdapterActiveRscConnections);
    wprintf(L".");

    return performance_counter;
}
void DeleteNetworkAdapterCounters() noexcept
{
    networkAdapterTotalBytes.reset();
    networkAdapterOffloadedConnections.reset();
    networkAdapterPacketsOutboundDiscarded.reset();
    networkAdapterPacketsOutboundErrors.reset();
    networkAdapterPacketsReceivedDiscarded.reset();
    networkAdapterPacketsReceivedErrors.reset();
    networkAdapterPacketsPerSecond.reset();
    networkAdapterActiveRscConnections.reset();
}
void ProcessNetworkAdapterCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;

    // there is no great way to find the 'Name' for each network interface tracked
    // - it is not guaranteed to match anything from NetAdapter or NetIPInteface
    // - making a single query directly here to at least get the names
    ctWmiEnumerate enumAdapter(*g_Wmi);
    enumAdapter.query(L"SELECT * FROM Win32_PerfFormattedData_Tcpip_NetworkAdapter");
    if (enumAdapter.begin() == enumAdapter.end()) {
        throw exception("Unable to find an adapter to report on - querying Win32_PerfFormattedData_Tcpip_NetworkAdapter returned nothing");
    }

	writer.write_row(L"NetworkAdapter");
    for (const auto& _adapter : enumAdapter) {
        wstring name;
        _adapter.get(L"Name", &name);

        auto network_range = networkAdapterPacketsPerSecond->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        if (g_MeanOnly) {
            writer.write_mean(
                L"NetworkAdapter",
                wil::str_printf<std::wstring>(
                    L"PacketsPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.write_details(
                L"NetworkAdapter",
                wil::str_printf<std::wstring>(
                    L"PacketsPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        }
        network_range = networkAdapterTotalBytes->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        if (g_MeanOnly) {
            writer.write_mean(
                L"NetworkAdapter",
                wil::str_printf<std::wstring>(
                    L"BytesTotalPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.write_details(
                L"NetworkAdapter",
                wil::str_printf<std::wstring>(
                    L"BytesTotalPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        }

        network_range = networkAdapterOffloadedConnections->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"OffloadedConnections for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = networkAdapterActiveRscConnections->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"TCPActiveRSCConnections for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = networkAdapterPacketsOutboundDiscarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"PacketsOutboundDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = networkAdapterPacketsOutboundErrors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"PacketsOutboundErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = networkAdapterPacketsReceivedDiscarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = networkAdapterPacketsReceivedErrors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

		writer.write_empty_row();
    }
}

/****************************************************************************************************/
/*                                     NetworkInterface                                             */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkInterfaceTotalBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkInterfacePacketsOutboundDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkInterfacePacketsOutboundErrors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkInterfacePacketsReceivedDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkInterfacePacketsReceivedErrors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> networkInterfacePacketsReceivedUnknown;
ctWmiPerformance InstantiateNetworkInterfaceCounters(const std::wstring& trackInterfaceDescription)
{
    ctWmiPerformance performance_counter(*g_Wmi);

    networkInterfaceTotalBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"BytesTotalPerSec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        networkInterfaceTotalBytes->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkInterfaceTotalBytes);
    wprintf(L".");

    networkInterfacePacketsOutboundDiscarded = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkInterfacePacketsOutboundDiscarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkInterfacePacketsOutboundDiscarded);
    wprintf(L".");

    networkInterfacePacketsOutboundErrors = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsOutboundErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkInterfacePacketsOutboundErrors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkInterfacePacketsOutboundErrors);
    wprintf(L".");

    networkInterfacePacketsReceivedDiscarded = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkInterfacePacketsReceivedDiscarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkInterfacePacketsReceivedDiscarded);
    wprintf(L".");

    networkInterfacePacketsReceivedErrors = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkInterfacePacketsReceivedErrors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkInterfacePacketsReceivedErrors);
    wprintf(L".");

    networkInterfacePacketsReceivedUnknown = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsReceivedUnknown",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        networkInterfacePacketsReceivedUnknown->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(networkInterfacePacketsReceivedUnknown);
    wprintf(L".");

    return performance_counter;
}
void DeleteNetworkInterfaceCounters() noexcept
{
    networkInterfaceTotalBytes.reset();
    networkInterfacePacketsOutboundDiscarded.reset();
    networkInterfacePacketsOutboundErrors.reset();
    networkInterfacePacketsReceivedDiscarded.reset();
    networkInterfacePacketsReceivedErrors.reset();
    networkInterfacePacketsReceivedUnknown.reset();
}
void ProcessNetworkInterfaceCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;

    // there is no great way to find the 'Name' for each network interface tracked
    // - it is not guaranteed to match anything from NetAdapter or NetIPInterface
    // - making a single query directly here to at least get the names
    ctWmiEnumerate enumAdapter(*g_Wmi);
    enumAdapter.query(L"SELECT * FROM Win32_PerfFormattedData_Tcpip_NetworkInterface");
    if (enumAdapter.begin() == enumAdapter.end()) {
        throw exception("Unable to find an adapter to report on - querying Win32_PerfFormattedData_Tcpip_NetworkInterface returned nothing");
    }

	writer.write_row(L"NetworkInterface");
    for (const auto& _adapter : enumAdapter) {
        wstring name;
        _adapter.get(L"Name", &name);

        const auto byte_range = networkInterfaceTotalBytes->reference_range(name.c_str());
        ullData.assign(byte_range.first, byte_range.second);
        if (g_MeanOnly) {
            writer.write_mean(
                L"NetworkInterface",
                wil::str_printf<std::wstring>(
                    L"BytesTotalPerSec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.write_details(
                L"NetworkInterface",
                wil::str_printf<std::wstring>(
                    L"BytesTotalPerSec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        }
        auto network_range = networkInterfacePacketsOutboundDiscarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsOutboundDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = networkInterfacePacketsOutboundErrors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsOutboundErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = networkInterfacePacketsReceivedDiscarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = networkInterfacePacketsReceivedErrors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = networkInterfacePacketsReceivedUnknown->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            wil::str_printf<std::wstring>(
                L"PacketsReceivedUnknown for interface %ws",
                name.c_str()).c_str(),
            ullData);

		writer.write_empty_row();
    }
}


/****************************************************************************************************/
/*                                        TCPIP IPv4                                                */
/*                                        TCPIP IPv6                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv4OutboundDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv4OutboundNoRoute;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv4ReceivedAddressErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv4ReceivedDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv4ReceivedHeaderErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv4ReceivedUnknownProtocol;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv4FragmentReassemblyFailures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv4FragmentationFailures;

shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv6OutboundDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv6OutboundNoRoute;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv6ReceivedAddressErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv6ReceivedDiscarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv6ReceivedHeaderErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv6ReceivedUnknownProtocol;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv6FragmentReassemblyFailures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipIpv6FragmentationFailures;
ctWmiPerformance InstantiateIPCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);

    tcpipIpv4OutboundDiscarded = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv4OutboundDiscarded);
    wprintf(L".");

    tcpipIpv4OutboundNoRoute = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsOutboundNoRoute",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv4OutboundNoRoute);
    wprintf(L".");

    tcpipIpv4ReceivedAddressErrors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedAddressErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv4ReceivedAddressErrors);
    wprintf(L".");

    tcpipIpv4ReceivedDiscarded = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv4ReceivedDiscarded);
    wprintf(L".");

    tcpipIpv4ReceivedHeaderErrors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedHeaderErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv4ReceivedHeaderErrors);
    wprintf(L".");

    tcpipIpv4ReceivedUnknownProtocol = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedUnknownProtocol",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv4ReceivedUnknownProtocol);
    wprintf(L".");

    tcpipIpv4FragmentReassemblyFailures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"FragmentReassemblyFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv4FragmentReassemblyFailures);
    wprintf(L".");

    tcpipIpv4FragmentationFailures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"FragmentationFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv4FragmentationFailures);
    wprintf(L".");

    tcpipIpv6OutboundDiscarded = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv6OutboundDiscarded);
    wprintf(L".");

    tcpipIpv6OutboundNoRoute = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsOutboundNoRoute",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv6OutboundNoRoute);
    wprintf(L".");

    tcpipIpv6ReceivedAddressErrors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedAddressErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv6ReceivedAddressErrors);
    wprintf(L".");

    tcpipIpv6ReceivedDiscarded = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv6ReceivedDiscarded);
    wprintf(L".");

    tcpipIpv6ReceivedHeaderErrors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedHeaderErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv6ReceivedHeaderErrors);
    wprintf(L".");

    tcpipIpv6ReceivedUnknownProtocol = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedUnknownProtocol",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv6ReceivedUnknownProtocol);
    wprintf(L".");

    tcpipIpv6FragmentReassemblyFailures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"FragmentReassemblyFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv6FragmentReassemblyFailures);
    wprintf(L".");

    tcpipIpv6FragmentationFailures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"FragmentationFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipIpv6FragmentationFailures);
    wprintf(L".");

    return performance_counter;
}
void DeleteIPCounters() noexcept
{
    tcpipIpv4OutboundDiscarded.reset();
    tcpipIpv4OutboundNoRoute.reset();
    tcpipIpv4ReceivedAddressErrors.reset();
    tcpipIpv4ReceivedDiscarded.reset();
    tcpipIpv4ReceivedHeaderErrors.reset();
    tcpipIpv4ReceivedUnknownProtocol.reset();
    tcpipIpv4FragmentReassemblyFailures.reset();
    tcpipIpv4FragmentationFailures.reset();

    tcpipIpv6OutboundDiscarded.reset();
    tcpipIpv6OutboundNoRoute.reset();
    tcpipIpv6ReceivedAddressErrors.reset();
    tcpipIpv6ReceivedDiscarded.reset();
    tcpipIpv6ReceivedHeaderErrors.reset();
    tcpipIpv6ReceivedUnknownProtocol.reset();
    tcpipIpv6FragmentReassemblyFailures.reset();
    tcpipIpv6FragmentationFailures.reset();
}
void ProcessIPCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONG> ulData;

	writer.write_row(L"TCPIP - IPv4");

    auto network_loss_range = tcpipIpv4OutboundDiscarded->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsOutboundDiscarded",
        ulData);

    network_loss_range = tcpipIpv4OutboundNoRoute->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsOutboundNoRoute",
        ulData);

    network_loss_range = tcpipIpv4ReceivedAddressErrors->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedAddressErrors",
        ulData);

    network_loss_range = tcpipIpv4ReceivedDiscarded->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedDiscarded",
        ulData);

    network_loss_range = tcpipIpv4ReceivedHeaderErrors->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedHeaderErrors",
        ulData);

    network_loss_range = tcpipIpv4ReceivedUnknownProtocol->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedUnknownProtocol",
        ulData);

    network_loss_range = tcpipIpv4FragmentReassemblyFailures->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"FragmentReassemblyFailures",
        ulData);

    network_loss_range = tcpipIpv4FragmentationFailures->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"FragmentationFailures",
        ulData);

    network_loss_range = tcpipIpv6OutboundDiscarded->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsOutboundDiscarded",
        ulData);

    network_loss_range = tcpipIpv6OutboundNoRoute->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsOutboundNoRoute",
        ulData);

    network_loss_range = tcpipIpv6ReceivedAddressErrors->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedAddressErrors",
        ulData);

    network_loss_range = tcpipIpv6ReceivedDiscarded->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedDiscarded",
        ulData);

    network_loss_range = tcpipIpv6ReceivedHeaderErrors->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedHeaderErrors",
        ulData);

    network_loss_range = tcpipIpv6ReceivedUnknownProtocol->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedUnknownProtocol",
        ulData);

    network_loss_range = tcpipIpv6FragmentReassemblyFailures->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"FragmentReassemblyFailures",
        ulData);

    network_loss_range = tcpipIpv6FragmentationFailures->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"FragmentationFailures",
        ulData);

	writer.write_empty_row();
}


/****************************************************************************************************/
/*                                        TCPIP TCPv4                                                */
/*                                        TCPIP TCPv6                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipTcpv4ConnectionsEstablished;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipTcpv6ConnectionsEstablished;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipTcpv4ConnectionFailures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipTcpv6ConnectionFailures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipTcpv4ConnectionsReset;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipTcpv6ConnectionsReset;
shared_ptr<ctWmiPerformanceCounter<ULONG>> winsockBspRejectedConnections;
shared_ptr<ctWmiPerformanceCounter<ULONG>> winsockBspRejectedConnectionsPerSec;
ctWmiPerformance InstantiateTCPCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);

    tcpipTcpv4ConnectionsEstablished = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv4,
        L"ConnectionsEstablished",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpipTcpv4ConnectionsEstablished);
    wprintf(L".");

    tcpipTcpv6ConnectionsEstablished = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv6,
        L"ConnectionsEstablished",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpipTcpv6ConnectionsEstablished);
    wprintf(L".");

    tcpipTcpv4ConnectionFailures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv4,
        L"ConnectionFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipTcpv4ConnectionFailures);
    wprintf(L".");

    tcpipTcpv6ConnectionFailures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv6,
        L"ConnectionFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipTcpv6ConnectionFailures);
    wprintf(L".");

    tcpipTcpv4ConnectionsReset = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv4,
        L"ConnectionsReset",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipTcpv4ConnectionsReset);
    wprintf(L".");

    tcpipTcpv6ConnectionsReset = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv6,
        L"ConnectionsReset",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipTcpv6ConnectionsReset);
    wprintf(L".");

    winsockBspRejectedConnections = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"RejectedConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(winsockBspRejectedConnections);
    wprintf(L".");

    winsockBspRejectedConnectionsPerSec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"RejectedConnectionsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(winsockBspRejectedConnectionsPerSec);
    wprintf(L".");

    return performance_counter;
}
void DeleteTCPCounters() noexcept
{
    tcpipTcpv4ConnectionsEstablished.reset();
    tcpipTcpv6ConnectionsEstablished.reset();
    tcpipTcpv4ConnectionFailures.reset();
    tcpipTcpv6ConnectionFailures.reset();
    tcpipTcpv4ConnectionsReset.reset();
    tcpipTcpv6ConnectionsReset.reset();
    winsockBspRejectedConnections.reset();
    winsockBspRejectedConnectionsPerSec.reset();
}
void ProcessTCPCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONG> ulData;

	writer.write_row(L"TCPIP - TCPv4");
    auto network_range = tcpipTcpv4ConnectionsEstablished->reference_range();
    ulData.assign(network_range.first, network_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            L"TCPIP - TCPv4",
            L"ConnectionsEstablished",
            ulData);
    } else {
        writer.write_details(
            L"TCPIP - TCPv4",
            L"ConnectionsEstablished",
            ulData);
    }

    network_range = tcpipTcpv6ConnectionsEstablished->reference_range();
    ulData.assign(network_range.first, network_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            L"TCPIP - TCPv6",
            L"ConnectionsEstablished",
            ulData);
    }
    else {
        writer.write_details(
            L"TCPIP - TCPv6",
            L"ConnectionsEstablished",
            ulData);
    }

    network_range = tcpipTcpv4ConnectionFailures->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv4",
        L"ConnectionFailures",
        ulData);

    network_range = tcpipTcpv6ConnectionFailures->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv6",
        L"ConnectionFailures",
        ulData);

    network_range = tcpipTcpv4ConnectionsReset->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv4",
        L"ConnectionsReset",
        ulData);

    network_range = tcpipTcpv6ConnectionsReset->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv6",
        L"ConnectionsReset",
        ulData);

    network_range = winsockBspRejectedConnections->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"Winsock",
        L"RejectedConnections",
        ulData);

    network_range = winsockBspRejectedConnectionsPerSec->reference_range();
    ulData.assign(network_range.first, network_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            L"Winsock",
            L"RejectedConnectionsPersec",
            ulData);
    } else {
        writer.write_details(
            L"Winsock",
            L"RejectedConnectionsPersec",
            ulData);
    }

	writer.write_empty_row();
}


/****************************************************************************************************/
/*                                        TCPIP UDPv4                                                */
/*                                        TCPIP UDPv6                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipUdpv4NoportPerSec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipUdpv4ReceivedErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipUdpv4DatagramsPerSec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipUdpv6NoportPerSec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipUdpv6ReceivedErrors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpipUdpv6DatagramsPerSec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> winsockBspDroppedDatagrams;
shared_ptr<ctWmiPerformanceCounter<ULONG>> winsockBspDroppedDatagramsPerSecond;
ctWmiPerformance InstantiateUDPCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);

    tcpipUdpv4NoportPerSec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv4,
        L"DatagramsNoPortPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpipUdpv4NoportPerSec);
    wprintf(L".");

    tcpipUdpv4ReceivedErrors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv4,
        L"DatagramsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipUdpv4ReceivedErrors);
    wprintf(L".");

    tcpipUdpv4DatagramsPerSec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv4,
        L"DatagramsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpipUdpv4DatagramsPerSec);
    wprintf(L".");

    tcpipUdpv6NoportPerSec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv6,
        L"DatagramsNoPortPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpipUdpv6NoportPerSec);
    wprintf(L".");

    tcpipUdpv6ReceivedErrors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv6,
        L"DatagramsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpipUdpv6ReceivedErrors);
    wprintf(L".");

    tcpipUdpv6DatagramsPerSec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv6,
        L"DatagramsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpipUdpv6DatagramsPerSec);
    wprintf(L".");

    winsockBspDroppedDatagrams = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"DroppedDatagrams",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(winsockBspDroppedDatagrams);
    wprintf(L".");

    winsockBspDroppedDatagramsPerSecond = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"DroppedDatagramsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(winsockBspDroppedDatagramsPerSecond);
    wprintf(L".");

    return performance_counter;
}
void DeleteUDPCounters() noexcept
{
    tcpipUdpv4NoportPerSec.reset();
    tcpipUdpv4ReceivedErrors.reset();
    tcpipUdpv4DatagramsPerSec.reset();
    tcpipUdpv6NoportPerSec.reset();
    tcpipUdpv6ReceivedErrors.reset();
    tcpipUdpv6DatagramsPerSec.reset();
    winsockBspDroppedDatagrams.reset();
    winsockBspDroppedDatagramsPerSecond.reset();
}
void ProcessUDPCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONG> ulData;

	writer.write_row(L"TCPIP - UDPv4");

    auto udp_range = tcpipUdpv4NoportPerSec->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            L"TCPIP - UDPv4",
            L"DatagramsNoPortPersec",
            ulData);
    } else {
        writer.write_details(
            L"TCPIP - UDPv4",
            L"DatagramsNoPortPersec",
            ulData);
    }

    udp_range = tcpipUdpv4DatagramsPerSec->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            L"TCPIP - UDPv4",
            L"DatagramsPersec",
            ulData);
    } else {
        writer.write_details(
            L"TCPIP - UDPv4",
            L"DatagramsPersec",
            ulData);
    }

	udp_range = tcpipUdpv4ReceivedErrors->reference_range();
	ulData.assign(udp_range.first, udp_range.second);
	writer.write_difference(
		L"TCPIP - UDPv4",
		L"DatagramsReceivedErrors",
		ulData);

	writer.write_empty_row();
	writer.write_row(L"TCPIP - UDPv6");

	udp_range = tcpipUdpv6NoportPerSec->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            L"TCPIP - UDPv6",
            L"DatagramsNoPortPersec",
            ulData);
    } else {
        writer.write_details(
            L"TCPIP - UDPv6",
            L"DatagramsNoPortPersec",
            ulData);
    }

    udp_range = tcpipUdpv6DatagramsPerSec->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            L"TCPIP - UDPv6",
            L"DatagramsPersec",
            ulData);
    } else {
        writer.write_details(
            L"TCPIP - UDPv6",
            L"DatagramsPersec",
            ulData);
    }

    udp_range = tcpipUdpv6ReceivedErrors->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    writer.write_difference(
        L"TCPIP - UDPv6",
        L"DatagramsReceivedErrors",
        ulData);

	writer.write_empty_row();
	writer.write_row(L"Winsock Datagrams");

    udp_range = winsockBspDroppedDatagrams->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    writer.write_difference(
        L"Winsock",
        L"DroppedDatagrams",
        ulData);

    udp_range = winsockBspDroppedDatagramsPerSecond->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            L"Winsock",
            L"DroppedDatagramsPersec",
            ulData);
    } else {
        writer.write_details(
            L"Winsock",
            L"DroppedDatagramsPersec",
            ulData);
    }

	writer.write_empty_row();
}


shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> perProcessPrivilegedTime;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> perProcessProcessorTime;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> perProcessUserTime;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> perProcessPrivateBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> perProcessVirtualBytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> perProcessWorkingSet;
ctWmiPerformance InstantiatePerProcessByNameCounters(const std::wstring& trackProcess)
{
    ctWmiPerformance performance_counter(*g_Wmi);

    // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
    perProcessPrivilegedTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentPrivilegedTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessPrivilegedTime->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(perProcessPrivilegedTime);
    wprintf(L".");

    perProcessProcessorTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentProcessorTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessProcessorTime->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(perProcessProcessorTime);
    wprintf(L".");

    perProcessUserTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentUserTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessUserTime->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(perProcessUserTime);
    wprintf(L".");

    perProcessPrivateBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PrivateBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessPrivateBytes->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(perProcessPrivateBytes);
    wprintf(L".");

    perProcessVirtualBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"VirtualBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessVirtualBytes->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(perProcessVirtualBytes);
    wprintf(L".");

    perProcessWorkingSet = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"WorkingSet",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessWorkingSet->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(perProcessWorkingSet);
    wprintf(L".");

    return performance_counter;
}
ctWmiPerformance InstantiatePerProcessByPIDCounters(const DWORD processId)
{
    ctWmiPerformance performance_counter(*g_Wmi);

    // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
    perProcessPrivilegedTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentPrivilegedTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessPrivilegedTime->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(perProcessPrivilegedTime);
    wprintf(L".");

    perProcessProcessorTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentProcessorTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessProcessorTime->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(perProcessProcessorTime);
    wprintf(L".");

    perProcessUserTime = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentUserTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessUserTime->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(perProcessUserTime);
    wprintf(L".");

    perProcessPrivateBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PrivateBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessPrivateBytes->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(perProcessPrivateBytes);
    wprintf(L".");

    perProcessVirtualBytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"VirtualBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessVirtualBytes->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(perProcessVirtualBytes);
    wprintf(L".");

    perProcessWorkingSet = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"WorkingSet",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    perProcessWorkingSet->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(perProcessWorkingSet);
    wprintf(L".");

    return performance_counter;
}
void DeletePerProcessCounters() noexcept
{
    perProcessPrivilegedTime.reset();
    perProcessProcessorTime.reset();
    perProcessUserTime.reset();
    perProcessPrivateBytes.reset();
    perProcessVirtualBytes.reset();
    perProcessWorkingSet.reset();
}
void ProcessPerProcessCounters(const wstring& trackProcess, const DWORD processId, ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;

    wstring counter_classname;
    if (!trackProcess.empty()) {
        wstring full_name(trackProcess);
        full_name += L".exe";
        counter_classname = wil::str_printf<std::wstring>(L"Process (%ws)", full_name.c_str());

    } else {
        counter_classname = wil::str_printf<std::wstring>(L"Process (pid %u)", processId);
    }

    auto per_process_range = perProcessPrivilegedTime->reference_range();
    ullData.assign(per_process_range.first, per_process_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            counter_classname.c_str(),
            L"PercentPrivilegedTime",
            ullData);
    } else {
        writer.write_details(
            counter_classname.c_str(),
            L"PercentPrivilegedTime",
            ullData);
    }

    per_process_range = perProcessProcessorTime->reference_range();
    ullData.assign(per_process_range.first, per_process_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            counter_classname.c_str(),
            L"PercentProcessorTime",
            ullData);
    } else {
        writer.write_details(
            counter_classname.c_str(),
            L"PercentProcessorTime",
            ullData);
    }

    per_process_range = perProcessUserTime->reference_range();
    ullData.assign(per_process_range.first, per_process_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            counter_classname.c_str(),
            L"PercentUserTime",
            ullData);
    } else {
        writer.write_details(
            counter_classname.c_str(),
            L"PercentUserTime",
            ullData);
    }

    per_process_range = perProcessPrivateBytes->reference_range();
    ullData.assign(per_process_range.first, per_process_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            counter_classname.c_str(),
            L"PrivateBytes",
            ullData);
    } else {
        writer.write_details(
            counter_classname.c_str(),
            L"PrivateBytes",
            ullData);
    }

    per_process_range = perProcessVirtualBytes->reference_range();
    ullData.assign(per_process_range.first, per_process_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            counter_classname.c_str(),
            L"VirtualBytes",
            ullData);
    } else {
        writer.write_details(
            counter_classname.c_str(),
            L"VirtualBytes",
            ullData);
    }

    per_process_range = perProcessWorkingSet->reference_range();
    ullData.assign(per_process_range.first, per_process_range.second);
    if (g_MeanOnly) {
        writer.write_mean(
            counter_classname.c_str(),
            L"WorkingSet",
            ullData);
    } else {
        writer.write_details(
            counter_classname.c_str(),
            L"WorkingSet",
            ullData);
    }
}
