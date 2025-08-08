#include "CppUnitTest.h"

#include "ctsPrintStatus.hpp"

ctsTraffic::ctsConfig::ctsConfigSettings* ctsTraffic::ctsConfig::g_configSettings;

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace ctsPrintStatusUnitTest
{
	TEST_CLASS(ctsPrintStatusUnitTest)
	{
	public:
		TEST_CLASS_INITIALIZE(TestInit)
		{
			ctsTraffic::ctsConfig::g_configSettings = new ctsTraffic::ctsConfig::ctsConfigSettings;
		}

		TEST_CLASS_CLEANUP(TestCleanup)
		{
			delete ctsTraffic::ctsConfig::g_configSettings;
		}

		TEST_METHOD_INITIALIZE(TestcaseInit)
		{
			ctsTraffic::ctsConfig::g_configSettings->TcpStatusDetails.m_bytesSent.SetValue(0);
			ctsTraffic::ctsConfig::g_configSettings->TcpStatusDetails.m_bytesRecv.SetValue(0);
			ctsTraffic::ctsConfig::g_configSettings->TcpStatusDetails.m_startTime.SetValue(0);
			ctsTraffic::ctsConfig::g_configSettings->TcpStatusDetails.m_endTime.SetValue(0);

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(0);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(0);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(0);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(0);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_startTime.SetValue(0);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_endTime.SetValue(0);
		}

		TEST_METHOD(ctsTcpStatusInformationCsvAllZeroTest)
		{
			ctsTraffic::ctsTcpStatusInformation tcpStatusInfo;

			const auto* header = tcpStatusInfo.PrintHeader(ctsTraffic::ctsConfig::StatusFormatting::Csv);
			Logger::WriteMessage((std::wstring(L"PrintHeader():") + header).c_str());
			Assert::AreEqual(
				std::wstring(L"TimeSlice,SendBps,RecvBps,In-Flight,Completed,NetError,DataError\r\n"),
				std::wstring(header));

			const auto* legend = tcpStatusInfo.PrintLegend(ctsTraffic::ctsConfig::StatusFormatting::Csv);
			Assert::AreEqual(nullptr, legend);

			const auto* printingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::Csv, 1000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus():") + printingStatus).c_str());
			Assert::AreEqual(
				std::wstring(L"1.000,0,0,0,0,0,0\r\n"),
				std::wstring(printingStatus));

			// status update and print again
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.Increment();
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.Increment();
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.Increment();
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.Increment();
			const auto* updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::Csv, 2000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());
			Assert::AreEqual(
				std::wstring(L"2.000,0,0,1,1,1,1\r\n"),
				std::wstring(updatedPrintingStatus));
		}

		TEST_METHOD(ctsTcpStatusInformationConsoleOutputAllZeroTest)
		{
			ctsTraffic::ctsTcpStatusInformation tcpStatusInfo;

			const auto* header = tcpStatusInfo.PrintHeader(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput);
			Logger::WriteMessage((std::wstring(L"PrintHeader():") + header).c_str());

			const auto* legend = tcpStatusInfo.PrintLegend(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput);
			Logger::WriteMessage((std::wstring(L"PrintLegend():") + legend).c_str());

			const auto* printingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 1000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus():") + printingStatus).c_str());
			Assert::AreEqual(
				std::wstring(L"     1.000            0            0          0          0         0          0\n"),
				std::wstring(printingStatus));

			// status update and print again
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.Increment();
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.Increment();
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.Increment();
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.Increment();
			const auto* updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 2000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());
			Assert::AreEqual(
				std::wstring(L"     2.000            0            0          1          1         1          1\n"),
				std::wstring(updatedPrintingStatus));
		}

		TEST_METHOD(ctsTcpStatusInformationCsvMaxValueTest)
		{
			ctsTraffic::ctsTcpStatusInformation tcpStatusInfo;

			const auto* header = tcpStatusInfo.PrintHeader(ctsTraffic::ctsConfig::StatusFormatting::Csv);
			Logger::WriteMessage((std::wstring(L"PrintHeader():") + header).c_str());
			Assert::AreEqual(
				std::wstring(L"TimeSlice,SendBps,RecvBps,In-Flight,Completed,NetError,DataError\r\n"),
				std::wstring(header));

			const auto* legend = tcpStatusInfo.PrintLegend(ctsTraffic::ctsConfig::StatusFormatting::Csv);
			Assert::AreEqual(nullptr, legend);

			const auto* printingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::Csv, 1000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus():") + printingStatus).c_str());
			Assert::AreEqual(
				std::wstring(L"1.000,0,0,0,0,0,0\r\n"),
				std::wstring(printingStatus));

			// status update and print again
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(INT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(INT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(INT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(INT64_MAX);
			const auto* updatedPrintingStatus1 = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::Csv, 2000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus1).c_str());
			Assert::AreEqual(
				std::wstring(L"2.000,0,0,9223372036854775807,9223372036854775807,9223372036854775807,9223372036854775807\r\n"),
				std::wstring(updatedPrintingStatus1));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(UINT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(UINT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(UINT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(UINT64_MAX);
			const auto* updatedPrintingStatus2 = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::Csv, 3000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus2).c_str());
			Assert::AreEqual(
				std::wstring(L"3.000,0,0,18446744073709551615,18446744073709551615,18446744073709551615,18446744073709551615\r\n"),
				std::wstring(updatedPrintingStatus2));
		}

		TEST_METHOD(ctsTcpStatusInformationConsoleOutputMaxValueTest)
		{
			ctsTraffic::ctsTcpStatusInformation tcpStatusInfo;

			const auto* header = tcpStatusInfo.PrintHeader(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput);
			Logger::WriteMessage((std::wstring(L"PrintHeader():") + header).c_str());

			const auto* legend = tcpStatusInfo.PrintLegend(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput);
			Logger::WriteMessage((std::wstring(L"PrintLegend():") + legend).c_str());

			const auto* printingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 1000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus():") + printingStatus).c_str());
			Assert::AreEqual(
				std::wstring(L"     1.000            0            0          0          0         0          0\n"),
				std::wstring(printingStatus));

			// status update and print again
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(INT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(INT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(INT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(INT64_MAX);
			const auto* updatedPrintingStatus1 = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 2000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus1).c_str());
			Assert::AreEqual(
				std::wstring(L"     2.000            0            0      9+++T      9+++T     9+++T      9+++T\n"),
				std::wstring(updatedPrintingStatus1));

			// if we go greater than INT64_MAX, we will print -1 to the console. that's fine.
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(UINT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(UINT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(UINT64_MAX);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(UINT64_MAX);
			const auto* updatedPrintingStatus2 = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 3000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus2).c_str());
			Assert::AreEqual(
				std::wstring(L"     3.000            0            0         -1         -1        -1         -1\n"),
				std::wstring(updatedPrintingStatus2));
		}

		TEST_METHOD(ctsTcpStatusInformationConsoleOutputIterativeValuesTest)
		{
			ctsTraffic::ctsTcpStatusInformation tcpStatusInfo;

			const auto* header = tcpStatusInfo.PrintHeader(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput);
			Logger::WriteMessage((std::wstring(L"PrintHeader():") + header).c_str());

			const auto* legend = tcpStatusInfo.PrintLegend(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput);
			Logger::WriteMessage((std::wstring(L"PrintLegend():") + legend).c_str());

			const auto* printingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 1000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus():") + printingStatus).c_str());
			Assert::AreEqual(
				std::wstring(L"     1.000            0            0          0          0         0          0\n"),
				std::wstring(printingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(9);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(9);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(9);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(9);
			auto* updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 2000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"     2.000            0            0          9          9         9          9\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(99);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(99);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(99);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(99);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 3000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"     3.000            0            0         99         99        99         99\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 4000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"     4.000            0            0        999        999       999        999\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(9999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(9999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(9999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(9999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 5000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"     5.000            0            0       9999       9999      9999       9999\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(99999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(99999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(99999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(99999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 6000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"     6.000            0            0      99999      99999     99999      99999\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 7000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"     7.000            0            0     999999     999999    999999     999999\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(9999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(9999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(9999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(9999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 8000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"     8.000            0            0    9999999    9999999   9999999    9999999\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(9999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(9999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(9999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(9999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 9000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"     9.000            0            0    9999999    9999999   9999999    9999999\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(99999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(99999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(99999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(99999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 10000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    10.000            0            0     0.1x^9     0.1x^9    0.1x^9     0.1x^9\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(999999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 11000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    11.000            0            0     1.0x^9     1.0x^9    1.0x^9     1.0x^9\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(9999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(9999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(9999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(9999999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 12000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    12.000            0            0    10.0x^9    10.0x^9   10.0x^9    10.0x^9\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(99999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(99999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(99999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(99999999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 13000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    13.000            0            0    0.1x^12    0.1x^12   0.1x^12    0.1x^12\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(999999999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 14000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    14.000            0            0    1.0x^12    1.0x^12   1.0x^12    1.0x^12\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(9999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(9999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(9999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(9999999999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 15000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    15.000            0            0      9+++T      9+++T     9+++T      9+++T\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(99999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(99999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(99999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(99999999999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 16000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    16.000            0            0      9+++T      9+++T     9+++T      9+++T\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(999999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(999999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(999999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(999999999999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 17000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    17.000            0            0      9+++T      9+++T     9+++T      9+++T\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(9999999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(9999999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(9999999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(9999999999999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 18000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    18.000            0            0      9+++T      9+++T     9+++T      9+++T\n"),
				std::wstring(updatedPrintingStatus));

			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_activeConnectionCount.SetValue(99999999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_connectionErrorCount.SetValue(99999999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_successfulCompletionCount.SetValue(99999999999999999);
			ctsTraffic::ctsConfig::g_configSettings->ConnectionStatusDetails.m_protocolErrorCount.SetValue(99999999999999999);
			updatedPrintingStatus = tcpStatusInfo.PrintStatus(ctsTraffic::ctsConfig::StatusFormatting::ConsoleOutput, 19000, false);
			Logger::WriteMessage((std::wstring(L"PrintStatus(all zero test update):") + updatedPrintingStatus).c_str());

			Assert::AreEqual(
				std::wstring(L"    19.000            0            0      9+++T      9+++T     9+++T      9+++T\n"),
				std::wstring(updatedPrintingStatus));
		}
	};
}
