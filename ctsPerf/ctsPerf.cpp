#include <stdio.h>
#include <vector>
#include <string>
#include <memory>

#include <windows.h>

#include <ctString.hpp>
#include <ctWmiInitialize.hpp>
#include <ctWmiEnumerate.hpp>
#include <ctWmiPerformance.hpp>
#include <ctException.hpp>

#include "ctsWriteDetails.hpp"

using namespace std;
using namespace ctl;

static HANDLE hBreak = NULL;

BOOL WINAPI BreakHandlerRoutine(DWORD)
{
    // regardless of the break type, signal to exit
    ::SetEvent(hBreak);
    return TRUE;
}


static const WCHAR UsageStatement[] = L"Invalid arguments: ctsPerf accepts two optional arguments:\n"
                                      L"  <time to run (in seconds)>  [default is 60 seconds]\n"
                                      L" and optionally one of two process identifiers [default is no process tracking]\n"
                                      L"  -process:<process name>\n"
                                      L"  -pid:<process id>\n"
                                      L" For example:\n"
                                      L"\nctsPerf.exe\n"
                                      L"  -- will capture system counters for the default 60 seconds\n"
                                      L"\nctsPerf.exe 300 -process:outlook.exe\n"
                                      L"  -- will capture system counters + process counters for outlook.exe for 300 seconds"
                                      L"\nctsPerf.exe -pid:2048\n"
                                      L"  -- will capture system counters + process counters for process id 2048 for 60 seconds";

// 0 is a possible process ID
static const DWORD UninitializedProcessId = 0xffffffff;

int __cdecl wmain(_In_ int argc, _In_reads_z_(argc) const wchar_t** argv)
{
    wstring trackProcess;
    DWORD processId = UninitializedProcessId;
    DWORD timeToRunMs = 60000; // default to 60 seconds

    // create a notification event to signal if the user wants to exit early
    hBreak = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (hBreak == NULL)
    {
        DWORD gle = ::GetLastError();
        wprintf(L"Out of resources -- cannot initialize (CreateEvent) (%u)\n", gle);
        return 1;
    }

    if (!::SetConsoleCtrlHandler(BreakHandlerRoutine, TRUE))
    {
        DWORD gle = ::GetLastError();
        wprintf(L"Out of resources -- cannot initialize (SetConsoleCtrlHandler) (%u)\n", gle);
        return 1;
    }

    if (argc > 3)
    {
        wprintf(UsageStatement);
        return 1;
    }

    for (DWORD arg_count = argc; arg_count > 1; --arg_count)
    {
        if (ctString::istarts_with(argv[arg_count - 1], L"-process:"))
        {
            trackProcess = argv[arg_count - 1];

            // strip off the "process:" preface to the string
            auto endOfToken = find(trackProcess.begin(), trackProcess.end(), L':');
            trackProcess.erase(trackProcess.begin(), endOfToken + 1);

            // the performance counter does not look at the extension, so remove .exe if it's there
            if (ctString::iends_with(trackProcess, L".exe"))
            {
                trackProcess.erase(trackProcess.end() - 4, trackProcess.end());
            }
            if (trackProcess.empty())
            {
                wprintf(UsageStatement);
                return 1;
            }
        }
        else if (ctString::istarts_with(argv[arg_count - 1], L"-pid:"))
        {
            wstring pidString(argv[arg_count - 1]);

            // strip off the "pid:" preface to the string
            auto endOfToken = find(pidString.begin(), pidString.end(), L':');
            pidString.erase(pidString.begin(), endOfToken + 1);

            // the user could have specified zero, which happens to be what is returned from wcstoul on error
            if (pidString == L"0")
            {
                processId = 0;
            }
            else
            {
                processId = wcstoul(pidString.c_str(), nullptr, 10);
                if (processId == 0 || processId == ULONG_MAX)
                {
                    wprintf(UsageStatement);
                    return 1;
                }
            }
        }
        else
        {
            DWORD timeToRun = wcstoul(argv[arg_count - 1], nullptr, 10);
            if (timeToRun == 0 || timeToRun == ULONG_MAX)
            {
                wprintf(UsageStatement);
                return 1;
            }
            timeToRunMs = timeToRun * 1000;
            if (timeToRunMs < timeToRun)
            {
                wprintf(UsageStatement);
                return 1;
            }
        }
    }

    if (timeToRunMs <= 5000)
    {
        wprintf(L"ERROR: Must run over 5 seconds to have enough samples for analysis\n");
        wprintf(UsageStatement);
        return 1;
    }

    wprintf(L"Instantiating WMI Performance objects (this can take a few seconds)\n");
    ctWmiService wmi(L"root\\cimv2");

    try {
        ctsPerf::ctsWriteDetails writer(L"Performance.csv");

        // create objects for system counters we care about
        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> processor_time(ctSharedProcessorPerfCounter<ULONGLONG>(L"PercentProcessorTime"));
        shared_ptr<ctWmiPerformanceCounter<ULONG>> processor_percent_of_max(ctSharedProcessorPerfCounter<ULONG>(L"PercentofMaximumFrequency"));
        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> network_total_bytes(ctSharedNetworkInterfacePerfCounter<ULONGLONG>(L"BytesTotalPerSec"));
        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> paged_pool_bytes(ctSharedMemoryPerfCounter<ULONGLONG>(L"PoolPagedBytes"));
        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> non_paged_pool_bytes(ctSharedMemoryPerfCounter<ULONGLONG>(L"PoolNonpagedBytes"));

        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_privileged_time;
        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_processor_time;
        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_user_time;
        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_private_bytes;
        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_virtual_bytes;
        shared_ptr<ctWmiPerformanceCounter<ULONGLONG>> per_process_working_set;

        shared_ptr<ctWmiPerformanceCounter<ULONG>> packets_outbound_discarded(ctSharedNetworkInterfacePerfCounter<ULONG>(L"PacketsOutboundDiscarded"));
        shared_ptr<ctWmiPerformanceCounter<ULONG>> packets_outbound_errors(ctSharedNetworkInterfacePerfCounter<ULONG>(L"PacketsOutboundErrors"));
        shared_ptr<ctWmiPerformanceCounter<ULONG>> packets_received_discarded(ctSharedNetworkInterfacePerfCounter<ULONG>(L"PacketsReceivedDiscarded"));
        shared_ptr<ctWmiPerformanceCounter<ULONG>> packets_received_errors(ctSharedNetworkInterfacePerfCounter<ULONG>(L"PacketsReceivedErrors"));
        shared_ptr<ctWmiPerformanceCounter<ULONG>> packets_received_unknown(ctSharedNetworkInterfacePerfCounter<ULONG>(L"PacketsReceivedUnknown"));

        if (!trackProcess.empty())
        {
            // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
            per_process_privileged_time = ctSharedProcessPerfCounter<ULONGLONG>(L"PercentPrivilegedTime");
            per_process_privileged_time->add_filter(L"Name", trackProcess.c_str());

            per_process_processor_time = ctSharedProcessPerfCounter<ULONGLONG>(L"PercentProcessorTime");
            per_process_processor_time->add_filter(L"Name", trackProcess.c_str());

            per_process_user_time = ctSharedProcessPerfCounter<ULONGLONG>(L"PercentUserTime");
            per_process_user_time->add_filter(L"Name", trackProcess.c_str());

            per_process_private_bytes = ctSharedProcessPerfCounter<ULONGLONG>(L"PrivateBytes");
            per_process_private_bytes->add_filter(L"Name", trackProcess.c_str());

            per_process_virtual_bytes = ctSharedProcessPerfCounter<ULONGLONG>(L"VirtualBytes");
            per_process_virtual_bytes->add_filter(L"Name", trackProcess.c_str());

            per_process_working_set = ctSharedProcessPerfCounter<ULONGLONG>(L"WorkingSet");
            per_process_working_set->add_filter(L"Name", trackProcess.c_str());
        }
        else if (processId != UninitializedProcessId)
        {
            // PercentPrivilegedTime, PercentProcessorTime, PercentUserTime, PrivateBytes, VirtualBytes, WorkingSet
            per_process_privileged_time = ctSharedProcessPerfCounter<ULONGLONG>(L"PercentPrivilegedTime");
            per_process_privileged_time->add_filter(L"IDProcess", processId);

            per_process_processor_time = ctSharedProcessPerfCounter<ULONGLONG>(L"PercentProcessorTime");
            per_process_processor_time->add_filter(L"IDProcess", processId);

            per_process_user_time = ctSharedProcessPerfCounter<ULONGLONG>(L"PercentUserTime");
            per_process_user_time->add_filter(L"IDProcess", processId);

            per_process_private_bytes = ctSharedProcessPerfCounter<ULONGLONG>(L"PrivateBytes");
            per_process_private_bytes->add_filter(L"IDProcess", processId);

            per_process_virtual_bytes = ctSharedProcessPerfCounter<ULONGLONG>(L"VirtualBytes");
            per_process_virtual_bytes->add_filter(L"IDProcess", processId);

            per_process_working_set = ctSharedProcessPerfCounter<ULONGLONG>(L"WorkingSet");
            per_process_working_set->add_filter(L"IDProcess", processId);
        }

        // create a perf counter object to maintain these counters
        ctWmiPerformance performance_counter;
        performance_counter.add_counter(processor_time);
        performance_counter.add_counter(processor_percent_of_max);
        performance_counter.add_counter(network_total_bytes);
        performance_counter.add_counter(paged_pool_bytes);
        performance_counter.add_counter(non_paged_pool_bytes);
        if (!trackProcess.empty() || processId != UninitializedProcessId)
        {
            performance_counter.add_counter(per_process_privileged_time);
            performance_counter.add_counter(per_process_processor_time);
            performance_counter.add_counter(per_process_user_time);
            performance_counter.add_counter(per_process_private_bytes);
            performance_counter.add_counter(per_process_virtual_bytes);
            performance_counter.add_counter(per_process_working_set);
        }

        wprintf(L".... starting counters : will run for %lu seconds\n (hit ctrl-c to exit early) ....\n\n", static_cast<DWORD>(timeToRunMs / 1000UL));
        performance_counter.start_all_counters(1000);
        ::WaitForSingleObject(hBreak, timeToRunMs);
        performance_counter.stop_all_counters();
        wprintf(L".... stopping counters ....\n\n");

        // reusable vectors to store & sort data
        vector<ULONGLONG> ullData;
        vector<ULONG> ulData;

        // process each counter
        auto paged_pool_range = paged_pool_bytes->reference_range();
        ullData.assign(paged_pool_range.first, paged_pool_range.second);
        writer.write_details(
            L"Memory",
            L"PoolPagedBytes",
            ullData);

        auto non_paged_pool_range = non_paged_pool_bytes->reference_range();
        ullData.assign(non_paged_pool_range.first, non_paged_pool_range.second);
        writer.write_details(
            L"Memory",
            L"PoolNonpagedBytes",
            ullData);


        ctWmiEnumerate enumProcessors(wmi);
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
            vector<ULONG> normalized_processor_time;

            // produce the raw % as well as the 'normalized' % based off of the PercentofMaximumFrequency
            auto percentage_iterator(processor_percent_vector.begin());
            for (const auto& processor_data : processor_time_vector) {
                auto calculated_processor_time = processor_data / 100.0; // convert to a percentage
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

        // there is no great way to find the 'Name' for each network interface tracked
        // - it is not guaranteed to match anything from NetAdapter or NetIPInteface
        // - making a single query directly here to at least get the names
        ctWmiEnumerate enumAdapter(wmi);
        enumAdapter.query(L"SELECT * FROM Win32_PerfFormattedData_Tcpip_NetworkInterface");
        if (enumAdapter.begin() == enumAdapter.end()) {
            throw exception("Unable to find an adapter to report on - querying Win32_PerfFormattedData_Tcpip_NetworkInterface returned nothing");
        }

        for (const auto& _adapter : enumAdapter) {
            wstring name;
            _adapter.get(L"Name", &name);

            auto network_range = network_total_bytes->reference_range(name.c_str());
            ullData.assign(network_range.first, network_range.second);
            sort(ullData.begin(), ullData.end());

            writer.write_details(
                L"NetworkInterface",
                ctString::format_string(
                    L"BytesTotalPerSec for interface %s",
                    name.c_str()).c_str(),
                ullData);
        }

        if (!trackProcess.empty() || processId != UninitializedProcessId)
        {
            wstring counter_classname;
            
            if (!trackProcess.empty())
            {
                trackProcess += L".exe";
                counter_classname = ctString::format_string(L"Process (%s)", trackProcess.c_str());
            }
            else
            {
                counter_classname = ctString::format_string(L"Process (pid %u)", processId);
            }
            
            auto per_process_range = per_process_privileged_time->reference_range();
            ullData.assign(per_process_range.first, per_process_range.second);
            writer.write_details(
                counter_classname.c_str(),
                L"PercentPrivilegedTime",
                ullData);

            per_process_range = per_process_processor_time->reference_range();
            ullData.assign(per_process_range.first, per_process_range.second);
            writer.write_details(
                counter_classname.c_str(),
                L"PercentProcessorTime",
                ullData);

            per_process_range = per_process_user_time->reference_range();
            ullData.assign(per_process_range.first, per_process_range.second);
            writer.write_details(
                counter_classname.c_str(),
                L"PercentUserTime",
                ullData);

            per_process_range = per_process_private_bytes->reference_range();
            ullData.assign(per_process_range.first, per_process_range.second);
            writer.write_details(
                counter_classname.c_str(),
                L"PrivateBytes",
                ullData);

            per_process_range = per_process_virtual_bytes->reference_range();
            ullData.assign(per_process_range.first, per_process_range.second);
            writer.write_details(
                counter_classname.c_str(),
                L"VirtualBytes",
                ullData);

            per_process_range = per_process_working_set->reference_range();
            ullData.assign(per_process_range.first, per_process_range.second);
            writer.write_details(
                counter_classname.c_str(),
                L"WorkingSet",
                ullData);
        }


        for (const auto& _adapter : enumAdapter) {
            wstring name;
            _adapter.get(L"Name", &name);

            auto network_loss_range = packets_outbound_discarded->reference_range();
            ullData.assign(network_loss_range.first, network_loss_range.second);
            writer.write_sum(
                L"NetworkInterface",
                ctString::format_string(
                    L"PacketsOutboundDiscarded for interface %s",
                    name.c_str()).c_str(),
                ullData);

            network_loss_range = packets_outbound_errors->reference_range();
            ullData.assign(network_loss_range.first, network_loss_range.second);
            writer.write_sum(
                L"NetworkInterface",
                ctString::format_string(
                    L"PacketsOutboundErrors for interface %s",
                    name.c_str()).c_str(),
                ullData);

            network_loss_range = packets_received_discarded->reference_range();
            ullData.assign(network_loss_range.first, network_loss_range.second);
            writer.write_sum(
                L"NetworkInterface",
                ctString::format_string(
                    L"PacketsReceivedDiscarded for interface %s",
                    name.c_str()).c_str(),
                ullData);

            network_loss_range = packets_received_errors->reference_range();
            ullData.assign(network_loss_range.first, network_loss_range.second);
            writer.write_sum(
                L"NetworkInterface",
                ctString::format_string(
                    L"PacketsReceivedErrors for interface %s",
                    name.c_str()).c_str(),
                ullData);

            network_loss_range = packets_received_unknown->reference_range();
            ullData.assign(network_loss_range.first, network_loss_range.second);
            writer.write_sum(
                L"NetworkInterface",
                ctString::format_string(
                    L"PacketsReceivedUnknown for interface %s",
                    name.c_str()).c_str(),
                ullData);
        }
    }
    catch (const exception& e) {
        wprintf(L"ERROR: %s\n", ctString::format_exception(e).c_str());
        return 1;
    }

    CloseHandle(hBreak);

    return 0;
}
