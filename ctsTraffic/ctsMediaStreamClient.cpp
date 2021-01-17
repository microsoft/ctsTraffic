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
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>
// ctl headers
#include <ctSockaddr.hpp>
// project headers
#include "ctsMediaStreamProtocol.hpp"
#include "ctsMediaStreamClient.h"
#include "ctsWinsockLayer.h"
#include "ctsIOTask.hpp"
#include "ctsIOPattern.h"
#include "ctsSocket.h"
#include "ctsConfig.h"

namespace ctsTraffic
{

    struct IoImplStatus
    {
        int m_errorCode = 0;
        bool m_continueIo = false;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Internal implementation functions
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    IoImplStatus ctsMediaStreamClientIoImpl(
        const std::shared_ptr<ctsSocket>& sharedSocket,
        SOCKET socket,
        const std::shared_ptr<ctsIoPattern>& lockedPattern,
        const ctsTask& task) noexcept;

    void ctsMediaStreamClientIoCompletionCallback(
        _In_ OVERLAPPED* pOverlapped,
        const std::weak_ptr<ctsSocket>& weakSocket,
        const ctsTask& task
    ) noexcept;

    void ctsMediaStreamClientConnectionCompletionCallback(
        _In_ OVERLAPPED* pOverlapped,
        const std::weak_ptr<ctsSocket>& weakSocket,
        const ctl::ctSockaddr& targetAddress
    ) noexcept;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to run Winsock IO using IO Completion Ports
    /// - with the specified ctsSocket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClient(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
    {
        // attempt to get a reference to the socket
        auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        // hold a reference on the socket
        const auto lockedSocket = sharedSocket->AcquireSocketLock();
        auto lockedPattern = lockedSocket.GetPattern();
        if (!lockedPattern || lockedSocket.GetSocket() == INVALID_SOCKET)
        {
            return;
        }

        // always register our ctsIOPattern callback since it's necessary for this IO Pattern
        // this callback can be invoked out-of-band directly from the IO Pattern class
        lockedPattern->RegisterCallback(
            [weakSocket](const ctsTask& task) noexcept {
                // attempt to get a reference to the socket
                auto lambdaSharedSocket(weakSocket.lock());
                if (!lambdaSharedSocket)
                {
                    return;
                }

                // hold a reference on the socket
                const auto lambdaLockedSocket = lambdaSharedSocket->AcquireSocketLock();
                const auto lambdaLockedPattern = lambdaLockedSocket.GetPattern();
                if (!lambdaLockedPattern || lambdaLockedSocket.GetSocket() == INVALID_SOCKET)
                {
                    return;
                }

                //
                // the below check with increment_io avoids a possible race-condition: 
                // - if increment_io() returns 1, it means our IO count in the main loop
                //   hit an io_count of 0 : which means that main thread will be completing this socket
                // - if this OOB callback ever returns 1, we cannot use this socket, since this socket
                //   will either be completed soon, or will have already been completed
                //
                // this special scenario exists because the callback doesn't hold a ref-count
                // - so this callback could be invoked after the mainline completed
                // this is still 'safe' due to the above socket locks
                //

                // increment IO count while issuing this Impl so we hold a ref-count during this out of band callback
                if (lambdaSharedSocket->IncrementIo() > 1)
                {
                    // only running this one task in the OOB callback
                    const IoImplStatus status = ctsMediaStreamClientIoImpl(lambdaSharedSocket, lambdaLockedSocket.GetSocket(), lambdaLockedPattern, task);
                    // decrement the IO count that we added before calling the Impl
                    // - complete_state if this happened to be the final IO refcount
                    if (lambdaSharedSocket->DecrementIo() == 0)
                    {
                        lambdaSharedSocket->CompleteState(status.m_errorCode);
                    }
                }
                else
                {
                    // in this case, the io_count in the ctsSocket was zero, so no IO was in flight to interrupt
                    // just decrement the IO count that we added before calling the Impl (no IO attempted)
                    lambdaSharedSocket->DecrementIo();
                }
            });

        // increment IO count while issuing this Impl so we hold a ref-count during this out of band callback
        sharedSocket->IncrementIo();
        IoImplStatus status = ctsMediaStreamClientIoImpl(sharedSocket, lockedSocket.GetSocket(), lockedPattern, lockedPattern->InitiateIo());
        while (status.m_continueIo)
        {
            // invoke the new IO call while holding a refcount to the prior IO in a tight loop
            status = ctsMediaStreamClientIoImpl(sharedSocket, lockedSocket.GetSocket(), lockedPattern, lockedPattern->InitiateIo());
        }
        if (0 == sharedSocket->DecrementIo())
        {
            sharedSocket->CompleteState(status.m_errorCode);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to 'connect' to the target server by sending a START command
    /// using IO Completion Ports
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClientConnect(const std::weak_ptr<ctsSocket>& weakSocket) noexcept
    {
        // attempt to get a reference to the socket
        auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        // hold a reference on the socket
        const auto lockedSocket = sharedSocket->AcquireSocketLock();
        if (lockedSocket.GetSocket() == INVALID_SOCKET)
        {
            sharedSocket->CompleteState(WSAECONNABORTED);
            return;
        }

        const SOCKET socket = lockedSocket.GetSocket();
        const auto error = ctsConfig::SetPreConnectOptions(socket);
        ctsConfig::PrintErrorIfFailed("SetPreConnectOptions", error);
        if (error != NO_ERROR)
        {
            sharedSocket->CompleteState(error);
            return;
        }

        const ctl::ctSockaddr targetAddress(sharedSocket->GetRemoteSockaddr());
        const ctsTask startTask = ctsMediaStreamMessage::Construct(MediaStreamAction::START);

        // Not add-ref'ing the IO on the socket since this is a single send() simulating connect()
        const auto response = ctsWSASendTo(
            sharedSocket,
            lockedSocket.GetSocket(),
            startTask,
            [weakSocket, targetAddress](OVERLAPPED* ov) noexcept {
                ctsMediaStreamClientConnectionCompletionCallback(ov, weakSocket, targetAddress);
            });

        if (NO_ERROR == response.m_errorCode)
        {
            // set the local and remote addresses on the socket object
            const ctl::ctSockaddr localAddr;
            auto localAddrLen = localAddr.length();
            if (0 == getsockname(socket, localAddr.sockaddr(), &localAddrLen))
            {
                sharedSocket->SetLocalSockaddr(localAddr);
            }
            sharedSocket->SetRemoteSockaddr(targetAddress);

            ctsConfig::PrintNewConnection(localAddr, targetAddress);

            PRINT_DEBUG_INFO(
                L"\t\tctsMediaStreamClient sent its START message to %ws\n",
                targetAddress.WriteCompleteAddress().c_str());
        }

        // complete only on failure or successfully completed inline (otherwise will complete in the IOCP callback)
        if (response.m_errorCode != WSA_IO_PENDING)
        {
            sharedSocket->CompleteState(response.m_errorCode);
        }
    }

    IoImplStatus ctsMediaStreamClientIoImpl(const std::shared_ptr<ctsSocket>& sharedSocket, SOCKET socket, const std::shared_ptr<ctsIoPattern>& lockedPattern, const ctsTask& task) noexcept
    {
        IoImplStatus returnStatus;

        switch (task.m_ioAction)
        {
            case ctsTaskAction::Send: // fall-through
            case ctsTaskAction::Recv:
            {
                // add-ref the IO about to start
                sharedSocket->IncrementIo();
                auto callback = [weak_reference = std::weak_ptr<ctsSocket>(sharedSocket), task](OVERLAPPED* ov) noexcept {
                    ctsMediaStreamClientIoCompletionCallback(ov, weak_reference, task);
                };

                PCSTR functionName{};
                wsIOResult result;
                if (ctsTaskAction::Send == task.m_ioAction)
                {
                    functionName = "WSASendTo";
                    result = ctsWSASendTo(sharedSocket, socket, task, std::move(callback));
                }
                else if (ctsTaskAction::Recv == task.m_ioAction)
                {
                    functionName = "WSARecvFrom";
                    result = ctsWSARecvFrom(sharedSocket, socket, task, std::move(callback));
                }
                else
                {
                    FAIL_FAST_MSG(
                        "ctsMediaStreamClientIoImpl: received an unexpected IOStatus in the ctsIOTask (%p)", &task);
                }

                if (WSA_IO_PENDING == result.m_errorCode)
                {
                    // if successful but did not complete inline
                    returnStatus.m_errorCode = result.m_errorCode;
                    returnStatus.m_continueIo = true;
                }
                else
                {
                    // IO successfully completed inline and the async completion won't be invoke
                    // - or the IO failed
                    if (result.m_errorCode != 0) PRINT_DEBUG_INFO(L"\t\tIO Failed: %hs (%d) [ctsMediaStreamClient]\n", functionName, result.m_errorCode);

                    const auto protocolStatus = lockedPattern->CompleteIo(
                        task,
                        result.m_bytesTransferred,
                        result.m_errorCode);

                    switch (protocolStatus)
                    {
                        case ctsIoStatus::ContinueIo:
                            // the protocol wants to ignore the error and send more data
                            returnStatus.m_errorCode = NO_ERROR;
                            returnStatus.m_continueIo = true;
                            break;

                        case ctsIoStatus::CompletedIo:
                            // the protocol wants to ignore the error but is done with IO
                            sharedSocket->CloseSocket();
                            returnStatus.m_errorCode = NO_ERROR;
                            returnStatus.m_continueIo = false;
                            break;

                        case ctsIoStatus::FailedIo:
                            // write out the error
                            ctsConfig::PrintErrorIfFailed(functionName, result.m_errorCode);
                            // the protocol acknoledged the failure - socket is done with IO
                            sharedSocket->CloseSocket();
                            returnStatus.m_errorCode = static_cast<int>(lockedPattern->GetLastPatternError());
                            returnStatus.m_continueIo = false;
                            break;

                        default:
                            FAIL_FAST_MSG("ctsMediaStreamClientIoImpl: unknown ctsSocket::IOStatus - %u\n", static_cast<unsigned>(protocolStatus));
                    }

                    // decrement the IO count if failed and/or inlined-completed
                    const auto ioCount = sharedSocket->DecrementIo();
                    // IO count should never be zero: callers should be guaranteeing a refcount before calling Impl
                    FAIL_FAST_IF_MSG(
                        0 == ioCount,
                        "ctsMediaStreamClient : ctsSocket::io_count fell to zero while the Impl function was called (dt %p ctsTraffic::ctsSocket)",
                        sharedSocket.get());
                }

                break;
            }

            case ctsTaskAction::None:
            {
                // nothing failed, just no more IO right now
                returnStatus.m_errorCode = NO_ERROR;
                returnStatus.m_continueIo = false;
                break;
            }

            case ctsTaskAction::Abort:
            {
                // the protocol signaled to immediately stop the stream
                lockedPattern->CompleteIo(task, 0, 0);
                sharedSocket->CloseSocket();

                returnStatus.m_errorCode = NO_ERROR;
                returnStatus.m_continueIo = false;
                break;
            }

            case ctsTaskAction::FatalAbort:
            {
                // the protocol indicated to rudely abort the connection
                lockedPattern->CompleteIo(task, 0, 0);
                sharedSocket->CloseSocket();

                returnStatus.m_errorCode = static_cast<int>(lockedPattern->GetLastPatternError());
                returnStatus.m_continueIo = false;
                break;
            }

            case ctsTaskAction::GracefulShutdown:
            case ctsTaskAction::HardShutdown:
            default: break;
        }

        return returnStatus;
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// IO Threadpool completion callback 
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClientIoCompletionCallback(
        _In_ OVERLAPPED* pOverlapped,
        const std::weak_ptr<ctsSocket>& weakSocket,
        const ctsTask& task) noexcept
    {
        auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        // hold a reference on the socket
        const auto lockedSocket = sharedSocket->AcquireSocketLock();
        auto lockedPattern = lockedSocket.GetPattern();
        if (!lockedPattern)
        {
            sharedSocket->DecrementIo();
            sharedSocket->CompleteState(WSAECONNABORTED);
            return;
        }

        const SOCKET socket = lockedSocket.GetSocket();

        int gle = NO_ERROR;
        DWORD transferred = 0;
        // scope to the socket lock
        {
            if (socket != INVALID_SOCKET)
            {
                DWORD flags;
                if (!WSAGetOverlappedResult(socket, pOverlapped, &transferred, FALSE, &flags))
                {
                    gle = WSAGetLastError();
                }
            }
            else
            {
                // we're intentionally ignoring the error when we have closed it early
                // - doing this because that's how we shutdown the client after processing all frames
                gle = NO_ERROR;
            }
        }

        if (gle == WSAEMSGSIZE)
        {
            // something truncated the datagram - don't treat it as a hard-error
            ctsConfig::PrintErrorInfo(L"MediaStream Client: IO failed with WSAEMSGSIZE: received [%d bytes] - expected [%d bytes]", transferred, task.m_bufferLength);
            gle = NO_ERROR;
        }

        // see if complete_io requests more IO
        const ctsIoStatus protocolStatus = lockedPattern->CompleteIo(task, transferred, gle);
        switch (protocolStatus)
        {
            case ctsIoStatus::ContinueIo:
            {
                // more IO is requested from the protocol
                IoImplStatus status;
                do
                {
                    // invoke the new IO call while holding a refcount to the prior IO in a tight loop
                    status = ctsMediaStreamClientIoImpl(sharedSocket, lockedSocket.GetSocket(), lockedPattern, lockedPattern->InitiateIo());
                }
                while (status.m_continueIo);

                gle = status.m_errorCode;
                break;
            }

            case ctsIoStatus::CompletedIo:
                sharedSocket->CloseSocket();
                gle = NO_ERROR;
                break;

            case ctsIoStatus::FailedIo:
                if (gle != 0)
                {
                    // the failure may have been a protocol error - in which case gle would just be NO_ERROR
                    ctsConfig::PrintErrorInfo(
                        L"MediaStream Client: IO failed (%ws) with error %d",
                        task.m_ioAction == ctsTaskAction::Recv ? L"WSARecvFrom" : L"WSASendTo", gle);
                }
                else
                {
                    ctsConfig::PrintErrorInfo(
                        L"MediaStream Client: IO succeeded (%ws) but the ctsIOProtocol failed the stream (%u)",
                        task.m_ioAction == ctsTaskAction::Recv ? L"WSARecvFrom" : L"WSASendTo",
                        lockedPattern->GetLastPatternError());
                }

                sharedSocket->CloseSocket();
                gle = static_cast<int>(lockedPattern->GetLastPatternError());
                break;

            default:
                FAIL_FAST_MSG(
                    "ctsMediaStreamClientIoCompletionCallback: unknown ctsSocket::IOStatus - %u\n",
                    static_cast<unsigned>(protocolStatus));
        }

        // always decrement *after* attempting new IO - the prior IO is now formally "done"
        if (sharedSocket->DecrementIo() == 0)
        {
            // if we have no more IO pended, complete the state
            sharedSocket->CompleteState(gle);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// IO Threadpool completion callback for the 'connect' request
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClientConnectionCompletionCallback(
        _In_ OVERLAPPED* pOverlapped,
        const std::weak_ptr<ctsSocket>& weakSocket,
        const ctl::ctSockaddr& targetAddress) noexcept
    {
        auto sharedSocket(weakSocket.lock());
        if (!sharedSocket)
        {
            return;
        }

        int gle = NO_ERROR;
        // hold a reference on the socket
        const auto lockedSocket = sharedSocket->AcquireSocketLock();
        const SOCKET socket = lockedSocket.GetSocket();
        if (socket == INVALID_SOCKET)
        {
            gle = WSAECONNABORTED;
        }
        else
        {
            DWORD transferred, flags; // unused
            if (!WSAGetOverlappedResult(socket, pOverlapped, &transferred, FALSE, &flags))
            {
                gle = WSAGetLastError();
            }
        }

        ctsConfig::PrintErrorIfFailed("\tWSASendTo (START request)", gle);

        if (NO_ERROR == gle)
        {
            // set the local and remote addr's
            const ctl::ctSockaddr localAddr;
            int localAddrLen = localAddr.length();
            if (0 == getsockname(socket, localAddr.sockaddr(), &localAddrLen))
            {
                sharedSocket->SetLocalSockaddr(localAddr);
            }
            sharedSocket->SetRemoteSockaddr(targetAddress);
            ctsConfig::PrintNewConnection(localAddr, targetAddress);
        }

        sharedSocket->CompleteState(gle);
    }

} // namespace