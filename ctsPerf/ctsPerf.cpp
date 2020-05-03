/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <cstdio>
#include <cwchar>
#include <vector>
#include <string>
#include <memory>
// os headers
#include <windows.h>
#include <WinSock2.h>
// wil headers
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
    catch (const exception& e)
    {
        wprintf(L"ctsPerf exception: %ws\n", ctString::ctFormatException(e).c_str());
        return 1;
    }

    CloseHandle(g_Break);

    return 0;
}


/****************************************************************************************************/
/*                                         Processor                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processor_time;
shared_ptr<ctWmiPerformanceCounter<ULONG>> processor_percent_of_max;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processor_percent_dpc_time;
shared_ptr<ctWmiPerformanceCounter<ULONG>> processor_dpcs_queued_per_second;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processor_percent_privileged_time;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processor_percent_user_time;
ctWmiPerformance InstantiateProcessorCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);

    // create objects for system counters we care about
    processor_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Processor,
        L"PercentProcessorTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(processor_time);
    wprintf(L".");

    processor_percent_of_max = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::Processor,
        L"PercentofMaximumFrequency",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(processor_percent_of_max);
    wprintf(L".");

	processor_percent_dpc_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
		ctWmiEnumClassName::Processor,
		L"PercentDPCTime",
		g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performance_counter.add_counter(processor_percent_dpc_time);
	wprintf(L".");

	processor_dpcs_queued_per_second = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
		ctWmiEnumClassName::Processor,
		L"DPCsQueuedPersec",
		g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performance_counter.add_counter(processor_dpcs_queued_per_second);
	wprintf(L".");

	processor_percent_privileged_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
		ctWmiEnumClassName::Processor,
		L"PercentPrivilegedTime",
		g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performance_counter.add_counter(processor_percent_privileged_time);
	wprintf(L".");

	processor_percent_user_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
		ctWmiEnumClassName::Processor,
		L"PercentUserTime",
		g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
	performance_counter.add_counter(processor_percent_user_time);
	wprintf(L".");

    return performance_counter;
}
void DeleteProcessorCounters() noexcept
{
    processor_time.reset();
    processor_percent_of_max.reset();
	processor_percent_dpc_time.reset();
	processor_dpcs_queued_per_second.reset();
	processor_percent_privileged_time.reset();
	processor_percent_user_time.reset();
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
			ctString::ctFormatString(
				L"Processor %ws",
				ctString::ctReplaceAllCopy(name, L",", L" - ").c_str()));

        const auto processor_range = processor_time->reference_range(name.c_str());
		vector<ULONGLONG> processor_time_vector(processor_range.first, processor_range.second);

        const auto percent_range = processor_percent_of_max->reference_range(name.c_str());
		vector<ULONG> processor_percent_vector(percent_range.first, percent_range.second);

        const auto percent_dpc_time_range = processor_percent_dpc_time->reference_range(name.c_str());
        const auto dpcs_queued_per_second_range = processor_dpcs_queued_per_second->reference_range(name.c_str());
        const auto processor_percent_privileged_time_range = processor_percent_privileged_time->reference_range(name.c_str());
        const auto processor_percent_user_time_range = processor_percent_user_time->reference_range(name.c_str());

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
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> paged_pool_bytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> non_paged_pool_bytes;
ctWmiPerformance InstantiateMemoryCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);
    paged_pool_bytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Memory,
        L"PoolPagedBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(paged_pool_bytes);
    wprintf(L".");

    non_paged_pool_bytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Memory,
        L"PoolNonpagedBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(non_paged_pool_bytes);
    wprintf(L".");

    return performance_counter;
}
void DeleteMemoryCounters() noexcept
{
    paged_pool_bytes.reset();
    non_paged_pool_bytes.reset();
}
void ProcessMemoryCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;
    const auto paged_pool_range = paged_pool_bytes->reference_range();
    const auto non_paged_pool_range = non_paged_pool_bytes->reference_range();

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
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_adapter_total_bytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_adapter_offloaded_connections;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_adapter_packets_outbound_discarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_adapter_packets_outbound_errors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_adapter_packets_received_discarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_adapter_packets_received_errors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_adapter_packets_per_second;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_adapter_active_rsc_connections;
ctWmiPerformance InstantiateNetworkAdapterCounters(const std::wstring& trackInterfaceDescription)
{
    ctWmiPerformance performance_counter(*g_Wmi);

    network_adapter_total_bytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"BytesTotalPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_total_bytes->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_total_bytes);
    wprintf(L".");

    network_adapter_offloaded_connections = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"OffloadedConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_offloaded_connections->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_offloaded_connections);
    wprintf(L".");

    network_adapter_packets_outbound_discarded = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_outbound_discarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_outbound_discarded);
    wprintf(L".");

    network_adapter_packets_outbound_errors = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsOutboundErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_outbound_errors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_outbound_errors);
    wprintf(L".");

    network_adapter_packets_received_discarded = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_received_discarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_received_discarded);
    wprintf(L".");

    network_adapter_packets_received_errors = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_received_errors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_received_errors);
    wprintf(L".");

    network_adapter_packets_per_second = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"PacketsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_per_second->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_per_second);
    wprintf(L".");

    network_adapter_active_rsc_connections = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkAdapter,
        L"TCPActiveRSCConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_active_rsc_connections->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_active_rsc_connections);
    wprintf(L".");

    return performance_counter;
}
void DeleteNetworkAdapterCounters() noexcept
{
    network_adapter_total_bytes.reset();
    network_adapter_offloaded_connections.reset();
    network_adapter_packets_outbound_discarded.reset();
    network_adapter_packets_outbound_errors.reset();
    network_adapter_packets_received_discarded.reset();
    network_adapter_packets_received_errors.reset();
    network_adapter_packets_per_second.reset();
    network_adapter_active_rsc_connections.reset();
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

        auto network_range = network_adapter_packets_per_second->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        if (g_MeanOnly) {
            writer.write_mean(
                L"NetworkAdapter",
                ctString::ctFormatString(
                    L"PacketsPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.write_details(
                L"NetworkAdapter",
                ctString::ctFormatString(
                    L"PacketsPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        }
        network_range = network_adapter_total_bytes->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        if (g_MeanOnly) {
            writer.write_mean(
                L"NetworkAdapter",
                ctString::ctFormatString(
                    L"BytesTotalPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.write_details(
                L"NetworkAdapter",
                ctString::ctFormatString(
                    L"BytesTotalPersec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        }

        network_range = network_adapter_offloaded_connections->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::ctFormatString(
                L"OffloadedConnections for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_active_rsc_connections->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::ctFormatString(
                L"TCPActiveRSCConnections for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_packets_outbound_discarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::ctFormatString(
                L"PacketsOutboundDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_packets_outbound_errors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::ctFormatString(
                L"PacketsOutboundErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_packets_received_discarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::ctFormatString(
                L"PacketsReceivedDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_packets_received_errors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::ctFormatString(
                L"PacketsReceivedErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

		writer.write_empty_row();
    }
}

/****************************************************************************************************/
/*                                     NetworkInterface                                             */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_interface_total_bytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_interface_packets_outbound_discarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_interface_packets_outbound_errors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_interface_packets_received_discarded;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_interface_packets_received_errors;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_interface_packets_received_unknown;
ctWmiPerformance InstantiateNetworkInterfaceCounters(const std::wstring& trackInterfaceDescription)
{
    ctWmiPerformance performance_counter(*g_Wmi);

    network_interface_total_bytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"BytesTotalPerSec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        network_interface_total_bytes->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_total_bytes);
    wprintf(L".");

    network_interface_packets_outbound_discarded = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_outbound_discarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_outbound_discarded);
    wprintf(L".");

    network_interface_packets_outbound_errors = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsOutboundErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_outbound_errors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_outbound_errors);
    wprintf(L".");

    network_interface_packets_received_discarded = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_received_discarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_received_discarded);
    wprintf(L".");

    network_interface_packets_received_errors = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_received_errors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_received_errors);
    wprintf(L".");

    network_interface_packets_received_unknown = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::NetworkInterface,
        L"PacketsReceivedUnknown",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_received_unknown->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_received_unknown);
    wprintf(L".");

    return performance_counter;
}
void DeleteNetworkInterfaceCounters() noexcept
{
    network_interface_total_bytes.reset();
    network_interface_packets_outbound_discarded.reset();
    network_interface_packets_outbound_errors.reset();
    network_interface_packets_received_discarded.reset();
    network_interface_packets_received_errors.reset();
    network_interface_packets_received_unknown.reset();
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

        const auto byte_range = network_interface_total_bytes->reference_range(name.c_str());
        ullData.assign(byte_range.first, byte_range.second);
        if (g_MeanOnly) {
            writer.write_mean(
                L"NetworkInterface",
                ctString::ctFormatString(
                    L"BytesTotalPerSec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.write_details(
                L"NetworkInterface",
                ctString::ctFormatString(
                    L"BytesTotalPerSec for interface %ws",
                    name.c_str()).c_str(),
                ullData);
        }
        auto network_range = network_interface_packets_outbound_discarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::ctFormatString(
                L"PacketsOutboundDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = network_interface_packets_outbound_errors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::ctFormatString(
                L"PacketsOutboundErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = network_interface_packets_received_discarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::ctFormatString(
                L"PacketsReceivedDiscarded for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = network_interface_packets_received_errors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::ctFormatString(
                L"PacketsReceivedErrors for interface %ws",
                name.c_str()).c_str(),
            ullData);

        network_range = network_interface_packets_received_unknown->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::ctFormatString(
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
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv4_outbound_discarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv4_outbound_no_route;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv4_received_address_errors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv4_received_discarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv4_received_header_errors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv4_received_unknown_protocol;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv4_fragment_reassembly_failures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv4_fragmentation_failures;

shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv6_outbound_discarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv6_outbound_no_route;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv6_received_address_errors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv6_received_discarded;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv6_received_header_errors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv6_received_unknown_protocol;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv6_fragment_reassembly_failures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_ipv6_fragmentation_failures;
ctWmiPerformance InstantiateIPCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);

    tcpip_ipv4_outbound_discarded = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_outbound_discarded);
    wprintf(L".");

    tcpip_ipv4_outbound_no_route = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsOutboundNoRoute",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_outbound_no_route);
    wprintf(L".");

    tcpip_ipv4_received_address_errors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedAddressErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_received_address_errors);
    wprintf(L".");

    tcpip_ipv4_received_discarded = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_received_discarded);
    wprintf(L".");

    tcpip_ipv4_received_header_errors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedHeaderErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_received_header_errors);
    wprintf(L".");

    tcpip_ipv4_received_unknown_protocol = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"DatagramsReceivedUnknownProtocol",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_received_unknown_protocol);
    wprintf(L".");

    tcpip_ipv4_fragment_reassembly_failures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"FragmentReassemblyFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_fragment_reassembly_failures);
    wprintf(L".");

    tcpip_ipv4_fragmentation_failures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv4,
        L"FragmentationFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_fragmentation_failures);
    wprintf(L".");

    tcpip_ipv6_outbound_discarded = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_outbound_discarded);
    wprintf(L".");

    tcpip_ipv6_outbound_no_route = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsOutboundNoRoute",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_outbound_no_route);
    wprintf(L".");

    tcpip_ipv6_received_address_errors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedAddressErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_received_address_errors);
    wprintf(L".");

    tcpip_ipv6_received_discarded = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_received_discarded);
    wprintf(L".");

    tcpip_ipv6_received_header_errors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedHeaderErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_received_header_errors);
    wprintf(L".");

    tcpip_ipv6_received_unknown_protocol = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"DatagramsReceivedUnknownProtocol",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_received_unknown_protocol);
    wprintf(L".");

    tcpip_ipv6_fragment_reassembly_failures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"FragmentReassemblyFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_fragment_reassembly_failures);
    wprintf(L".");

    tcpip_ipv6_fragmentation_failures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipIpv6,
        L"FragmentationFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_fragmentation_failures);
    wprintf(L".");

    return performance_counter;
}
void DeleteIPCounters() noexcept
{
    tcpip_ipv4_outbound_discarded.reset();
    tcpip_ipv4_outbound_no_route.reset();
    tcpip_ipv4_received_address_errors.reset();
    tcpip_ipv4_received_discarded.reset();
    tcpip_ipv4_received_header_errors.reset();
    tcpip_ipv4_received_unknown_protocol.reset();
    tcpip_ipv4_fragment_reassembly_failures.reset();
    tcpip_ipv4_fragmentation_failures.reset();

    tcpip_ipv6_outbound_discarded.reset();
    tcpip_ipv6_outbound_no_route.reset();
    tcpip_ipv6_received_address_errors.reset();
    tcpip_ipv6_received_discarded.reset();
    tcpip_ipv6_received_header_errors.reset();
    tcpip_ipv6_received_unknown_protocol.reset();
    tcpip_ipv6_fragment_reassembly_failures.reset();
    tcpip_ipv6_fragmentation_failures.reset();
}
void ProcessIPCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONG> ulData;

	writer.write_row(L"TCPIP - IPv4");

    auto network_loss_range = tcpip_ipv4_outbound_discarded->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsOutboundDiscarded",
        ulData);

    network_loss_range = tcpip_ipv4_outbound_no_route->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsOutboundNoRoute",
        ulData);

    network_loss_range = tcpip_ipv4_received_address_errors->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedAddressErrors",
        ulData);

    network_loss_range = tcpip_ipv4_received_discarded->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedDiscarded",
        ulData);

    network_loss_range = tcpip_ipv4_received_header_errors->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedHeaderErrors",
        ulData);

    network_loss_range = tcpip_ipv4_received_unknown_protocol->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"DatagramsReceivedUnknownProtocol",
        ulData);

    network_loss_range = tcpip_ipv4_fragment_reassembly_failures->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"FragmentReassemblyFailures",
        ulData);

    network_loss_range = tcpip_ipv4_fragmentation_failures->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv4",
        L"FragmentationFailures",
        ulData);

    network_loss_range = tcpip_ipv6_outbound_discarded->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsOutboundDiscarded",
        ulData);

    network_loss_range = tcpip_ipv6_outbound_no_route->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsOutboundNoRoute",
        ulData);

    network_loss_range = tcpip_ipv6_received_address_errors->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedAddressErrors",
        ulData);

    network_loss_range = tcpip_ipv6_received_discarded->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedDiscarded",
        ulData);

    network_loss_range = tcpip_ipv6_received_header_errors->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedHeaderErrors",
        ulData);

    network_loss_range = tcpip_ipv6_received_unknown_protocol->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"DatagramsReceivedUnknownProtocol",
        ulData);

    network_loss_range = tcpip_ipv6_fragment_reassembly_failures->reference_range();
    ulData.assign(network_loss_range.first, network_loss_range.second);
    writer.write_difference(
        L"TCPIP - IPv6",
        L"FragmentReassemblyFailures",
        ulData);

    network_loss_range = tcpip_ipv6_fragmentation_failures->reference_range();
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
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_tcpv4_connections_established;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_tcpv6_connections_established;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_tcpv4_connection_failures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_tcpv6_connection_failures;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_tcpv4_connections_reset;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_tcpv6_connections_reset;
shared_ptr<ctWmiPerformanceCounter<ULONG>> winsock_bsp_rejected_connections;
shared_ptr<ctWmiPerformanceCounter<ULONG>> winsock_bsp_rejected_connections_per_sec;
ctWmiPerformance InstantiateTCPCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);

    tcpip_tcpv4_connections_established = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv4,
        L"ConnectionsEstablished",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_tcpv4_connections_established);
    wprintf(L".");

    tcpip_tcpv6_connections_established = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv6,
        L"ConnectionsEstablished",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_tcpv6_connections_established);
    wprintf(L".");

    tcpip_tcpv4_connection_failures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv4,
        L"ConnectionFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv4_connection_failures);
    wprintf(L".");

    tcpip_tcpv6_connection_failures = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv6,
        L"ConnectionFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv6_connection_failures);
    wprintf(L".");

    tcpip_tcpv4_connections_reset = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv4,
        L"ConnectionsReset",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv4_connections_reset);
    wprintf(L".");

    tcpip_tcpv6_connections_reset = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipTcpv6,
        L"ConnectionsReset",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv6_connections_reset);
    wprintf(L".");

    winsock_bsp_rejected_connections = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"RejectedConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(winsock_bsp_rejected_connections);
    wprintf(L".");

    winsock_bsp_rejected_connections_per_sec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"RejectedConnectionsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(winsock_bsp_rejected_connections_per_sec);
    wprintf(L".");

    return performance_counter;
}
void DeleteTCPCounters() noexcept
{
    tcpip_tcpv4_connections_established.reset();
    tcpip_tcpv6_connections_established.reset();
    tcpip_tcpv4_connection_failures.reset();
    tcpip_tcpv6_connection_failures.reset();
    tcpip_tcpv4_connections_reset.reset();
    tcpip_tcpv6_connections_reset.reset();
    winsock_bsp_rejected_connections.reset();
    winsock_bsp_rejected_connections_per_sec.reset();
}
void ProcessTCPCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONG> ulData;

	writer.write_row(L"TCPIP - TCPv4");
    auto network_range = tcpip_tcpv4_connections_established->reference_range();
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

    network_range = tcpip_tcpv6_connections_established->reference_range();
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

    network_range = tcpip_tcpv4_connection_failures->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv4",
        L"ConnectionFailures",
        ulData);

    network_range = tcpip_tcpv6_connection_failures->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv6",
        L"ConnectionFailures",
        ulData);

    network_range = tcpip_tcpv4_connections_reset->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv4",
        L"ConnectionsReset",
        ulData);

    network_range = tcpip_tcpv6_connections_reset->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv6",
        L"ConnectionsReset",
        ulData);

    network_range = winsock_bsp_rejected_connections->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"Winsock",
        L"RejectedConnections",
        ulData);

    network_range = winsock_bsp_rejected_connections_per_sec->reference_range();
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
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_udpv4_noport_per_sec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_udpv4_received_errors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_udpv4_datagrams_per_sec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_udpv6_noport_per_sec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_udpv6_received_errors;
shared_ptr<ctWmiPerformanceCounter<ULONG>> tcpip_udpv6_datagrams_per_sec;
shared_ptr<ctWmiPerformanceCounter<ULONG>> winsock_bsp_dropped_datagrams;
shared_ptr<ctWmiPerformanceCounter<ULONG>> winsock_bsp_dropped_datagrams_per_second;
ctWmiPerformance InstantiateUDPCounters()
{
    ctWmiPerformance performance_counter(*g_Wmi);

    tcpip_udpv4_noport_per_sec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv4,
        L"DatagramsNoPortPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_udpv4_noport_per_sec);
    wprintf(L".");

    tcpip_udpv4_received_errors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv4,
        L"DatagramsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_udpv4_received_errors);
    wprintf(L".");

    tcpip_udpv4_datagrams_per_sec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv4,
        L"DatagramsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_udpv4_datagrams_per_sec);
    wprintf(L".");

    tcpip_udpv6_noport_per_sec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv6,
        L"DatagramsNoPortPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_udpv6_noport_per_sec);
    wprintf(L".");

    tcpip_udpv6_received_errors = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv6,
        L"DatagramsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_udpv6_received_errors);
    wprintf(L".");

    tcpip_udpv6_datagrams_per_sec = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::TcpipUdpv6,
        L"DatagramsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_udpv6_datagrams_per_sec);
    wprintf(L".");

    winsock_bsp_dropped_datagrams = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"DroppedDatagrams",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(winsock_bsp_dropped_datagrams);
    wprintf(L".");

    winsock_bsp_dropped_datagrams_per_second = ctCreatePerfCounter<ULONG>(
        *g_Wmi,
        ctWmiEnumClassName::WinsockBsp,
        L"DroppedDatagramsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(winsock_bsp_dropped_datagrams_per_second);
    wprintf(L".");

    return performance_counter;
}
void DeleteUDPCounters() noexcept
{
    tcpip_udpv4_noport_per_sec.reset();
    tcpip_udpv4_received_errors.reset();
    tcpip_udpv4_datagrams_per_sec.reset();
    tcpip_udpv6_noport_per_sec.reset();
    tcpip_udpv6_received_errors.reset();
    tcpip_udpv6_datagrams_per_sec.reset();
    winsock_bsp_dropped_datagrams.reset();
    winsock_bsp_dropped_datagrams_per_second.reset();
}
void ProcessUDPCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONG> ulData;

	writer.write_row(L"TCPIP - UDPv4");

    auto udp_range = tcpip_udpv4_noport_per_sec->reference_range();
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

    udp_range = tcpip_udpv4_datagrams_per_sec->reference_range();
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

	udp_range = tcpip_udpv4_received_errors->reference_range();
	ulData.assign(udp_range.first, udp_range.second);
	writer.write_difference(
		L"TCPIP - UDPv4",
		L"DatagramsReceivedErrors",
		ulData);

	writer.write_empty_row();
	writer.write_row(L"TCPIP - UDPv6");

	udp_range = tcpip_udpv6_noport_per_sec->reference_range();
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

    udp_range = tcpip_udpv6_datagrams_per_sec->reference_range();
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

    udp_range = tcpip_udpv6_received_errors->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    writer.write_difference(
        L"TCPIP - UDPv6",
        L"DatagramsReceivedErrors",
        ulData);

	writer.write_empty_row();
	writer.write_row(L"Winsock Datagrams");

    udp_range = winsock_bsp_dropped_datagrams->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    writer.write_difference(
        L"Winsock",
        L"DroppedDatagrams",
        ulData);

    udp_range = winsock_bsp_dropped_datagrams_per_second->reference_range();
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


shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_privileged_time;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_processor_time;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_user_time;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_private_bytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_virtual_bytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_working_set;
ctWmiPerformance InstantiatePerProcessByNameCounters(const std::wstring& trackProcess)
{
    ctWmiPerformance performance_counter(*g_Wmi);

    // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
    per_process_privileged_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentPrivilegedTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_privileged_time->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_privileged_time);
    wprintf(L".");

    per_process_processor_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentProcessorTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_processor_time->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_processor_time);
    wprintf(L".");

    per_process_user_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentUserTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_user_time->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_user_time);
    wprintf(L".");

    per_process_private_bytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PrivateBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_private_bytes->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_private_bytes);
    wprintf(L".");

    per_process_virtual_bytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"VirtualBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_virtual_bytes->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_virtual_bytes);
    wprintf(L".");

    per_process_working_set = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"WorkingSet",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_working_set->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_working_set);
    wprintf(L".");

    return performance_counter;
}
ctWmiPerformance InstantiatePerProcessByPIDCounters(const DWORD processId)
{
    ctWmiPerformance performance_counter(*g_Wmi);

    // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
    per_process_privileged_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentPrivilegedTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_privileged_time->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_privileged_time);
    wprintf(L".");

    per_process_processor_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentProcessorTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_processor_time->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_processor_time);
    wprintf(L".");

    per_process_user_time = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PercentUserTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_user_time->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_user_time);
    wprintf(L".");

    per_process_private_bytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"PrivateBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_private_bytes->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_private_bytes);
    wprintf(L".");

    per_process_virtual_bytes = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"VirtualBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_virtual_bytes->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_virtual_bytes);
    wprintf(L".");

    per_process_working_set = ctCreatePerfCounter<ULONGLONG>(
        *g_Wmi,
        ctWmiEnumClassName::Process,
        L"WorkingSet",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_working_set->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_working_set);
    wprintf(L".");

    return performance_counter;
}
void DeletePerProcessCounters() noexcept
{
    per_process_privileged_time.reset();
    per_process_processor_time.reset();
    per_process_user_time.reset();
    per_process_private_bytes.reset();
    per_process_virtual_bytes.reset();
    per_process_working_set.reset();
}
void ProcessPerProcessCounters(const wstring& trackProcess, const DWORD processId, ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;

    wstring counter_classname;
    if (!trackProcess.empty()) {
        wstring full_name(trackProcess);
        full_name += L".exe";
        counter_classname = ctString::ctFormatString(L"Process (%ws)", full_name.c_str());

    } else {
        counter_classname = ctString::ctFormatString(L"Process (pid %u)", processId);
    }

    auto per_process_range = per_process_privileged_time->reference_range();
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

    per_process_range = per_process_processor_time->reference_range();
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

    per_process_range = per_process_user_time->reference_range();
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

    per_process_range = per_process_private_bytes->reference_range();
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

    per_process_range = per_process_virtual_bytes->reference_range();
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

    per_process_range = per_process_working_set->reference_range();
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
