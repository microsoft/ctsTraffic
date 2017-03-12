#include <stdio.h>
#include <wchar.h>
#include <vector>
#include <string>
#include <memory>

#include <windows.h>

#include <ctString.hpp>
#include <ctWmiInitialize.hpp>
#include <ctWmiEnumerate.hpp>
#include <ctWmiPerformance.hpp>
#include <ctException.hpp>
#include <ctScopeGuard.hpp>

#include "ctsWriteDetails.h"

using namespace std;
using namespace ctl;

static HANDLE g_hBreak = NULL;
static ctWmiService* g_wmi = nullptr;

BOOL WINAPI BreakHandlerRoutine(DWORD)
{
    // regardless of the break type, signal to exit
    ::SetEvent(g_hBreak);
    return TRUE;
}

static const WCHAR UsageStatement[] = 
    L"ctsPerf.exe usage::\n"
    L" #### <time to run (in seconds)>  [default is 60 seconds]\n"
    L" -MeanOnly  [will save memory by not storing every data point, only a sum and mean\n"
    L" -Filename:########  [default is Performance.csv]\n"
    L"\n"
    L" [optionally additional performance counters]\n"
    L"  -Memory   [will collect paged-pool and non-paged-pool counters]\n"
    L"  -NetworkAdapter\n"
    L"  -NetworkInterface\n"
    L"  -IP   [will collect TCP/IP IPv4 and IPv6 counters]\n"
    L"  -TCP   [will collect TCP/IP TCPv4 and TCPv6 counters]\n"
    L"  -UDP   [will collect TCP/IP UDPv4 and UDPv6 counters]\n"
    L"\n"
    L" [optionally the specific interface description can be specified]\n"
    L"  by default *all* interface counters are collected\n"
    L"  note: the Interface Description can be found from the powershell cmdlet Get-NetAdapter\n"
    L"        or by running ctsPerf.exe and viewing the names from the log file\n"
    L"  -InterfaceDescription #####\n"
    L"\n"
    L" [optionally one of two process identifiers]\n"
    L"  by default is no process tracking\n"
    L"  -process:<process name>\n"
    L"  -pid:<process id>\n"
    L"\n\n"
    L"For example:\n"
    L"> ctsPerf.exe\n"
    L"  -- will capture processor counters for the default 60 seconds\n"
    L"\n"
    L"> ctsPerf.exe -NetworkAdapter -NetworkInterface -IP -TCP\n"
    L"  -- will capture processor, network adapter, network interface, and TCP/IP IP and TCP counters\n"
    L"\n"
    L"> ctsPerf.exe 300 -process:outlook.exe\n"
    L"  -- will capture processor counters + process counters for outlook.exe for 300 seconds"
    L"\n"
    L"> ctsPerf.exe -pid:2048\n"
    L"  -- will capture processor counters + process counters for process id 2048 for 60 seconds"
    L"\n";

// 0 is a possible process ID
static const DWORD UninitializedProcessId = 0xffffffff;

ctWmiPerformance InstantiateProcessorCounters();
ctWmiPerformance InstantiateMemoryCounters();
ctWmiPerformance InstantiateNetworkAdapterCounters(const std::wstring& trackInterfaceDescription);
ctWmiPerformance InstantiateNetworkInterfaceCounters(const std::wstring& trackInterfaceDescription);
ctWmiPerformance InstantiateIPCounters();
ctWmiPerformance InstantiateTCPCounters();
ctWmiPerformance InstantiateUDPCounters();
ctWmiPerformance InstantiatePerProcessByNameCounters(const std::wstring& trackProcess);
ctWmiPerformance InstantiatePerProcessByPIDCounters(const DWORD processId);

void DeleteProcessorCounters();
void DeleteMemoryCounters();
void DeleteNetworkAdapterCounters();
void DeleteNetworkInterfaceCounters();
void DeleteIPCounters();
void DeleteTCPCounters();
void DeleteUDPCounters();
void DeletePerProcessCounters();

void DeleteAllCounters()
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
void ProcessPerProcessCounters(const wstring& trackProcess, const DWORD processId, ctsPerf::ctsWriteDetails& writer);

std::wstring g_FileName = L"ctsPerf.csv";
bool g_MeanOnly = false;

int __cdecl wmain(_In_ int argc, _In_reads_z_(argc) const wchar_t** argv)
{
    bool trackMemory = false;
    bool trackNetworkAdapter = false;
    bool trackNetworkInterface = false;
    bool trackIP = false;
    bool trackTCP = false;
    bool trackUDP = false;

    // create a notification event to signal if the user wants to exit early
    g_hBreak = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (g_hBreak == NULL) {
        DWORD gle = ::GetLastError();
        wprintf(L"Out of resources -- cannot initialize (CreateEvent) (%u)\n", gle);
        return gle;
    }

    if (!::SetConsoleCtrlHandler(BreakHandlerRoutine, TRUE)) {
        DWORD gle = ::GetLastError();
        wprintf(L"Out of resources -- cannot initialize (SetConsoleCtrlHandler) (%u)\n", gle);
        return gle;
    }

    wstring trackInterfaceDescription;
    wstring trackProcess;
    DWORD processId = UninitializedProcessId;
    DWORD timeToRunMs = 60000; // default to 60 seconds
    for (DWORD arg_count = argc; arg_count > 1; --arg_count) {
        if (ctString::istarts_with(argv[arg_count - 1], L"-process:")) {
            trackProcess = argv[arg_count - 1];

            // strip off the "process:" preface to the string
            auto endOfToken = find(trackProcess.begin(), trackProcess.end(), L':');
            trackProcess.erase(trackProcess.begin(), endOfToken + 1);

            // the performance counter does not look at the extension, so remove .exe if it's there
            if (ctString::iends_with(trackProcess, L".exe")) {
                trackProcess.erase(trackProcess.end() - 4, trackProcess.end());
            }
            if (trackProcess.empty()) {
                wprintf(L"Incorrect option: %ws\n", argv[arg_count - 1]);
                wprintf(UsageStatement);
                return 1;
            }
        
        } else if (ctString::istarts_with(argv[arg_count - 1], L"-pid:")) {
            wstring pidString(argv[arg_count - 1]);

            // strip off the "pid:" preface to the string
            auto endOfToken = find(pidString.begin(), pidString.end(), L':');
            pidString.erase(pidString.begin(), endOfToken + 1);

            // the user could have specified zero, which happens to be what is returned from wcstoul on error
            if (pidString == L"0") {
                processId = 0;
            
            } else {
                processId = wcstoul(pidString.c_str(), nullptr, 10);
                if (processId == 0 || processId == ULONG_MAX) {
                    wprintf(L"Incorrect option: %ws\n", argv[arg_count - 1]);
                    wprintf(UsageStatement);
                    return 1;
                }
            }
        
        } else if (ctString::istarts_with(argv[arg_count - 1], L"-memory")) {
            trackMemory = true;
        
        } else if (ctString::istarts_with(argv[arg_count - 1], L"-NetworkAdapter")) {
            trackNetworkAdapter = true;
        
        } else if (ctString::istarts_with(argv[arg_count - 1], L"-NetworkInterface")) {
            trackNetworkInterface = true;

        } else if (ctString::istarts_with(argv[arg_count - 1], L"-IP")) {
            trackIP = true;
        
        } else if (ctString::istarts_with(argv[arg_count - 1], L"-TCP")) {
            trackTCP = true;
        
        } else if (ctString::istarts_with(argv[arg_count - 1], L"-UDP")) {
            trackUDP = true;
        
        } else if (ctString::istarts_with(argv[arg_count - 1], L"-InterfaceDescription:")) {
            trackInterfaceDescription = argv[arg_count - 1];

            // strip off the "-InterfaceDescription:" preface to the string
            auto endOfToken = find(trackInterfaceDescription.begin(), trackInterfaceDescription.end(), L':');
            trackInterfaceDescription.erase(trackInterfaceDescription.begin(), endOfToken + 1);

        } else if (ctString::istarts_with(argv[arg_count - 1], L"-MeanOnly")) {
            g_MeanOnly = true;

        } else if (ctString::istarts_with(argv[arg_count - 1], L"-filename:")) {
            wstring filename_string(argv[arg_count - 1]);

            // strip off the "pid:" preface to the string
            auto endOfToken = find(filename_string.begin(), filename_string.end(), L':');
            filename_string.erase(filename_string.begin(), endOfToken + 1);
            g_FileName = filename_string;

        } else {
            DWORD timeToRun = wcstoul(argv[arg_count - 1], nullptr, 10);
            if (timeToRun == 0 || timeToRun == ULONG_MAX) {
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

    if (timeToRunMs <= 5000) {
        wprintf(L"ERROR: Must run over 5 seconds to have enough samples for analysis\n");
        wprintf(UsageStatement);
        return 1;
    }

    wprintf(L"Instantiating WMI Performance objects (this can take a few seconds)\n");
    ctComInitialize coinit;
    ctWmiService wmi(L"root\\cimv2");
    g_wmi = &wmi;

    try {
        ctlScopeGuard(deleteAllCounters, { DeleteAllCounters(); });

        ctsPerf::ctsWriteDetails writer(g_FileName.c_str(), g_MeanOnly);
        wprintf(L".");

        // create a perf counter objects to maintain these counters
        std::vector<ctWmiPerformance> performance_vector;

        performance_vector.emplace_back(InstantiateProcessorCounters());

        if (trackMemory) {
            performance_vector.emplace_back(InstantiateMemoryCounters());
        }
        if (trackNetworkAdapter) {
            performance_vector.emplace_back(InstantiateNetworkAdapterCounters(trackInterfaceDescription));
        }
        if (trackNetworkInterface) {
            performance_vector.emplace_back(InstantiateNetworkInterfaceCounters(trackInterfaceDescription));
        }
        if (trackIP) {
            performance_vector.emplace_back(InstantiateIPCounters());
        }
        if (trackTCP) {
            performance_vector.emplace_back(InstantiateTCPCounters());
        }
        if (trackUDP) {
            performance_vector.emplace_back(InstantiateUDPCounters());
        }
        if (!trackProcess.empty()) {
            performance_vector.emplace_back(InstantiatePerProcessByNameCounters(trackProcess));
        
        } else if (processId != UninitializedProcessId) {
            performance_vector.emplace_back(InstantiatePerProcessByPIDCounters(processId));
        }

        wprintf(L"\nStarting counters : will run for %lu seconds\n (hit ctrl-c to exit early) ...\n\n", static_cast<DWORD>(timeToRunMs / 1000UL));
        for (auto& perf_object : performance_vector) {
            perf_object.start_all_counters(1000);
        }

        ::WaitForSingleObject(g_hBreak, timeToRunMs);

        wprintf(L"Stopping counters ....\n\n");
        for (auto& perf_object : performance_vector) {
            perf_object.stop_all_counters();
        }

        ProcessProcessorCounters(writer);
        if (trackMemory) {
            ProcessMemoryCounters(writer);
        }
        if (trackNetworkAdapter) {
            ProcessNetworkAdapterCounters(writer);
        }
        if (trackNetworkInterface) {
            ProcessNetworkInterfaceCounters(writer);
        }
        if (trackIP) {
            ProcessIPCounters(writer);
        }
        if (trackTCP) {
            ProcessTCPCounters(writer);
        }
        if (trackUDP) {
            ProcessUDPCounters(writer);
        }
        ProcessPerProcessCounters(trackProcess, processId, writer);
    }
    catch (const exception& e) {
        wprintf(L"ERROR: %s\n", ctString::format_exception(e).c_str());
        return 1;
    }

    CloseHandle(g_hBreak);

    return 0;
}


/****************************************************************************************************/
/*                                         Processor                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processor_time;
shared_ptr<ctWmiPerformanceCounter<ULONG>> processor_percent_of_max;
ctWmiPerformance InstantiateProcessorCounters()
{
    ctWmiPerformance performance_counter;

    // create objects for system counters we care about
    processor_time = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Processor,
        L"PercentProcessorTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(processor_time);
    wprintf(L".");

    processor_percent_of_max = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Processor,
        L"PercentofMaximumFrequency",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(processor_percent_of_max);
    wprintf(L".");

    return performance_counter;
}
void DeleteProcessorCounters()
{
    processor_time.reset();
    processor_percent_of_max.reset();
}
void ProcessProcessorCounters(ctsPerf::ctsWriteDetails& writer)
{
    ctWmiEnumerate enumProcessors(*g_wmi);
    enumProcessors.query(L"SELECT * FROM Win32_PerfFormattedData_Counters_ProcessorInformation");
    if (enumProcessors.begin() == enumProcessors.end()) {
        throw exception("Unable to find any processors to report on - querying Win32_PerfFormattedData_Counters_ProcessorInformation returned nothing");
    }

    for (const auto& processor : enumProcessors) {
        wstring name;
        processor.get(L"Name", &name);

        auto processor_range = processor_time->reference_range(name.c_str());
        auto percent_range = processor_percent_of_max->reference_range(name.c_str());

        vector<ULONGLONG> processor_time_vector(processor_range.first, processor_range.second);
        vector<ULONG> processor_percent_vector(percent_range.first, percent_range.second);

        if (g_MeanOnly) {
            vector<ULONGLONG> normalized_processor_time(processor_time_vector);

            // convert to a percentage
            auto calculated_processor_time = processor_time_vector[3] / 100.0;
            calculated_processor_time *= (processor_percent_vector[3] / 100.0);
            normalized_processor_time[3] = static_cast<ULONG>(calculated_processor_time * 100UL);

            writer.write_mean(
                L"Processor",
                ctString::format_string(
                    L"Raw CPU Usage [%s]",
                    name.c_str()).c_str(),
                processor_time_vector);
            writer.write_mean(
                L"Processor",
                ctString::format_string(
                    L"Normalized CPU Usage (Raw * PercentofMaximumFrequency) [%s]",
                    name.c_str()).c_str(),
                normalized_processor_time);

        } else {
            vector<ULONG> normalized_processor_time;
            // produce the raw % as well as the 'normalized' % based off of the PercentofMaximumFrequency
            auto percentage_iterator(processor_percent_vector.begin());
            for (const auto& processor_data : processor_time_vector) {
                // convert to a percentage
                auto calculated_processor_time = processor_data / 100.0;
                calculated_processor_time *= (*percentage_iterator / 100.0);

                normalized_processor_time.push_back(static_cast<ULONG>(calculated_processor_time * 100UL));
                ++percentage_iterator;
            }

            writer.write_details(
                L"Processor",
                ctString::format_string(
                    L"Raw CPU Usage [%s]",
                    name.c_str()).c_str(),
                processor_time_vector);
            writer.write_details(
                L"Processor",
                ctString::format_string(
                    L"Normalized CPU Usage (Raw * PercentofMaximumFrequency) [%s]",
                    name.c_str()).c_str(),
                normalized_processor_time);
        }
    }
}

/****************************************************************************************************/
/*                                            Memory                                                */
/****************************************************************************************************/
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> paged_pool_bytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> non_paged_pool_bytes;
ctWmiPerformance InstantiateMemoryCounters()
{
    ctWmiPerformance performance_counter;
    paged_pool_bytes = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Memory,
        L"PoolPagedBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(paged_pool_bytes);
    wprintf(L".");

    non_paged_pool_bytes = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Memory,
        L"PoolNonpagedBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(non_paged_pool_bytes);
    wprintf(L".");
    
    return performance_counter;
}
void DeleteMemoryCounters()
{
    paged_pool_bytes.reset();
    non_paged_pool_bytes.reset();
}
void ProcessMemoryCounters(ctsPerf::ctsWriteDetails& writer)
{
    vector<ULONGLONG> ullData;
    auto paged_pool_range = paged_pool_bytes->reference_range();
    auto non_paged_pool_range = non_paged_pool_bytes->reference_range();

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
    ctWmiPerformance performance_counter;

    network_adapter_total_bytes = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkAdapter,
        L"BytesTotalPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_total_bytes->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_total_bytes);
    wprintf(L".");

    network_adapter_offloaded_connections = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkAdapter,
        L"OffloadedConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_offloaded_connections->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_offloaded_connections);
    wprintf(L".");

    network_adapter_packets_outbound_discarded = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkAdapter,
        L"PacketsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_outbound_discarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_outbound_discarded);
    wprintf(L".");

    network_adapter_packets_outbound_errors = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkAdapter,
        L"PacketsOutboundErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_outbound_errors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_outbound_errors);
    wprintf(L".");

    network_adapter_packets_received_discarded = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkAdapter,
        L"PacketsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_received_discarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_received_discarded);
    wprintf(L".");

    network_adapter_packets_received_errors = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkAdapter,
        L"PacketsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_received_errors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_received_errors);
    wprintf(L".");

    network_adapter_packets_per_second = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkAdapter,
        L"PacketsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_packets_per_second->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_packets_per_second);
    wprintf(L".");

    network_adapter_active_rsc_connections = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkAdapter,
        L"TCPActiveRSCConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_adapter_active_rsc_connections->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_adapter_active_rsc_connections);
    wprintf(L".");

    return performance_counter;
}
void DeleteNetworkAdapterCounters()
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
    ctWmiEnumerate enumAdapter(*g_wmi);
    enumAdapter.query(L"SELECT * FROM Win32_PerfFormattedData_Tcpip_NetworkAdapter");
    if (enumAdapter.begin() == enumAdapter.end()) {
        throw exception("Unable to find an adapter to report on - querying Win32_PerfFormattedData_Tcpip_NetworkAdapter returned nothing");
    }

    for (const auto& _adapter : enumAdapter) {
        wstring name;
        _adapter.get(L"Name", &name);

        auto network_range = network_adapter_packets_per_second->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        if (g_MeanOnly) {
            writer.write_mean(
                L"NetworkAdapter",
                ctString::format_string(
                    L"PacketsPersec for interface %s",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.write_details(
                L"NetworkAdapter",
                ctString::format_string(
                    L"PacketsPersec for interface %s",
                    name.c_str()).c_str(),
                ullData);
        }
        network_range = network_adapter_total_bytes->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        if (g_MeanOnly) {
            writer.write_mean(
                L"NetworkAdapter",
                ctString::format_string(
                    L"BytesTotalPersec for interface %s",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.write_details(
                L"NetworkAdapter",
                ctString::format_string(
                    L"BytesTotalPersec for interface %s",
                    name.c_str()).c_str(),
                ullData);
        }

        network_range = network_adapter_offloaded_connections->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::format_string(
                L"OffloadedConnections for interface %s",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_active_rsc_connections->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::format_string(
                L"TCPActiveRSCConnections for interface %s",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_packets_outbound_discarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::format_string(
                L"PacketsOutboundDiscarded for interface %s",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_packets_outbound_errors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::format_string(
                L"PacketsOutboundErrors for interface %s",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_packets_received_discarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::format_string(
                L"PacketsReceivedDiscarded for interface %s",
                name.c_str()).c_str(),
            ullData);

        network_range = network_adapter_packets_received_errors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkAdapter",
            ctString::format_string(
                L"PacketsReceivedErrors for interface %s",
                name.c_str()).c_str(),
            ullData);
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
    ctWmiPerformance performance_counter;

    network_interface_total_bytes = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkInterface,
        L"BytesTotalPerSec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    if (!trackInterfaceDescription.empty()) {
        network_interface_total_bytes->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_total_bytes);
    wprintf(L".");

    network_interface_packets_outbound_discarded = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkInterface,
        L"PacketsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_outbound_discarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_outbound_discarded);
    wprintf(L".");

    network_interface_packets_outbound_errors = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkInterface,
        L"PacketsOutboundErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_outbound_errors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_outbound_errors);
    wprintf(L".");

    network_interface_packets_received_discarded = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkInterface,
        L"PacketsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_received_discarded->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_received_discarded);
    wprintf(L".");

    network_interface_packets_received_errors = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkInterface,
        L"PacketsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_received_errors->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_received_errors);
    wprintf(L".");

    network_interface_packets_received_unknown = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::NetworkInterface,
        L"PacketsReceivedUnknown",
        ctWmiPerformanceCollectionType::FirstLast);
    if (!trackInterfaceDescription.empty()) {
        network_interface_packets_received_unknown->add_filter(L"Name", trackInterfaceDescription.c_str());
    }
    performance_counter.add_counter(network_interface_packets_received_unknown);
    wprintf(L".");

    return performance_counter;
}
void DeleteNetworkInterfaceCounters()
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
    // - it is not guaranteed to match anything from NetAdapter or NetIPInteface
    // - making a single query directly here to at least get the names
    ctWmiEnumerate enumAdapter(*g_wmi);
    enumAdapter.query(L"SELECT * FROM Win32_PerfFormattedData_Tcpip_NetworkInterface");
    if (enumAdapter.begin() == enumAdapter.end()) {
        throw exception("Unable to find an adapter to report on - querying Win32_PerfFormattedData_Tcpip_NetworkInterface returned nothing");
    }

    for (const auto& _adapter : enumAdapter) {
        wstring name;
        _adapter.get(L"Name", &name);

        auto byte_range = network_interface_total_bytes->reference_range(name.c_str());
        ullData.assign(byte_range.first, byte_range.second);
        if (g_MeanOnly) {
            writer.write_mean(
                L"NetworkInterface",
                ctString::format_string(
                    L"BytesTotalPerSec for interface %s",
                    name.c_str()).c_str(),
                ullData);
        } else {
            writer.write_details(
                L"NetworkInterface",
                ctString::format_string(
                    L"BytesTotalPerSec for interface %s",
                    name.c_str()).c_str(),
                ullData);
        }
        auto network_range = network_interface_packets_outbound_discarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::format_string(
                L"PacketsOutboundDiscarded for interface %s",
                name.c_str()).c_str(),
            ullData);

        network_range = network_interface_packets_outbound_errors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::format_string(
                L"PacketsOutboundErrors for interface %s",
                name.c_str()).c_str(),
            ullData);

        network_range = network_interface_packets_received_discarded->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::format_string(
                L"PacketsReceivedDiscarded for interface %s",
                name.c_str()).c_str(),
            ullData);

        network_range = network_interface_packets_received_errors->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::format_string(
                L"PacketsReceivedErrors for interface %s",
                name.c_str()).c_str(),
            ullData);

        network_range = network_interface_packets_received_unknown->reference_range(name.c_str());
        ullData.assign(network_range.first, network_range.second);
        writer.write_difference(
            L"NetworkInterface",
            ctString::format_string(
                L"PacketsReceivedUnknown for interface %s",
                name.c_str()).c_str(),
            ullData);
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
    ctWmiPerformance performance_counter;

    tcpip_ipv4_outbound_discarded = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv4,
        L"DatagramsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_outbound_discarded);
    wprintf(L".");

    tcpip_ipv4_outbound_no_route = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv4,
        L"DatagramsOutboundNoRoute",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_outbound_no_route);
    wprintf(L".");

    tcpip_ipv4_received_address_errors = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv4,
        L"DatagramsReceivedAddressErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_received_address_errors);
    wprintf(L".");

    tcpip_ipv4_received_discarded = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv4,
        L"DatagramsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_received_discarded);
    wprintf(L".");

    tcpip_ipv4_received_header_errors = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv4,
        L"DatagramsReceivedHeaderErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_received_header_errors);
    wprintf(L".");

    tcpip_ipv4_received_unknown_protocol = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv4,
        L"DatagramsReceivedUnknownProtocol",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_received_unknown_protocol);
    wprintf(L".");

    tcpip_ipv4_fragment_reassembly_failures = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv4,
        L"FragmentReassemblyFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_fragment_reassembly_failures);
    wprintf(L".");

    tcpip_ipv4_fragmentation_failures = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv4,
        L"FragmentationFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv4_fragmentation_failures);
    wprintf(L".");

    tcpip_ipv6_outbound_discarded = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv6,
        L"DatagramsOutboundDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_outbound_discarded);
    wprintf(L".");

    tcpip_ipv6_outbound_no_route = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv6,
        L"DatagramsOutboundNoRoute",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_outbound_no_route);
    wprintf(L".");

    tcpip_ipv6_received_address_errors = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv6,
        L"DatagramsReceivedAddressErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_received_address_errors);
    wprintf(L".");

    tcpip_ipv6_received_discarded = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv6,
        L"DatagramsReceivedDiscarded",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_received_discarded);
    wprintf(L".");

    tcpip_ipv6_received_header_errors = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv6,
        L"DatagramsReceivedHeaderErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_received_header_errors);
    wprintf(L".");

    tcpip_ipv6_received_unknown_protocol = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv6,
        L"DatagramsReceivedUnknownProtocol",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_received_unknown_protocol);
    wprintf(L".");

    tcpip_ipv6_fragment_reassembly_failures = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv6,
        L"FragmentReassemblyFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_fragment_reassembly_failures);
    wprintf(L".");

    tcpip_ipv6_fragmentation_failures = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_Ipv6,
        L"FragmentationFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_ipv6_fragmentation_failures);
    wprintf(L".");

    return performance_counter;
}
void DeleteIPCounters()
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
    ctWmiPerformance performance_counter;

    tcpip_tcpv4_connections_established = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_TCPv4,
        L"ConnectionsEstablished",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv4_connections_established);
    wprintf(L".");

    tcpip_tcpv6_connections_established = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_TCPv6,
        L"ConnectionsEstablished",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv6_connections_established);
    wprintf(L".");

    tcpip_tcpv4_connection_failures = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_TCPv4,
        L"ConnectionFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv4_connection_failures);
    wprintf(L".");

    tcpip_tcpv6_connection_failures = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_TCPv6,
        L"ConnectionFailures",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv6_connection_failures);
    wprintf(L".");
    
    tcpip_tcpv4_connections_reset = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_TCPv4,
        L"ConnectionsReset",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv4_connections_reset);
    wprintf(L".");

    tcpip_tcpv6_connections_reset = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_TCPv6,
        L"ConnectionsReset",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_tcpv6_connections_reset);
    wprintf(L".");

    winsock_bsp_rejected_connections = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::WinsockBSP,
        L"RejectedConnections",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(winsock_bsp_rejected_connections);
    wprintf(L".");

    winsock_bsp_rejected_connections_per_sec = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::WinsockBSP,
        L"RejectedConnectionsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(winsock_bsp_rejected_connections_per_sec);
    wprintf(L".");

    return performance_counter;
}
void DeleteTCPCounters()
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

    auto network_range = tcpip_tcpv4_connections_established->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv4",
        L"ConnectionsEstablished",
        ulData);

    network_range = tcpip_tcpv6_connections_established->reference_range();
    ulData.assign(network_range.first, network_range.second);
    writer.write_difference(
        L"TCPIP - TCPv6",
        L"ConnectionsEstablished",
        ulData);

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
    ctWmiPerformance performance_counter;

    tcpip_udpv4_noport_per_sec = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_UDPv4,
        L"DatagramsNoPortPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_udpv4_noport_per_sec);
    wprintf(L".");

    tcpip_udpv4_received_errors = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_UDPv4,
        L"DatagramsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_udpv4_received_errors);
    wprintf(L".");

    tcpip_udpv4_datagrams_per_sec = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_UDPv4,
        L"DatagramsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_udpv4_datagrams_per_sec);
    wprintf(L".");

    tcpip_udpv6_noport_per_sec = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_UDPv6,
        L"DatagramsNoPortPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_udpv6_noport_per_sec);
    wprintf(L".");

    tcpip_udpv6_received_errors = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_UDPv6,
        L"DatagramsReceivedErrors",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(tcpip_udpv6_received_errors);
    wprintf(L".");

    tcpip_udpv6_datagrams_per_sec = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::Tcpip_UDPv6,
        L"DatagramsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(tcpip_udpv6_datagrams_per_sec);
    wprintf(L".");

    winsock_bsp_dropped_datagrams = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::WinsockBSP,
        L"DroppedDatagrams",
        ctWmiPerformanceCollectionType::FirstLast);
    performance_counter.add_counter(winsock_bsp_dropped_datagrams);
    wprintf(L".");

    winsock_bsp_dropped_datagrams_per_second = ctCreatePerfCounter<ULONG>(
        ctWmiClassName::WinsockBSP,
        L"DroppedDatagramsPersec",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    performance_counter.add_counter(winsock_bsp_dropped_datagrams_per_second);
    wprintf(L".");

    return performance_counter;
}
void DeleteUDPCounters()
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

    udp_range = tcpip_udpv4_received_errors->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    writer.write_difference(
        L"TCPIP - UDPv4",
        L"DatagramsReceivedErrors",
        ulData);

    udp_range = tcpip_udpv6_received_errors->reference_range();
    ulData.assign(udp_range.first, udp_range.second);
    writer.write_difference(
        L"TCPIP - UDPv6",
        L"DatagramsReceivedErrors",
        ulData);

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
}


shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_privileged_time;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_processor_time;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_user_time;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_private_bytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_virtual_bytes;
shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_working_set;
ctWmiPerformance InstantiatePerProcessByNameCounters(const std::wstring& trackProcess)
{
    ctWmiPerformance performance_counter;

    // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
    per_process_privileged_time = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"PercentPrivilegedTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_privileged_time->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_privileged_time);
    wprintf(L".");

    per_process_processor_time = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"PercentProcessorTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_processor_time->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_processor_time);
    wprintf(L".");

    per_process_user_time = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"PercentUserTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_user_time->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_user_time);
    wprintf(L".");

    per_process_private_bytes = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"PrivateBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_private_bytes->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_private_bytes);
    wprintf(L".");

    per_process_virtual_bytes = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"VirtualBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_virtual_bytes->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_virtual_bytes);
    wprintf(L".");

    per_process_working_set = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"WorkingSet",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_working_set->add_filter(L"Name", trackProcess.c_str());
    performance_counter.add_counter(per_process_working_set);
    wprintf(L".");

    return performance_counter;
}
ctWmiPerformance InstantiatePerProcessByPIDCounters(const DWORD processId)
{
    ctWmiPerformance performance_counter;

    // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
    per_process_privileged_time = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"PercentPrivilegedTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_privileged_time->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_privileged_time);
    wprintf(L".");

    per_process_processor_time = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"PercentProcessorTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_processor_time->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_processor_time);
    wprintf(L".");

    per_process_user_time = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"PercentUserTime",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_user_time->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_user_time);
    wprintf(L".");

    per_process_private_bytes = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"PrivateBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_private_bytes->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_private_bytes);
    wprintf(L".");

    per_process_virtual_bytes = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"VirtualBytes",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_virtual_bytes->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_virtual_bytes);
    wprintf(L".");

    per_process_working_set = ctCreatePerfCounter<ULONGLONG>(
        ctWmiClassName::Process,
        L"WorkingSet",
        g_MeanOnly ? ctWmiPerformanceCollectionType::MeanOnly : ctWmiPerformanceCollectionType::Detailed);
    per_process_working_set->add_filter(L"IDProcess", processId);
    performance_counter.add_counter(per_process_working_set);
    wprintf(L".");

    return performance_counter;
}
void DeletePerProcessCounters()
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
    if (!trackProcess.empty() || processId != UninitializedProcessId)
    {
        vector<ULONGLONG> ullData;

        wstring counter_classname;
        if (!trackProcess.empty()) {
            wstring full_name(trackProcess);
            full_name += L".exe";
            counter_classname = ctString::format_string(L"Process (%s)", full_name.c_str());
        
        } else {
            counter_classname = ctString::format_string(L"Process (pid %u)", processId);
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
}
