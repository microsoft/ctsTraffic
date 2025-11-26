/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <CppUnitTest.h>

#include <vector>
#include <memory>
#include <Windows.h>
#include "ctsConfig.h"
#include "ctsIOPattern.h"

#include <wil/resource.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace ctsTraffic;

template<> inline std::wstring
__stdcall Microsoft::VisualStudio::CppUnitTestFramework::ToString<ctsTaskAction>(const ctsTaskAction& task_action)
{
	switch (task_action)
	{
	case ctsTaskAction::None:
		return L"None";
	case ctsTaskAction::Send:
		return L"Send";
	case ctsTaskAction::Recv:
		return L"Recv";
	default:
		return L"Unknown";
	}
}

template<> inline std::wstring
__stdcall Microsoft::VisualStudio::CppUnitTestFramework::ToString<ctsIoStatus>(const ctsIoStatus& status)
{
	switch (status)
	{
	case ctsIoStatus::ContinueIo:
		return L"ContinueIo";
	case ctsIoStatus::CompletedIo:
		return L"CompletedIo";
	case ctsIoStatus::FailedIo:
		return L"FailedIo";
	default:
		return L"Unknown";
	}
}

void ctsConfig::PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&,unsigned int, const ctsTcpStatistics&) noexcept
{
}
void ctsConfig::PrintTcpDetails(const ctl::ctSockaddr&, const ctl::ctSockaddr&,unsigned int, const ctsTcpStatistics&) noexcept
{
}
unsigned int ctsConfig::GetMaxBufferSize() noexcept 
{
    return {};
}
bool ctsConfig::IsListening() noexcept
{
	return {};
}
bool ctsConfig::ShutdownCalled() noexcept
{
    return {};
}

unsigned int ctsConfig::ConsoleVerbosity() noexcept
{
    return {};
}

ctsConfig::ctsConfigSettings g_settings{};
ctsConfig::ctsConfigSettings * ctsConfig::g_configSettings {&g_settings};

class ctsIoPatternDuplexTestHelper : public ctsIoPatternDuplex
{
public:
    ctsIoPatternDuplexTestHelper() = default;
    ~ctsIoPatternDuplexTestHelper() noexcept override = default;
	using ctsIoPatternDuplex::m_remainingSendBytes;
	using ctsIoPatternDuplex::m_remainingRecvBytes;
	using ctsIoPatternDuplex::m_recvNeeded;
	using ctsIoPatternDuplex::m_sendBytesInFlight;
	using ctsIoPatternDuplex::GetTotalTransfer;
	using ctsIoPatternDuplex::SetTotalTransfer;
	using ctsIoPatternDuplex::GetIdealSendBacklog;
	using ctsIoPatternDuplex::SetIdealSendBacklog;
	using ctsIoPatternDuplex::InitiateIo;
	using ctsIoPatternDuplex::CompleteIo;
	using ctsIoPatternDuplex::GetCurrentStatus;
	using ctsIoPatternDuplex::GetLastPatternError;
};

struct DuplexTestState
{
	std::shared_ptr<ctsIoPatternDuplexTestHelper> m_pattern;
	uint64_t m_transferSize = 0;
	uint32_t m_bufferSize = 0;

	void InitializePattern()
	{
		m_pattern = std::make_shared<ctsIoPatternDuplexTestHelper>();
		m_transferSize = m_pattern->GetTotalTransfer();
		m_bufferSize = ctsConfig::GetMaxBufferSize();
	}

	void VerifyNoMoreIo()
	{
		const auto nextTask = m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::None, nextTask.m_ioAction);
		Assert::IsTrue(m_pattern->GetCurrentStatus() != ctsIoStatus::ContinueIo);
	}

	void CompleteRecvTasks(uint64_t bytesToReceive)
	{
		while (bytesToReceive > 0)
		{
			const auto task = m_pattern->InitiateIo();
			if (task.m_ioAction != ctsTaskAction::Recv)
			{
				break;
			}

			const uint32_t transferredBytes = static_cast<uint32_t>(
				(std::min)(static_cast<uint64_t>(task.m_bufferLength), bytesToReceive));

			const auto status = m_pattern->CompleteIo(task, transferredBytes, NO_ERROR);
			Assert::IsTrue(status != ctsIoStatus::FailedIo);

			bytesToReceive -= transferredBytes;
		}
	}

	void CompleteSendTasks(uint64_t bytesToSend)
	{
		while (bytesToSend > 0)
		{
			const auto task = m_pattern->InitiateIo();
			if (task.m_ioAction != ctsTaskAction::Send)
			{
				break;
			}

			const uint32_t transferredBytes = static_cast<uint32_t>(
				(std::min)(static_cast<uint64_t>(task.m_bufferLength), bytesToSend));

			const auto status = m_pattern->CompleteIo(task, transferredBytes, NO_ERROR);
			Assert::IsTrue(status != ctsIoStatus::FailedIo);

			bytesToSend -= transferredBytes;
		}
	}

    ctsTask InitiateIo()
	{
	    return {};
	}
    ctsIoStatus CompleteIo(const ctsTask&,unsigned int,unsigned int)
	{
	    return {};
	}
    ctsTask GetNextTaskFromPattern()
	{
	    return {};
	}
    ctsIoPatternError CompleteTaskBackToPattern(const ctsTask&,unsigned int)
	{
	    return {};
	}
	void PrintTcpInfo(const ctl::ctSockaddr&, const ctl::ctSockaddr&,unsigned int)
	{
	}
	void PrintStatistics(const ctl::ctSockaddr&, const ctl::ctSockaddr&)
	{
	}
};

TEST_CLASS(ctsIoPatternDuplexUnitTest)
{
public:
	TEST_METHOD(InitialState_AllowsSendAndReceive)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		ctsConfig::g_configSettings->PrePostRecvs = 1;
		ctsConfig::g_configSettings->PrePostSends = 1;
		testState.InitializePattern();

		const auto task = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);

		const auto sendTask = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::Send, sendTask.m_ioAction);
	}

	TEST_METHOD(SendAndReceive_ProgressConcurrently)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		ctsConfig::g_configSettings->PrePostRecvs = 1;
		ctsConfig::g_configSettings->PrePostSends = 1;
		testState.InitializePattern();

		const uint64_t halfTransfer = testState.m_transferSize / 2;
		testState.CompleteRecvTasks(halfTransfer);

		const auto sendTask = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::Send, sendTask.m_ioAction);

		testState.CompleteSendTasks(halfTransfer);

		const auto recvTask = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::Recv, recvTask.m_ioAction);
	}

	TEST_METHOD(CompleteAllTransfers_Success)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		ctsConfig::g_configSettings->PrePostRecvs = 1;
		ctsConfig::g_configSettings->PrePostSends = 1;
		testState.InitializePattern();

		testState.CompleteRecvTasks(testState.m_transferSize);
		testState.CompleteSendTasks(testState.m_transferSize);

		Assert::AreEqual(ctsIoStatus::CompletedIo, testState.m_pattern->GetCurrentStatus());
		Assert::AreEqual(static_cast<uint32_t>(NO_ERROR), testState.m_pattern->GetLastPatternError());
		testState.VerifyNoMoreIo();
	}

	TEST_METHOD(SendError_FailsPattern)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		testState.InitializePattern();

		auto task = testState.m_pattern->InitiateIo();
		while (task.m_ioAction != ctsTaskAction::Send && task.m_ioAction != ctsTaskAction::None)
		{
			task = testState.m_pattern->InitiateIo();
		}
		Assert::AreEqual(ctsTaskAction::Send, task.m_ioAction);

		const auto status = testState.m_pattern->CompleteIo(task, 0, WSAECONNRESET);
		Assert::AreEqual(ctsIoStatus::FailedIo, status);
		Assert::AreEqual(static_cast<uint32_t>(WSAECONNRESET), testState.m_pattern->GetLastPatternError());
	}

	TEST_METHOD(RecvError_FailsPattern)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		testState.InitializePattern();

		const auto task = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);

		const auto status = testState.m_pattern->CompleteIo(task, 0, WSAECONNABORTED);
		Assert::AreEqual(ctsIoStatus::FailedIo, status);
		Assert::AreEqual(static_cast<uint32_t>(WSAECONNABORTED), testState.m_pattern->GetLastPatternError());
	}

	TEST_METHOD(TooMuchDataReceived_FailsPattern)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		ctsConfig::g_configSettings->PrePostRecvs = 1;
		testState.InitializePattern();

		testState.CompleteRecvTasks(testState.m_transferSize);

		const auto extraTask = testState.m_pattern->InitiateIo();
		if (extraTask.m_ioAction == ctsTaskAction::Recv)
		{
			const auto status = testState.m_pattern->CompleteIo(extraTask, 100, NO_ERROR);
			Assert::AreEqual(ctsIoStatus::FailedIo, status);
			Assert::AreEqual(static_cast<uint32_t>(c_statusErrorTooMuchDataTransferred), testState.m_pattern->GetLastPatternError());
		}
	}

	TEST_METHOD(IdealSendBacklog_Respected)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		ctsConfig::g_configSettings->PrePostSends = 3;
		testState.InitializePattern();

		std::vector<ctsTask> sendTasks;
		for (uint32_t i = 0; i < ctsConfig::g_configSettings->PrePostSends; ++i)
		{
			auto task = testState.m_pattern->InitiateIo();
			if (task.m_ioAction == ctsTaskAction::Send)
			{
				sendTasks.push_back(task);
			}
		}
		Assert::IsTrue(!sendTasks.empty());

		if (!sendTasks.empty())
		{
			const auto status = testState.m_pattern->CompleteIo(sendTasks[0], sendTasks[0].m_bufferLength, NO_ERROR);
			Assert::IsTrue(status != ctsIoStatus::FailedIo);
		}
	}

	TEST_METHOD(PartialSendCompletion_ContinueIo)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		testState.InitializePattern();

		auto task = testState.m_pattern->InitiateIo();
		while (task.m_ioAction != ctsTaskAction::Send && task.m_ioAction != ctsTaskAction::None)
		{
			task = testState.m_pattern->InitiateIo();
		}
		Assert::AreEqual(ctsTaskAction::Send, task.m_ioAction);

		const uint32_t partialBytes = task.m_bufferLength / 2;
		const auto status = testState.m_pattern->CompleteIo(task, partialBytes, NO_ERROR);
		Assert::AreEqual(ctsIoStatus::ContinueIo, status);
	}

	TEST_METHOD(PartialRecvCompletion_ContinueIo)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		testState.InitializePattern();

		const auto task = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);

		const uint32_t partialBytes = task.m_bufferLength / 2;
		const auto status = testState.m_pattern->CompleteIo(task, partialBytes, NO_ERROR);
		Assert::AreEqual(ctsIoStatus::ContinueIo, status);
	}

	TEST_METHOD(ZeroByteRecv_GracefulClose)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		testState.InitializePattern();

		const auto task = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);

		const auto status = testState.m_pattern->CompleteIo(task, 0, NO_ERROR);
		Assert::AreEqual(ctsIoStatus::FailedIo, status);
		Assert::AreEqual(static_cast<uint32_t>(c_statusErrorNotAllDataTransferred), testState.m_pattern->GetLastPatternError());
	}

	TEST_METHOD(AlreadyCompleted_NoNewIo)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		testState.InitializePattern();

		testState.CompleteRecvTasks(testState.m_transferSize);
		testState.CompleteSendTasks(testState.m_transferSize);

		Assert::AreEqual(ctsIoStatus::CompletedIo, testState.m_pattern->GetCurrentStatus());

		const auto task = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::None, task.m_ioAction);
	}

	TEST_METHOD(SmallTransferSize_OneByte)
	{
		const auto savedProtocol = ctsConfig::g_configSettings->Protocol;
		const auto savedIoPattern = ctsConfig::g_configSettings->IoPattern;
		const auto savedPrePostRecvs = ctsConfig::g_configSettings->PrePostRecvs;
		const auto savedPrePostSends = ctsConfig::g_configSettings->PrePostSends;

		auto restoreSettings = wil::scope_exit([&] {
			ctsConfig::g_configSettings->Protocol = savedProtocol;
			ctsConfig::g_configSettings->IoPattern = savedIoPattern;
			ctsConfig::g_configSettings->PrePostRecvs = savedPrePostRecvs;
			ctsConfig::g_configSettings->PrePostSends = savedPrePostSends;
			});

		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		ctsConfig::g_configSettings->PrePostRecvs = 1;
		ctsConfig::g_configSettings->PrePostSends = 1;

		testState.InitializePattern();

		Assert::AreEqual(static_cast<uint64_t>(1), testState.m_pattern->GetTotalTransfer());

		const auto recvTask = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::Recv, recvTask.m_ioAction);
		auto status = testState.m_pattern->CompleteIo(recvTask, 1, NO_ERROR);
		Assert::IsTrue(status != ctsIoStatus::FailedIo);

		const auto sendTask = testState.m_pattern->InitiateIo();
		Assert::AreEqual(ctsTaskAction::Send, sendTask.m_ioAction);
		status = testState.m_pattern->CompleteIo(sendTask, 1, NO_ERROR);
		Assert::IsTrue(status != ctsIoStatus::FailedIo);

		Assert::AreEqual(ctsIoStatus::CompletedIo, testState.m_pattern->GetCurrentStatus());
	}

	TEST_METHOD(MultipleConcurrentOperations)
	{
		DuplexTestState testState;
		ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
		ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
		ctsConfig::g_configSettings->PrePostRecvs = 5;
		ctsConfig::g_configSettings->PrePostSends = 5;
		testState.InitializePattern();

		std::vector<ctsTask> pendingTasks;
		for (int i = 0; i < 10; ++i)
		{
			auto task = testState.m_pattern->InitiateIo();
			if (task.m_ioAction != ctsTaskAction::None)
			{
				pendingTasks.push_back(task);
			}
		}

		bool hasSend = false, hasRecv = false;
		for (const auto& t : pendingTasks)
		{
			if (t.m_ioAction == ctsTaskAction::Send) hasSend = true;
			if (t.m_ioAction == ctsTaskAction::Recv) hasRecv = true;
		}
		Assert::IsTrue(hasSend);
		Assert::IsTrue(hasRecv);

		for (const auto& task : pendingTasks)
		{
			const auto status = testState.m_pattern->CompleteIo(task, task.m_bufferLength, NO_ERROR);
			Assert::IsTrue(status != ctsIoStatus::FailedIo);
		}
	}
};
