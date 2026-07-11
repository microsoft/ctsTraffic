/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <sdkddkver.h>
#include "CppUnitTest.h"

#include <atomic>
#include <string>

#include <windows.h>
#include "ctThreadIocp_shard.hpp"
#include "ctThreadIocp.hpp"
#include <vector>
#include <memory>
#include <functional>

// wil headers always included last
#include <wil/stl.h>
#include <wil/network.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;

namespace ctsUnitTest
{
TEST_CLASS(ctsThreadIocpBaseImplementationsTest)
{
public:
    TEST_CLASS_INITIALIZE(Setup)
    {
        WSADATA wsa;
        const int startup = WSAStartup(WINSOCK_VERSION, &wsa);
        Assert::AreEqual(0, startup);
    }

    TEST_CLASS_CLEANUP(Cleanup)
    {
        WSACleanup();
    }

    TEST_METHOD(UdpReceiveInvokesCallback)
    {
        using Factory = std::function<std::unique_ptr<ctl::ctThreadIocp_base>(SOCKET)>;

        std::vector<Factory> factories;
        factories.push_back([](SOCKET s) { return std::make_unique<ctl::ctThreadIocp_shard>(s, 1, std::vector<ctl::GroupAffinity>{}, 1); });
        factories.push_back([](SOCKET s) { return std::make_unique<ctl::ctThreadIocp>(s); });

        for (const auto& makeTp : factories)
        {
            // create a UDP socket and bind to loopback ephemeral port
            const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            Assert::AreNotEqual(INVALID_SOCKET, s);

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = 0; // ephemeral
            const int bindResult = ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            Assert::AreEqual(0, bindResult);

            // get assigned port
            sockaddr_in local{};
            int locallen = sizeof local;
            const int getsock = getsockname(s, reinterpret_cast<sockaddr*>(&local), &locallen);
            Assert::AreEqual(0, getsock);

            // create the iocp implementation with a single thread where applicable
            auto tp = makeTp(s);

            // buffer for receive
            char buf[128] = {};
            WSABUF wbuf{static_cast<ULONG>(sizeof buf - 1), buf};
            sockaddr_in from{};
            int fromlen = sizeof from;
            DWORD flags = 0;

            // synchronization
            const HANDLE done = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            Assert::IsNotNull(done);

            // callback will capture the event and the buffer content
            const auto callback = [done, &buf](OVERLAPPED* ov) {
                // mark parameter used to avoid unreferenced-parameter warning
                (void)ov;
                // On success, signal the event
                SetEvent(done);
            };

            // allocate overlapped-backed request
            OVERLAPPED* ov = tp->new_request(callback);
            Assert::IsNotNull(ov);

            // post a WSARecvFrom using the OVERLAPPED we provided
            DWORD recvBytes = 0;
            const int wsaRes = WSARecvFrom(s, &wbuf, 1, &recvBytes, &flags, reinterpret_cast<sockaddr*>(&from), &fromlen, ov, nullptr);

            // WSARecvFrom may either complete synchronously (returns 0) or return
            // SOCKET_ERROR with WSAGetLastError() == WSA_IO_PENDING to indicate
            // the operation was queued for asynchronous completion. Handle both.
            if (wsaRes == SOCKET_ERROR)
            {
                const int err = WSAGetLastError();
                Assert::IsTrue(err == WSA_IO_PENDING);
            }
            else
            {
                // Synchronous success (wsaRes == 0): signal the done event now
                // because no completion will be posted to the IOCP for this
                // overlapped operation.
                Assert::AreEqual(0, wsaRes);
                SetEvent(done);
            }

            // send a small UDP packet from a temporary socket to trigger completion
            const SOCKET s2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            Assert::AreNotEqual(INVALID_SOCKET, s2);

            sockaddr_in dest = local;
            const int sent = sendto(s2, "x", 1, 0, reinterpret_cast<sockaddr*>(&dest), static_cast<int>(sizeof dest));
            Assert::AreEqual(1, sent);

            // wait for the callback to run
            const DWORD wait = WaitForSingleObject(done, 5000);
            Assert::AreEqual(static_cast<DWORD>(WAIT_OBJECT_0), wait);

            CloseHandle(done);
            closesocket(s2);
            closesocket(s);
        }
    }

    TEST_METHOD(CancelRequestDeletesRequest)
    {
        using Factory = std::function<std::unique_ptr<ctl::ctThreadIocp_base>(SOCKET)>;

        std::vector<Factory> factories;
        factories.push_back([](SOCKET s) { return std::make_unique<ctl::ctThreadIocp_shard>(s, 1, std::vector<ctl::GroupAffinity>{}, 1); });
        factories.push_back([](SOCKET s) { return std::make_unique<ctl::ctThreadIocp>(s); });

        for (const auto& makeTp : factories)
        {
            // create a UDP socket and attach to tp
            const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            Assert::AreNotEqual(INVALID_SOCKET, s);

            auto tp = makeTp(s);

            // create an overlapped-backed request with a callback that would fail the test if called
            const auto badCallback = [](OVERLAPPED* ov) { (void)ov; Assert::Fail(L"Callback should not be invoked"); };
            OVERLAPPED* ov = tp->new_request(badCallback);
            Assert::IsNotNull(ov);

            // simulate immediate failure of the API that consumes the overlapped (e.g., send failed)
            // caller must call cancel_request to free the request
            tp->cancel_request(ov);

            closesocket(s);
        }
    }
};
}
