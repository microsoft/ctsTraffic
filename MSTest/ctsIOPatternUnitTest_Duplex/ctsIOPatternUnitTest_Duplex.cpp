/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <sdkddkver.h>
#include "CppUnitTest.h"
// cpp headers
#include <memory>
// OS headers
#include <Windows.h>
#include <WinSock2.h>
// ctl headers
#include <ctTimer.hpp>
#include <ctString.hpp>
// project headers
#include "ctsIOTask.hpp"
#include "ctsConfig.h"
#include "ctsIOPattern.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{
// Test writer must define specialization of ToString<const Q& q> types used in Assert
template <>
std::wstring ToString<ctsTraffic::ctsTaskAction>(const ctsTraffic::ctsTaskAction& action)
{
    return ctsTraffic::ctsTask::PrintTaskAction(action);
}

template <>
std::wstring ToString<ctsTraffic::ctsIoStatus>(const ctsTraffic::ctsIoStatus& status)
{
    switch (status)
    {
        case ctsTraffic::ctsIoStatus::ContinueIo:
            return L"ContinueIo";
        case ctsTraffic::ctsIoStatus::CompletedIo:
            return L"CompletedIo";
        case ctsTraffic::ctsIoStatus::FailedIo:
            return L"FailedIo";
    }
    return L"Unknown_ctsIOStatus";
}
}


///
/// statics to return in the Fakes
///
int64_t g_tcpBytesPerSecond = 0LL;
uint32_t g_MaxBufferSize = 0UL;
uint32_t g_BufferSize = 0UL;
uint64_t g_transferSize = 0ULL;
bool g_IsListening = false;
ctsTraffic::ctsConfig::MediaStreamSettings g_MediaStreamSettings;
constexpr uint32_t g_TestRecvBufferLength = 1024;
constexpr uint32_t g_TestCompletionMessageLength = 4;
constexpr uint32_t g_TestErrorCode = 1;

///
/// Fakes
///
namespace ctsTraffic::ctsConfig
{
ctsConfigSettings* g_configSettings;

void PrintConnectionResults(uint32_t) noexcept
{
}

void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsTcpStatistics&) noexcept
{
}

void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, uint32_t, const ctsUdpStatistics&) noexcept
{
}

void PrintDebug(_In_ _Printf_format_string_ PCWSTR, ...) noexcept
{
}

void PrintException(const std::exception&) noexcept
{
}

void PrintJitterUpdate(const JitterFrameEntry&, const JitterFrameEntry&) noexcept
{
}

void PrintErrorInfo(_In_ _Printf_format_string_ PCWSTR, ...) noexcept
{
}

void PrintTcpDetails(const ctl::ctSockaddr&, const ctl::ctSockaddr&, SOCKET, const ctsTcpStatistics&) noexcept
{
}

bool IsListening() noexcept
{
    return g_IsListening;
}


const MediaStreamSettings& GetMediaStream() noexcept
{
    return g_MediaStreamSettings;
}

int64_t GetTcpBytesPerSecond() noexcept
{
    return g_tcpBytesPerSecond;
}

uint32_t GetMaxBufferSize() noexcept
{
    return g_MaxBufferSize;
}

uint32_t GetMinBufferSize() noexcept
{
    return g_BufferSize;
}

uint32_t GetBufferSize() noexcept
{
    return g_BufferSize;
}

uint64_t GetTransferSize() noexcept
{
    return g_transferSize;
}

float GetStatusTimeStamp() noexcept
{
    return static_cast<float>((ctl::ctTimer::snap_qpc_as_msec() - g_configSettings->StartTimeMilliseconds) / 1000.0);
}

bool ShutdownCalled() noexcept
{
    return false;
}

uint32_t ConsoleVerbosity() noexcept
{
    return 0;
}

TcpShutdownType GetShutdownType() noexcept
{
    return ctsConfig::g_configSettings->TcpShutdown;
}
}

///
/// End of Fakes
///

using namespace ctsTraffic;

namespace ctsUnitTest
{
///
/// Unit-tests specifically covering ctsIoPatternDuplex.
///
/// The Duplex pattern is unique among the TCP patterns: a single connection both sends and
/// receives concurrently. The transfer size is split in half - N/2 sent + N/2 received - and
/// both a send and a recv can be pended at the same time (never more than one recv pended).
///
/// The base ctsIOPattern / ctsIoPatternState state-machine only advances out of the data-transfer
/// (MoreIo) phase into the shutdown sequence once *both* concurrently-pended IOs have completed
/// (in-flight bytes == 0 && all bytes confirmed). Which of the two IOs completes *last* therefore
/// drives the transition. These tests exercise both completion orderings plus the final RST/FIN
/// handling that has been reported to occasionally confuse the end-of-connection logic.
///
TEST_CLASS(ctsIOPatternUnitTest_Duplex)
{
private:
    enum TestRole
    {
        Client,
        Server
    };

    enum TestShutdownMethod
    {
        Graceful,
        Hard
    };

    // 20 total => 10 bytes sent + 10 bytes received when split in half by the Duplex pattern
    static constexpr uint32_t DuplexTransferSize = 20UL;
    static constexpr uint32_t HalfTransferSize = DuplexTransferSize / 2;

    void SetTestDuplexDefaults(TestRole role, TestShutdownMethod shutdown = Graceful) const
    {
        if (Server == role && Hard == shutdown)
        {
            Assert::Fail(L"Servers only support the default Graceful shutdown");
        }

        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        ctsConfig::g_configSettings->ConnectionLimit = 8;
        ctsConfig::g_configSettings->TcpShutdown = (Graceful == shutdown) ? ctsConfig::TcpShutdownType::GracefulShutdown : ctsConfig::TcpShutdownType::HardShutdown;

        g_tcpBytesPerSecond = 0LL;
        g_MaxBufferSize = g_TestRecvBufferLength;
        g_BufferSize = g_TestRecvBufferLength;
        g_transferSize = DuplexTransferSize;
        g_IsListening = (Server == role);
    }

    // drives the connection-id handshake (recv for a client, send for a server)
    static void CompleteConnectionId(const std::shared_ptr<ctsIoPattern>& pattern, TestRole role)
    {
        const ctsTask task = pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, task.m_bufferLength);
        Assert::AreEqual(Server == role ? ctsTaskAction::Send : ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, pattern->CompleteIo(task, ctsStatistics::ConnectionIdLength, NO_ERROR));
    }

    // Completes a *successful* data recv. Because ShouldVerifyBuffers is enabled, the received
    // buffer must contain the expected send-pattern or the pattern reports data corruption. The
    // real IO layer fills this buffer from the network; here we copy it from the shared pattern
    // buffer (mirrors the ctsIOPatternUnitTest_Client convention) before completing the recv.
    static ctsIoStatus CompleteDataRecv(const std::shared_ptr<ctsIoPattern>& pattern, const ctsTask& task, uint32_t bytes)
    {
        memcpy(
            task.m_buffer + task.m_bufferOffset,
            ctsIoPattern::AccessSharedBuffer() + task.m_expectedPatternOffset,
            bytes);
        return pattern->CompleteIo(task, bytes, NO_ERROR);
    }

    // retrieves the concurrently-pended data recv + data send; verifies a third InitiateIo yields no IO
    static void GetPendedDataTasks(const std::shared_ptr<ctsIoPattern>& pattern, ctsTask& recvTask, ctsTask& sendTask)
    {
        recvTask = pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, recvTask.m_ioAction, L"first Duplex data task should be a recv");
        Assert::AreEqual(HalfTransferSize, recvTask.m_bufferLength);

        sendTask = pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, sendTask.m_ioAction, L"second Duplex data task should be a send");
        Assert::AreEqual(HalfTransferSize, sendTask.m_bufferLength);

        // With one recv + one send already pended for the whole transfer, the pattern must not
        // hand out any further work: it must return an empty/None task (NOT hang by looping).
        const ctsTask noTask = pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::None, noTask.m_ioAction, L"no additional IO should be requested while the transfer is fully pended");
    }

    // drives the shutdown sequence following a successful data transfer, asserting completion.
    static void CompleteSuccessfulShutdown(const std::shared_ptr<ctsIoPattern>& pattern, TestRole role, TestShutdownMethod shutdown)
    {
        if (Server == role)
        {
            // server sends its completion 'DONE' then waits for the client's FIN
            ctsTask task = pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::Send, task.m_ioAction);
            Assert::AreEqual(g_TestCompletionMessageLength, task.m_bufferLength);
            Assert::AreEqual(ctsIoStatus::ContinueIo, pattern->CompleteIo(task, g_TestCompletionMessageLength, NO_ERROR));

            task = pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction, L"server must request the FIN recv");
            Assert::AreEqual(ctsIoStatus::CompletedIo, pattern->CompleteIo(task, 0, NO_ERROR));
            return;
        }

        // client recvs the server's completion 'DONE'
        ctsTask task = pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(g_TestCompletionMessageLength, task.m_bufferLength);
        Assert::AreEqual(ctsIoStatus::ContinueIo, pattern->CompleteIo(task, g_TestCompletionMessageLength, NO_ERROR));

        if (Hard == shutdown)
        {
            task = pattern->InitiateIo();
            Assert::AreEqual(ctsTaskAction::HardShutdown, task.m_ioAction);
            Assert::AreEqual(ctsIoStatus::CompletedIo, pattern->CompleteIo(task, 0, NO_ERROR));
            return;
        }

        // graceful: shutdown(SD_SEND) then recv the server's FIN
        task = pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, pattern->CompleteIo(task, 0, NO_ERROR));

        task = pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction, L"client must request the FIN recv");
        Assert::AreEqual(ctsIoStatus::CompletedIo, pattern->CompleteIo(task, 0, NO_ERROR));
    }

public:
    TEST_CLASS_INITIALIZE(Setup)
    {
        ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;

        ctsConfig::g_configSettings->IoPattern = ctsConfig::IoPatternType::Duplex;
        ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
        ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        ctsConfig::g_configSettings->UseSharedBuffer = false;
        ctsConfig::g_configSettings->ShouldVerifyBuffers = true;
        ctsConfig::g_configSettings->PrePostRecvs = 1;
        ctsConfig::g_configSettings->PrePostSends = 1;
        ctsConfig::g_configSettings->ConnectionLimit = 8;
    }

    TEST_CLASS_CLEANUP(Cleanup)
    {
        delete ctsConfig::g_configSettings;
    }

    //
    // ---- Success paths ----
    //

    // Client, graceful shutdown, recv completes before send (transition driven by the send).
    TEST_METHOD(Duplex_Client_Graceful_RecvThenSend)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        // recv completes first: the transfer total is met, but the send is still in-flight,
        // so the pattern must remain in the data phase (ContinueIo) and not yet advance.
        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));
        // the send completing is the last in-flight IO => drives the transition into shutdown
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));

        CompleteSuccessfulShutdown(test_pattern, Client, Graceful);
        Assert::AreEqual(0u, test_pattern->GetLastPatternError());
    }

    // Client, graceful shutdown, send completes before recv (transition driven by the recv).
    // This is the ordering most likely to "confuse" end-of-connection logic.
    TEST_METHOD(Duplex_Client_Graceful_SendThenRecv)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));
        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));

        CompleteSuccessfulShutdown(test_pattern, Client, Graceful);
        Assert::AreEqual(0u, test_pattern->GetLastPatternError());
    }

    // Client, hard shutdown (RST) success.
    TEST_METHOD(Duplex_Client_HardShutdown)
    {
        this->SetTestDuplexDefaults(Client, Hard);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));

        CompleteSuccessfulShutdown(test_pattern, Client, Hard);
        Assert::AreEqual(0u, test_pattern->GetLastPatternError());
    }

    // Server, graceful shutdown success (send completes before recv).
    TEST_METHOD(Duplex_Server_Graceful_SendThenRecv)
    {
        this->SetTestDuplexDefaults(Server, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Server);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));
        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));

        CompleteSuccessfulShutdown(test_pattern, Server, Graceful);
        Assert::AreEqual(0u, test_pattern->GetLastPatternError());
    }

    // Server, graceful shutdown success (recv completes before send).
    TEST_METHOD(Duplex_Server_Graceful_RecvThenSend)
    {
        this->SetTestDuplexDefaults(Server, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Server);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));

        CompleteSuccessfulShutdown(test_pattern, Server, Graceful);
        Assert::AreEqual(0u, test_pattern->GetLastPatternError());
    }

    // A recv completing with fewer bytes than posted must re-post another recv for the remainder
    // (tests the pattern's over-subscription re-adjustment while a send is concurrently pended).
    TEST_METHOD(Duplex_Client_PartialRecv_RepostsRemainder)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        // recv (10) and send (10) both pended
        const ctsTask recv_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, recv_task.m_ioAction);
        Assert::AreEqual(HalfTransferSize, recv_task.m_bufferLength);

        const ctsTask send_task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, send_task.m_ioAction);
        Assert::AreEqual(HalfTransferSize, send_task.m_bufferLength);

        // recv completes with only 4 of the 10 bytes
        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, 4));

        // the pattern must now request another recv for the remaining 6 bytes
        const ctsTask recv_remainder = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, recv_remainder.m_ioAction, L"a partial recv must be followed by a recv for the remainder");
        Assert::AreEqual(HalfTransferSize - 4, recv_remainder.m_bufferLength);

        // complete the send and the remaining recv
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));
        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_remainder, HalfTransferSize - 4));

        CompleteSuccessfulShutdown(test_pattern, Client, Graceful);
        Assert::AreEqual(0u, test_pattern->GetLastPatternError());
    }

    // Server tolerates an abortive close (RST) while waiting for the client's FIN: the client may
    // RST instead of a graceful FIN after receiving the server's status. This must still complete.
    TEST_METHOD(Duplex_Server_TolerateRstWhileAwaitingFin)
    {
        this->SetTestDuplexDefaults(Server, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Server);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));

        // server sends completion 'DONE'
        ctsTask task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Send, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(task, g_TestCompletionMessageLength, NO_ERROR));

        // server requests the FIN, but the client RSTs (WSAECONNRESET) - this is tolerated and completes
        task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::CompletedIo, test_pattern->CompleteIo(task, 0, WSAECONNRESET));
        Assert::AreEqual(0u, test_pattern->GetLastPatternError());
    }

    //
    // ---- Error paths ----
    //

    // Client fails to receive the connection id.
    TEST_METHOD(Duplex_Client_FailConnectionId)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        const ctsTask task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(task, 0, g_TestErrorCode));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    // Server fails to send the connection id.
    TEST_METHOD(Duplex_Server_FailConnectionId)
    {
        this->SetTestDuplexDefaults(Server, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        const ctsTask task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsStatistics::ConnectionIdLength, task.m_bufferLength);
        Assert::AreEqual(ctsTaskAction::Send, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(task, 0, g_TestErrorCode));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    // A data send failing mid-transfer fails the whole pattern.
    TEST_METHOD(Duplex_Client_FailDataSend)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        // the send fails - the pattern must fail even though the recv is still outstanding
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(send_task, 0, g_TestErrorCode));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    // A data recv failing mid-transfer fails the whole pattern.
    TEST_METHOD(Duplex_Client_FailDataRecv)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(recv_task, 0, g_TestErrorCode));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    // Peer closes early: a data recv completes gracefully with 0 bytes before the transfer is
    // complete. That is too-few-bytes and must fail (not be mistaken for a clean shutdown FIN).
    TEST_METHOD(Duplex_Client_PrematureFinDuringTransfer)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        // the send lands fine
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));
        // but the recv completes with 0 bytes (a FIN) before the expected data arrived => failure
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(recv_task, 0, NO_ERROR));
        Assert::AreEqual(static_cast<uint32_t>(c_statusErrorNotAllDataTransferred), test_pattern->GetLastPatternError());
    }

    // End-of-connection confusion: after all data is transferred and the server's completion is
    // received, the client posts a recv expecting the server's FIN (a 0-byte graceful close).
    // If extra data bytes arrive instead of the FIN, that is a protocol violation (too many bytes)
    // and must fail rather than be mistaken for a clean shutdown.
    TEST_METHOD(Duplex_Client_ExtraBytesWhenExpectingFin)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));
        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));

        // client recvs the server's completion 'DONE'
        ctsTask task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(task, g_TestCompletionMessageLength, NO_ERROR));

        // graceful shutdown(SD_SEND) succeeds
        task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(task, 0, NO_ERROR));

        // the FIN recv should return 0 bytes; instead the peer sends data => too many bytes
        task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(task, 1, NO_ERROR));
        Assert::AreEqual(static_cast<uint32_t>(c_statusErrorTooMuchDataTransferred), test_pattern->GetLastPatternError());
    }

    // Client fails to receive the server's completion 'DONE' message.
    TEST_METHOD(Duplex_Client_FailReceivingServerCompletion)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));

        const ctsTask task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(task, 0, g_TestErrorCode));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    // Client graceful shutdown IO itself fails.
    TEST_METHOD(Duplex_Client_FailGracefulShutdown)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));

        ctsTask task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(task, g_TestCompletionMessageLength, NO_ERROR));

        task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(task, 0, g_TestErrorCode));
        Assert::AreEqual(g_TestErrorCode, test_pattern->GetLastPatternError());
    }

    // Client's FIN recv fails with a hard error (RST) - unlike the server, the client does NOT
    // tolerate this and reports failure.
    TEST_METHOD(Duplex_Client_FailFinWithRst)
    {
        this->SetTestDuplexDefaults(Client, Graceful);
        const std::shared_ptr test_pattern(ctsIoPattern::MakeIoPattern());

        CompleteConnectionId(test_pattern, Client);

        ctsTask recv_task;
        ctsTask send_task;
        GetPendedDataTasks(test_pattern, recv_task, send_task);

        Assert::AreEqual(ctsIoStatus::ContinueIo, CompleteDataRecv(test_pattern, recv_task, HalfTransferSize));
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(send_task, HalfTransferSize, NO_ERROR));

        ctsTask task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(task, g_TestCompletionMessageLength, NO_ERROR));

        task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::GracefulShutdown, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::ContinueIo, test_pattern->CompleteIo(task, 0, NO_ERROR));

        task = test_pattern->InitiateIo();
        Assert::AreEqual(ctsTaskAction::Recv, task.m_ioAction);
        Assert::AreEqual(ctsIoStatus::FailedIo, test_pattern->CompleteIo(task, 0, WSAECONNRESET));
        Assert::AreEqual(static_cast<uint32_t>(WSAECONNRESET), test_pattern->GetLastPatternError());
    }
};
}
