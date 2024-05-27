/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <memory>
// os headers
#include <Windows.h>
#include <WinSock2.h>
// ctl headers
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"

namespace ctsTraffic
{
void ctsReadWriteIocp(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;

// IO Threadpool completion callback 
static void ctsReadWriteIocpIoCompletionCallback(
    _In_ OVERLAPPED* pOverlapped,
    const std::weak_ptr<ctsSocket>& weakSocket,
    const ctsTask& task) noexcept
{
    const auto sharedSocket(weakSocket.lock());
    if (!sharedSocket)
    {
        return;
    }

    uint32_t gle = NO_ERROR;

    // hold a reference on the socket
    const auto lockedSocket = sharedSocket->AcquireSocketLock();
    const auto lockedPattern = lockedSocket.GetPattern();
    if (!lockedPattern)
    {
        gle = WSAECONNABORTED;
    }

    DWORD transferred = 0;
    const auto socket = lockedSocket.GetSocket();
    if (INVALID_SOCKET == socket)
    {
        gle = WSAECONNABORTED;
    }
    else
    {
        DWORD flags;
        if (!WSAGetOverlappedResult(socket, pOverlapped, &transferred, FALSE, &flags))
        {
            gle = WSAGetLastError();
        }
    }

    const char* functionName = ctsTaskAction::Send == task.m_ioAction ? "WriteFile" : "ReadFile";
    if (gle != NO_ERROR)
    {
        PRINT_DEBUG_INFO(L"\t\tIO Failed: %hs (%u) [ctsReadWriteIocp]\n", functionName, gle);
    }

    if (lockedPattern)
    {
        // see if complete_io requests more IO
        DWORD readwriteStatus = NO_ERROR;
        switch (const ctsIoStatus protocolStatus = lockedPattern->CompleteIo(task, transferred, gle))
        {
            case ctsIoStatus::ContinueIo:
                // more IO is requested from the protocol
                // - invoke the new IO call while holding a ref-count to the prior IO
                ctsReadWriteIocp(weakSocket);
                break;

            case ctsIoStatus::CompletedIo:
                // protocol didn't fail this IO: no more IO is requested from the protocol
                readwriteStatus = NO_ERROR;
                break;

            case ctsIoStatus::FailedIo:
                // write out the error
                ctsConfig::PrintErrorIfFailed(functionName, gle);
                // protocol sees this as a failure - capture the error the protocol recorded
                readwriteStatus = lockedPattern->GetLastPatternError();
                break;

            default:
                FAIL_FAST_MSG("ctsReadWriteIocp: unknown ctsSocket::IOStatus - %d\n", protocolStatus);
        }

        gle = readwriteStatus;
    }

    // always decrement *after* attempting new IO - the prior IO is now formally "done"
    if (sharedSocket->DecrementIo() == 0)
    {
        // if we have no more IO pended, complete the state
        sharedSocket->CompleteState(gle);
    }
}

// The registered function with ctsConfig
void ctsReadWriteIocp(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
{
    // must get a reference to the socket and the IO pattern
    const auto sharedSocket(weakSocket.lock());
    if (!sharedSocket)
    {
        return;
    }

    // hold a reference on the socket
    const auto lockedSocket = sharedSocket->AcquireSocketLock();
    const auto lockedPattern = lockedSocket.GetPattern();
    if (!lockedPattern)
    {
        return;
    }

    // can't initialize to zero - zero indicates to complete_state()
    long ioCount = -1;
    uint32_t ioError = NO_ERROR;

    auto socket = lockedSocket.GetSocket();
    if (socket != INVALID_SOCKET)
    {
        auto ioDone = false;
        // loop until failure or initiate_io returns None
        while (!ioDone && NO_ERROR == ioError)
        {
            // each loop requests the next task
            ctsTask nextIo = lockedPattern->InitiateIo();
            if (ctsTaskAction::None == nextIo.m_ioAction)
            {
                // nothing failed, just no more IO right now
                ioDone = true;
                continue;
            }

            if (ctsTaskAction::GracefulShutdown == nextIo.m_ioAction)
            {
                if (0 != shutdown(socket, SD_SEND))
                {
                    ioError = WSAGetLastError();
                    PRINT_DEBUG_INFO(L"\t\tIO Failed: shutdown(SD_SEND) (%u) [ctsReadWriteIocp]\n", ioError);
                }
                else
                {
                    PRINT_DEBUG_INFO(L"\t\tIO successfully called shutdown(SD_SEND) [ctsReadWriteIocp]\n");
                }

                ioDone = lockedPattern->CompleteIo(nextIo, 0, ioError) != ctsIoStatus::ContinueIo;
                continue;
            }

            if (ctsTaskAction::HardShutdown == nextIo.m_ioAction)
            {
                // pass through -1 to force an RST with the closesocket
                ioError = sharedSocket->CloseSocket(static_cast<uint32_t>(SOCKET_ERROR));
                socket = INVALID_SOCKET;

                ioDone = lockedPattern->CompleteIo(nextIo, 0, ioError) != ctsIoStatus::ContinueIo;
                continue;
            }

            // else we need to initiate another IO
            // add-ref the IO about to start
            // TODO: socket is locked - no need for interlocked
            ioCount = sharedSocket->IncrementIo();

            std::shared_ptr<ctl::ctThreadIocp> ioThreadPool;
            OVERLAPPED* pOverlapped = nullptr;
            try
            {
                // these are the only calls which can throw in this function
                ioThreadPool = sharedSocket->GetIocpThreadpool();
                pOverlapped = ioThreadPool->new_request(
                    [weakSocket, nextIo](OVERLAPPED* pCallbackOverlapped) noexcept { ctsReadWriteIocpIoCompletionCallback(pCallbackOverlapped, weakSocket, nextIo); });
            }
            catch (...)
            {
                ioError = ctsConfig::PrintThrownException();
            }

            // if an exception prevented this IO from initiating,
            if (ioError != NO_ERROR)
            {
                ioCount = sharedSocket->DecrementIo();
                ioDone = lockedPattern->CompleteIo(nextIo, 0, ioError) != ctsIoStatus::ContinueIo;
                continue;
            }

            char* ioBuffer = nextIo.m_buffer + nextIo.m_bufferOffset;
            if (ctsTaskAction::Send == nextIo.m_ioAction)
            {
                if (!WriteFile(reinterpret_cast<HANDLE>(socket), ioBuffer, nextIo.m_bufferLength, nullptr, pOverlapped)) // NOLINT(performance-no-int-to-ptr)
                {
                    ioError = GetLastError();
                }
            }
            else
            {
                if (!ReadFile(reinterpret_cast<HANDLE>(socket), ioBuffer, nextIo.m_bufferLength, nullptr, pOverlapped)) // NOLINT(performance-no-int-to-ptr)
                {
                    ioError = GetLastError();
                }
            }
            //
            // not calling complete_io on success, since the IO completion will handle that in the callback
            //
            if (ERROR_IO_PENDING == ioError)
            {
                ioError = NO_ERROR;
            }

            if (ioError != NO_ERROR)
            {
                // must cancel the IOCP TP if the IO call fails
                ioThreadPool->cancel_request(pOverlapped);
                // decrement the IO count since it was not pended
                ioCount = sharedSocket->DecrementIo();

                const char* functionName = ctsTaskAction::Send == nextIo.m_ioAction ? "WriteFile" : "ReadFile";
                PRINT_DEBUG_INFO(L"\t\tIO Failed: %hs (%u) [ctsReadWriteIocp]\n", functionName, ioError);

                // call back to the socket to inform it that the call failed to see if it wants to request more IO
                switch (const ctsIoStatus protocolStatus = lockedPattern->CompleteIo(nextIo, 0, ioError))
                {
                    case ctsIoStatus::ContinueIo:
                        // the protocol wants to ignore the error and send more data
                        ioError = NO_ERROR;
                        ioDone = false;
                        break;

                    case ctsIoStatus::CompletedIo:
                        // the protocol wants to ignore the error but is done with IO
                        ioError = NO_ERROR;
                        ioDone = true;
                        break;

                    case ctsIoStatus::FailedIo:
                        // print the error on failure
                        ctsConfig::PrintErrorIfFailed(functionName, ioError);
                    // the protocol acknowledged the failure - socket is done with IO
                        ioError = static_cast<int>(lockedPattern->GetLastPatternError());
                        ioDone = true;
                        break;

                    default:
                        FAIL_FAST_MSG("ctsReadWriteIocp: unknown ctsSocket::IOStatus - %d\n", protocolStatus);
                }
            }
        }
    }
    else
    {
        ioError = WSAECONNABORTED;
    }

    if (0 == ioCount)
    {
        // complete the ctsSocket if we have no IO pended
        sharedSocket->CompleteState(ioError);
    }
}
} // namespace
