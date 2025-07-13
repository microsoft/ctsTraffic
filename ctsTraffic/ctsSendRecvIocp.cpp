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
    // forward declaration
    void ctsSendRecvIocp(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;

    struct ctsSendRecvStatus
    {
        // Winsock error code
        uint32_t m_ioErrorCode = NO_ERROR;
        // flag if to request another ctsIOTask
        bool m_ioDone = false;
        // returns if IO was started (since can return !io_done, but I/O wasn't started yet)
        bool m_ioStarted = false;
    };

    // IO Threadpool completion callback 
    static void ctsSendRecvCompletionCallback(
        _In_ OVERLAPPED* pOverlapped,
        const std::weak_ptr<ctsSocket>& weakSocket,
        const ctsTask& task) noexcept
    {
        const auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        int gle = NO_ERROR;

        // hold a reference on the socket
        const auto lockedSocket = sharedSocket->AcquireSocketLock();
        const auto lockedPattern = lockedSocket.GetPattern();
        if (!lockedPattern)
        {
            gle = WSAECONNABORTED;
        }

        const auto socket = lockedSocket.GetSocket();
        DWORD transferred = 0;
        if (gle == NO_ERROR)
        {
            // try to get the success/error code and bytes transferred (under the socket lock)
            // if we no longer have a valid socket or the pattern was destroyed, return early
            if (INVALID_SOCKET == socket)
            {
                gle = WSAECONNABORTED;
            }
            else
            {
                DWORD flags{};
                if (!WSAGetOverlappedResult(socket, pOverlapped, &transferred, FALSE, &flags))
                {
                    gle = WSAGetLastError();
                }
            }
        }

        // write to PrintError if the IO failed
        const char* functionName = ctsTaskAction::Send == task.m_ioAction ? "WSASend" : "WSARecv";
        if (gle != NO_ERROR)
        {
            PRINT_DEBUG_INFO(L"\t\tIO Failed: %hs (%d) [ctsSendRecvIocp]\n", functionName, gle);
        }

        if (lockedPattern)
        {
            // see if complete_io requests more IO
            switch (const ctsIoStatus protocolStatus = lockedPattern->CompleteIo(task, transferred, gle))
            {
            case ctsIoStatus::ContinueIo:
                // more IO is requested from the protocol : invoke the new IO call while holding a ref-count to the prior IO
                ctsSendRecvIocp(weakSocket);
                break;

            case ctsIoStatus::CompletedIo:
                // no more IO is requested from the protocol : indicate success
                gle = NO_ERROR;
                break;

            case ctsIoStatus::FailedIo:
                // write out the error to the error log since the protocol sees this as a hard error
                ctsConfig::PrintErrorIfFailed(functionName, gle);
                // protocol sees this as a failure : capture the error the protocol recorded
                gle = static_cast<int>(lockedPattern->GetLastPatternError());
                break;

            default:
                FAIL_FAST_MSG("ctsSendRecvIocp : unknown ctsSocket::IOStatus %d", protocolStatus);
            }
        }

        // always decrement *after* attempting new IO : the prior IO is now formally "done"
        if (sharedSocket->DecrementIo() == 0)
        {
            // if we have no more IO pended, complete the state
            sharedSocket->CompleteState(gle);
        }
    }

    //
    // Attempts the IO specified in the ctsIOTask on the ctsSocket
    //
    // ** ctsSocket::increment_io must have been called before this function was invoked
    //
    static ctsSendRecvStatus ctsSendRecvProcessTask(SOCKET socket, const std::shared_ptr<ctsSocket>& sharedSocket, const std::shared_ptr<ctsIoPattern>& sharedPattern, const ctsTask& nextIo) noexcept
    {
        ctsSendRecvStatus returnStatus;

        // if we no longer have a valid socket return early
        if (INVALID_SOCKET == socket)
        {
            returnStatus.m_ioErrorCode = WSAECONNABORTED;
            returnStatus.m_ioStarted = false;
            returnStatus.m_ioDone = true;
            // even if the socket was closed we still must complete the IO request
            sharedPattern->CompleteIo(nextIo, 0, returnStatus.m_ioErrorCode);
            return returnStatus;
        }

        if (ctsTaskAction::GracefulShutdown == nextIo.m_ioAction)
        {
            if (shutdown(socket, SD_SEND) != 0)
            {
                returnStatus.m_ioErrorCode = WSAGetLastError();
                PRINT_DEBUG_INFO(L"\t\tIO Failed: shutdown(SD_SEND) (%d) [ctsSendRecvIocp]\n", returnStatus.m_ioErrorCode);
            }
            else
            {
                PRINT_DEBUG_INFO(L"\t\tIO successfully called shutdown(SD_SEND) (%d) [ctsSendRecvIocp]\n", returnStatus.m_ioErrorCode);
            }
            returnStatus.m_ioDone = sharedPattern->CompleteIo(nextIo, 0, returnStatus.m_ioErrorCode) != ctsIoStatus::ContinueIo;
            returnStatus.m_ioStarted = false;
        }
        else if (ctsTaskAction::HardShutdown == nextIo.m_ioAction)
        {
            // pass through -1 to force an RST with the closesocket
            returnStatus.m_ioErrorCode = sharedSocket->CloseSocket(static_cast<uint32_t>(SOCKET_ERROR));
            returnStatus.m_ioDone = sharedPattern->CompleteIo(nextIo, 0, returnStatus.m_ioErrorCode) != ctsIoStatus::ContinueIo;
            returnStatus.m_ioStarted = false;
        }
        else
        {
            try
            {
                // attempt to allocate an IO thread-pool object
                const std::shared_ptr<ctl::ctThreadIocp>& ioThreadPool(sharedSocket->GetIocpThreadpool());
                OVERLAPPED* const pOverlapped = ioThreadPool->new_request(
                    [weak_reference = std::weak_ptr(sharedSocket), nextIo](OVERLAPPED* pCallbackOverlapped) noexcept {
                        ctsSendRecvCompletionCallback(pCallbackOverlapped, weak_reference, nextIo);
                    });

                WSABUF wsaBuffer{};
                wsaBuffer.buf = nextIo.m_buffer + nextIo.m_bufferOffset;
                wsaBuffer.len = nextIo.m_bufferLength;

                PCSTR functionName{};
                if (ctsTaskAction::Send == nextIo.m_ioAction)
                {
                    if (nextIo.m_bufferLength == 0)
                    {
                        PRINT_DEBUG_INFO(L"\t\tIO sending zero bytes! [ctsSendRecvIocp]\n");
                    }

                    functionName = "WSASend";
                    if (WSASend(socket, &wsaBuffer, 1, nullptr, 0, pOverlapped, nullptr) != 0)
                    {
                        returnStatus.m_ioErrorCode = WSAGetLastError();
                    }
                }
                else
                {
                    functionName = "WSARecv";
                    DWORD flags = ctsConfig::g_configSettings->Options & ctsConfig::OptionType::MsgWaitAll ? MSG_WAITALL : 0;
                    if (WSARecv(socket, &wsaBuffer, 1, nullptr, &flags, pOverlapped, nullptr) != 0)
                    {
                        returnStatus.m_ioErrorCode = WSAGetLastError();
                    }
                }
                //
                // not calling complete_io if returned IO pended 
                // not calling complete_io if returned success but not handling inline completions
                //
                if (WSA_IO_PENDING == returnStatus.m_ioErrorCode ||
                    // ReSharper disable once CppRedundantParentheses
                    (NO_ERROR == returnStatus.m_ioErrorCode && !(ctsConfig::g_configSettings->Options & ctsConfig::OptionType::HandleInlineIocp)))
                {
                    returnStatus.m_ioErrorCode = NO_ERROR;
                    returnStatus.m_ioStarted = true;
                    returnStatus.m_ioDone = false;
                }
                else
                {
                    // process the completion if the API call failed, or if it succeeded, and we're handling the completion inline, 
                    returnStatus.m_ioStarted = false;
                    // determine # of bytes transferred, if any
                    DWORD bytesTransferred = 0;
                    if (NO_ERROR == returnStatus.m_ioErrorCode)
                    {
                        DWORD flags{};
                        if (!WSAGetOverlappedResult(socket, pOverlapped, &bytesTransferred, FALSE, &flags))
                        {
                            FAIL_FAST_MSG(
                                "WSAGetOverlappedResult failed (%d) after the IO request (%hs) succeeded", WSAGetLastError(), functionName);
                        }
                    }
                    else
                    {
                        PRINT_DEBUG_INFO(L"\t\tIO Failed: %hs (%d) [ctsSendRecvIocp]\n", functionName, returnStatus.m_ioErrorCode);
                    }

                    // must cancel the IOCP TP since IO is not pended
                    ioThreadPool->cancel_request(pOverlapped);
                    // call back to the socket to see if it wants more IO
                    switch (const ctsIoStatus protocolStatus = sharedPattern->CompleteIo(nextIo, bytesTransferred, returnStatus.m_ioErrorCode))
                    {
                    case ctsIoStatus::ContinueIo:
                        // The protocol layer wants to transfer more data
                        // if the prior IO request failed, the protocol wants to ignore the error
                        returnStatus.m_ioErrorCode = NO_ERROR;
                        returnStatus.m_ioDone = false;
                        break;

                    case ctsIoStatus::CompletedIo:
                        // The protocol layer has successfully complete all IO on this connection
                        // if the prior IO request failed, the protocol wants to ignore the error
                        returnStatus.m_ioErrorCode = NO_ERROR;
                        returnStatus.m_ioDone = true;
                        break;

                    case ctsIoStatus::FailedIo:
                        // write out the error
                        ctsConfig::PrintErrorIfFailed(functionName, sharedPattern->GetLastPatternError());
                        // the protocol acknowledged the failure - socket is done with IO
                        returnStatus.m_ioErrorCode = sharedPattern->GetLastPatternError();
                        returnStatus.m_ioDone = true;
                        break;

                    default:
                        FAIL_FAST_MSG("ctsSendRecvIocp: unknown ctsSocket::IOStatus - %d\n", protocolStatus);
                    }
                }
            }
            catch (...)
            {
                returnStatus.m_ioErrorCode = ctsConfig::PrintThrownException();
                returnStatus.m_ioDone = sharedPattern->CompleteIo(nextIo, 0, returnStatus.m_ioErrorCode) != ctsIoStatus::ContinueIo;
                returnStatus.m_ioStarted = false;
            }
        }

        return returnStatus;
    }

    //
    // This is the callback for the threadpool timer.
    // Processes the given task and then calls ctsSendRecvIocp function to deal with any additional tasks
    //
    static void ctsSendRecvTimerCallback(const std::weak_ptr<ctsSocket>& weakSocket, const ctsTask& nextIo) noexcept
    {
        // attempt to get a reference to the socket
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
        // if lockedSocket has an INVALID_SOCKET, continue below to ctsSendRecvProcessTask
        // where it's handled appropriately

        // increment IO for this IO request
        sharedSocket->IncrementIo();

        // run the ctsIOTask (next_io) that was scheduled through the TP timer
        const ctsSendRecvStatus status = ctsSendRecvProcessTask(lockedSocket.GetSocket(), sharedSocket, lockedPattern, nextIo);
        // if no IO was started, decrement the IO counter
        if (!status.m_ioStarted)
        {
            if (0 == sharedSocket->DecrementIo())
            {
                // this should never be zero since we should be holding a ref-count for this callback
                FAIL_FAST_MSG(
                    "The ref-count of the ctsSocket object (%p) fell to zero during a scheduled callback", sharedSocket.get());
            }
        }
        // continue requesting IO if this connection still isn't done with all IO after scheduling the prior IO
        if (!status.m_ioDone)
        {
            ctsSendRecvIocp(weakSocket);
        }
        // finally decrement the IO that was counted for this IO that was completed async
        if (sharedSocket->DecrementIo() == 0)
        {
            // if we have no more IO pended, complete the state
            sharedSocket->CompleteState(status.m_ioErrorCode);
        }
    }

    // The function registered with ctsConfig
    void ctsSendRecvIocp(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
    {
        // attempt to get a reference to the socket
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
        // if lockedSocket has an INVALID_SOCKET, continue below to ctsSendRecvProcessTask
        // where it's handled appropriately

        //
        // loop until failure or initiate_io returns None
        //
        // IO is always done in the ctsProcessIOTask function,
        // - either synchronously or scheduled through a timer object
        //
        // The IO ref-count must be incremented here to hold an IO count on the socket
        // - so that we won't inadvertently call complete_state() while IO is still being scheduled
        //
        sharedSocket->IncrementIo();

        ctsSendRecvStatus status{};
        while (!status.m_ioDone)
        {
            const ctsTask nextIo = lockedPattern->InitiateIo();
            if (ctsTaskAction::None == nextIo.m_ioAction)
            {
                // nothing failed, just no more IO right now
                break;
            }

            // increment IO for each individual request
            sharedSocket->IncrementIo();

            if (nextIo.m_timeOffsetMilliseconds > 0)
            {
                // set_timer can throw
                try
                {
                    sharedSocket->SetTimer(nextIo, ctsSendRecvTimerCallback);
                    status.m_ioStarted = true; // IO started in the context of keeping the count incremented
                    status.m_ioDone = true;
                }
                catch (...)
                {
                    status.m_ioErrorCode = ctsConfig::PrintThrownException();
                    status.m_ioStarted = false;
                }
            }
            else
            {
                status = ctsSendRecvProcessTask(lockedSocket.GetSocket(), sharedSocket, lockedPattern, nextIo);
            }

            // if no IO was started, decrement the IO counter
            if (!status.m_ioStarted)
            {
                // since IO is not pended, remove the ref-count
                if (0 == sharedSocket->DecrementIo())
                {
                    // this should never be zero as we are holding a reference outside the loop
                    FAIL_FAST_MSG(
                        "The ctsSocket (%p) ref-count fell to zero while this function was holding a reference", sharedSocket.get());
                }
            }
        }
        // decrement IO at the end to release the ref-count held before the loop
        if (0 == sharedSocket->DecrementIo())
        {
            sharedSocket->CompleteState(status.m_ioErrorCode);
        }
    }
} // namespace
