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
        int errorCode = 0;
        bool continueIo = false;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Internal implementation functions
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    IoImplStatus ctsMediaStreamClientIoImpl(
        const std::shared_ptr<ctsSocket>& _shared_socket,
        const ctsIOTask& _next_io) noexcept;

    void ctsMediaStreamClientIoCompletionCallback(
        _In_ OVERLAPPED* _overlapped,
        const std::weak_ptr<ctsSocket>& _weak_socket,
        const ctsIOTask& _io_task
    ) noexcept;

    void ctsMediaStreamClientConnectionCompletionCallback(
        _In_ OVERLAPPED* _overlapped,
        const std::weak_ptr<ctsSocket>& _weak_socket,
        const ctl::ctSockaddr& _target_address
    ) noexcept;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to run Winsock IO using IO Completion Ports
    /// - with the specified ctsSocket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClient(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket)
        {
            return;
        }
        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());

        // always register our ctsIOPattern callback since it's necessary for this IO Pattern
        // this callback can be invoked out-of-band directly from the IO Pattern class
        shared_pattern->register_callback(
            [_weak_socket](const ctsIOTask& _task) noexcept {
                // attempt to get a reference to the socket
                auto lambda_shared_socket(_weak_socket.lock());
                if (!lambda_shared_socket)
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
                if (lambda_shared_socket->increment_io() > 1)
                {
                    // only running this one task in the OOB callback
                    const IoImplStatus status = ctsMediaStreamClientIoImpl(lambda_shared_socket, _task);
                    // decrement the IO count that we added before calling the Impl
                    // - complete_state if this happened to be the final IO refcount
                    if (lambda_shared_socket->decrement_io() == 0)
                    {
                        lambda_shared_socket->complete_state(status.errorCode);
                    }
                }
                else
                {
                    // in this case, the io_count in the ctsSocket was zero, so no IO was in flight to interrupt
                    // just decrement the IO count that we added before calling the Impl (no IO attempted)
                    lambda_shared_socket->decrement_io();
                }
            });

        // increment IO count while issuing this Impl so we hold a ref-count during this out of band callback
        shared_socket->increment_io();
        IoImplStatus status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
        while (status.continueIo)
        {
            // invoke the new IO call while holding a refcount to the prior IO in a tight loop
            status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
        }
        if (0 == shared_socket->decrement_io())
        {
            shared_socket->complete_state(status.errorCode);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to 'connect' to the target server by sending a START command
    /// using IO Completion Ports
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClientConnect(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket)
        {
            return;
        }

        // scope to lock
        {
            const auto socket_ref(shared_socket->socket_reference());
            const SOCKET socket = socket_ref.socket();
            if (INVALID_SOCKET == socket)
            {
                shared_socket->complete_state(WSAECONNABORTED);
                return;
            }

            const auto error = ctsConfig::SetPreConnectOptions(socket);
            ctsConfig::PrintErrorIfFailed("SetPreConnectOptions", error);
            if (error != NO_ERROR)
            {
                shared_socket->complete_state(error);
                return;
            }
        }

        const ctl::ctSockaddr targetAddress(shared_socket->target_address());
        const ctsIOTask start_task = ctsMediaStreamMessage::Construct(MediaStreamAction::START);

        // Not add-ref'ing the IO on the socket since this is a single send() simulating connect()
        const auto response = ctsWSASendTo(
            shared_socket,
            start_task,
            [_weak_socket, targetAddress](OVERLAPPED* ov) noexcept {
                ctsMediaStreamClientConnectionCompletionCallback(ov, _weak_socket, targetAddress);
            });

        if (NO_ERROR == response.error_code)
        {
            const auto socket_ref(shared_socket->socket_reference());
            const SOCKET socket = socket_ref.socket();
            if (INVALID_SOCKET == socket)
            {
                shared_socket->complete_state(WSAECONNABORTED);
                return;
            }

            // set the local and remote addresses on the socket object
            const ctl::ctSockaddr local_addr;
            auto local_addr_len = local_addr.length();
            if (0 == getsockname(socket, local_addr.sockaddr(), &local_addr_len))
            {
                shared_socket->set_local_address(local_addr);
            }
            shared_socket->set_target_address(targetAddress);

            ctsConfig::PrintNewConnection(local_addr, targetAddress);

            try
            {
                PRINT_DEBUG_INFO(
                    L"\t\tctsMediaStreamClient sent its START message to %ws\n",
                    targetAddress.WriteCompleteAddress().c_str())
            }
            catch (...) {}
        }

        // complete only on failure or successfully completed inline (otherwise will complete in the IOCP callback)
        if (response.error_code != WSA_IO_PENDING)
        {
            shared_socket->complete_state(response.error_code);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// implementation of processing a ctsIOTask
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    IoImplStatus ctsMediaStreamClientIoImpl(const std::shared_ptr<ctsSocket>& _shared_socket, const ctsIOTask& _next_io) noexcept
    {
        IoImplStatus return_status;

        switch (_next_io.ioAction)
        {
            case IOTaskAction::Send: // fall-through
            case IOTaskAction::Recv:
            {
                // add-ref the IO about to start
                (void)_shared_socket->increment_io();
                auto callback = [weak_reference = std::weak_ptr<ctsSocket>(_shared_socket), nextio = _next_io](OVERLAPPED* ov) noexcept {
                    ctsMediaStreamClientIoCompletionCallback(ov, weak_reference, nextio);
                };

                PCSTR function_name{};
                wsIOResult result;
                if (IOTaskAction::Send == _next_io.ioAction)
                {
                    function_name = "WSASendTo";
                    result = ctsWSASendTo(_shared_socket, _next_io, std::move(callback));
                }
                else if (IOTaskAction::Recv == _next_io.ioAction)
                {
                    function_name = "WSARecvFrom";
                    result = ctsWSARecvFrom(_shared_socket, _next_io, std::move(callback));
                }
                else
                {
                    FAIL_FAST_MSG(
                        "ctsMediaStreamClientIoImpl: received an unexpected IOStatus in the ctsIOTask (%p)", &_next_io);
                }

                if (WSA_IO_PENDING == result.error_code)
                {
                    // if successful but did not complete inline
                    return_status.errorCode = result.error_code;
                    return_status.continueIo = true;
                }
                else
                {
                    // IO successfully completed inline and the async completion won't be invoke
                    // - or the IO failed
                    if (result.error_code != 0) PRINT_DEBUG_INFO(L"\t\tIO Failed: %hs (%d) [ctsMediaStreamClient]\n", function_name, result.error_code)

                    // hold a reference on the iopattern
                    auto shared_pattern(_shared_socket->io_pattern());
                    const auto protocol_status = shared_pattern->complete_io(
                        _next_io,
                        result.bytes_transferred,
                        result.error_code);

                    switch (protocol_status)
                    {
                        case ctsIOStatus::ContinueIo:
                            // the protocol wants to ignore the error and send more data
                            return_status.errorCode = NO_ERROR;
                            return_status.continueIo = true;
                            break;

                        case ctsIOStatus::CompletedIo:
                            // the protocol wants to ignore the error but is done with IO
                            _shared_socket->close_socket();
                            return_status.errorCode = NO_ERROR;
                            return_status.continueIo = false;
                            break;

                        case ctsIOStatus::FailedIo:
                            // write out the error
                            ctsConfig::PrintErrorIfFailed(function_name, result.error_code);
                            // the protocol acknoledged the failure - socket is done with IO
                            _shared_socket->close_socket();
                            return_status.errorCode = static_cast<int>(shared_pattern->get_last_error());
                            return_status.continueIo = false;
                            break;

                        default:
                            FAIL_FAST_MSG("ctsMediaStreamClientIoImpl: unknown ctsSocket::IOStatus - %u\n", static_cast<unsigned>(protocol_status));
                    }

                    // decrement the IO count if failed and/or inlined-completed
                    const auto io_count = _shared_socket->decrement_io();
                    // IO count should never be zero: callers should be guaranteeing a refcount before calling Impl
                    FAIL_FAST_IF_MSG(
                        0 == io_count,
                        "ctsMediaStreamClient : ctsSocket::io_count fell to zero while the Impl function was called (dt %p ctsTraffic::ctsSocket)",
                        _shared_socket.get());
                }

                break;
            }

            case IOTaskAction::None:
            {
                // nothing failed, just no more IO right now
                return_status.errorCode = NO_ERROR;
                return_status.continueIo = false;
                break;
            }

            case IOTaskAction::Abort:
            {
                // the protocol signaled to immediately stop the stream
                auto shared_pattern(_shared_socket->io_pattern());
                shared_pattern->complete_io(_next_io, 0, 0);
                _shared_socket->close_socket();

                return_status.errorCode = NO_ERROR;
                return_status.continueIo = false;
                break;
            }

            case IOTaskAction::FatalAbort:
            {
                // the protocol indicated to rudely abort the connection
                auto shared_pattern(_shared_socket->io_pattern());
                shared_pattern->complete_io(_next_io, 0, 0);
                _shared_socket->close_socket();

                return_status.errorCode = static_cast<int>(shared_pattern->get_last_error());
                return_status.continueIo = false;
                break;
            }

            case IOTaskAction::GracefulShutdown:
            case IOTaskAction::HardShutdown:
            default: break;
        }

        return return_status;
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// IO Threadpool completion callback 
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClientIoCompletionCallback(
        _In_ OVERLAPPED* _overlapped,
        const std::weak_ptr<ctsSocket>& _weak_socket,
        const ctsIOTask& _io_task
    ) noexcept
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket)
        {
            return;
        }

        int gle = NO_ERROR;
        DWORD transferred = 0;
        // scope to the socket lock
        {
            const auto socket_ref(shared_socket->socket_reference());
            const SOCKET socket = socket_ref.socket();
            if (socket != INVALID_SOCKET)
            {
                DWORD flags;
                if (!WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags))
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

        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());
        // see if complete_io requests more IO
        const ctsIOStatus protocol_status = shared_pattern->complete_io(_io_task, transferred, gle);
        switch (protocol_status)
        {
            case ctsIOStatus::ContinueIo:
            {
                // more IO is requested from the protocol
                IoImplStatus status;
                do
                {
                    // invoke the new IO call while holding a refcount to the prior IO in a tight loop
                    status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
                }
                while (status.continueIo);

                gle = status.errorCode;
                break;
            }

            case ctsIOStatus::CompletedIo:
                shared_socket->close_socket();
                gle = NO_ERROR;
                break;

            case ctsIOStatus::FailedIo:
                try
                {
                    if (gle != 0)
                    {
                        // the failure may have been a protocol error - in which case gle would just be NO_ERROR
                        ctsConfig::PrintErrorInfo(
                            wil::str_printf<std::wstring>(L"MediaStream Client: IO failed (%ws) with error %d",
                                _io_task.ioAction == IOTaskAction::Recv ? L"WSARecvFrom" : L"WSASendTo", gle).c_str());
                    }
                    else
                    {
                        ctsConfig::PrintErrorInfo(
                            wil::str_printf<std::wstring>(L"MediaStream Client: IO succeeded (%ws) but the ctsIOProtocol failed the stream (%u)",
                                _io_task.ioAction == IOTaskAction::Recv ? L"WSARecvFrom" : L"WSASendTo",
                                shared_pattern->get_last_error()).c_str());
                    }
                }
                catch (...)
                {
                }

                shared_socket->close_socket();
                gle = static_cast<int>(shared_pattern->get_last_error());
                break;

            default:
                FAIL_FAST_MSG(
                    "ctsMediaStreamClientIoCompletionCallback: unknown ctsSocket::IOStatus - %u\n",
                    static_cast<unsigned>(protocol_status));
        }

        // always decrement *after* attempting new IO - the prior IO is now formally "done"
        if (shared_socket->decrement_io() == 0)
        {
            // if we have no more IO pended, complete the state
            shared_socket->complete_state(gle);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// IO Threadpool completion callback for the 'connect' request
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClientConnectionCompletionCallback(
        _In_ OVERLAPPED* _overlapped,
        const std::weak_ptr<ctsSocket>& _weak_socket,
        const ctl::ctSockaddr& _target_address) noexcept
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket)
        {
            return;
        }

        int gle = NO_ERROR;
        const auto socket_ref(shared_socket->socket_reference());
        const SOCKET socket = socket_ref.socket();
        if (INVALID_SOCKET == socket)
        {
            gle = WSAECONNABORTED;
        }
        else
        {
            DWORD flags;
            DWORD transferred;
            if (!WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags))
            {
                gle = WSAGetLastError();
            }
        }

        ctsConfig::PrintErrorIfFailed("\tWSASendTo (START request)", gle);

        if (NO_ERROR == gle)
        {
            // set the local and remote addr's
            const ctl::ctSockaddr local_addr;
            int local_addr_len = local_addr.length();
            if (0 == getsockname(socket, local_addr.sockaddr(), &local_addr_len))
            {
                shared_socket->set_local_address(local_addr);
            }
            shared_socket->set_target_address(_target_address);
            ctsConfig::PrintNewConnection(local_addr, _target_address);
        }

        shared_socket->complete_state(gle);
    }

} // namespace