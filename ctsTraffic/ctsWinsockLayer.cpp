/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <exception>
#include <memory>

// os headers
#include <Windows.h>
#include <WinSock2.h>

// ctl headers
#include <ctSockaddr.hpp>
#include <ctThreadIocp.hpp>

// project headers
#include "ctsWinsockLayer.h"
#include "ctsIOTask.hpp"
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsSocketGuard.hpp"

//
// These functions encapsulate making Winsock API calls
// - primarily facilitating unit testing of interface logic that calls through Winsock
//   but also simplifying the logic behind the code to make reasoning over the code more straight forward
//
namespace ctsTraffic
{
    //
    // WSARecvFrom
    //
    wsIOResult ctsWSARecvFrom(
        const std::shared_ptr<ctsSocket>& _shared_socket,
        const ctsIOTask& _task,
        std::function<void(OVERLAPPED*)>&& _callback) NOEXCEPT
    {
        auto socket_lock(ctsGuardSocket(_shared_socket));
        SOCKET socket = socket_lock.get();
        if (INVALID_SOCKET == socket) {
            return wsIOResult(WSAECONNABORTED);
        }

        wsIOResult return_result;
        try {
            const auto& io_thread_pool = _shared_socket->thread_pool();
            OVERLAPPED* pov = io_thread_pool->new_request(std::move(_callback));

            WSABUF wsabuf;
            wsabuf.buf = _task.buffer + _task.buffer_offset;
            wsabuf.len = _task.buffer_length;

            DWORD flags = 0;
            if (::WSARecvFrom(socket, &wsabuf, 1, nullptr, &flags, nullptr, nullptr, pov, nullptr) != 0) {
                return_result.error_code = ::WSAGetLastError();
                // IO pended == successfully initiating the IO
                if (return_result.error_code != WSA_IO_PENDING) {
                    // must cancel the IOCP TP if the IO call fails
                    io_thread_pool->cancel_request(pov);
                }
                // will return WSA_IO_PENDING transparently to the caller

            } else {
                if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP) {
                    return_result.error_code = ERROR_SUCCESS;
                    // OVERLAPPED.InternalHigh == the number of bytes transferred for the I/O request.
                    // - this member is set when the request is completed inline
                    return_result.bytes_transferred = static_cast<unsigned long>(pov->InternalHigh);
                    // completed inline, so the TP won't be notified
                    io_thread_pool->cancel_request(pov);
                } else {
                    // WSARecvFrom returned success, but inline completions is not enabled
                    // so the IOCP callback will be invoked - thus will return WSA_IO_PENDING
                    return_result.error_code = WSA_IO_PENDING;
                }
            }
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            return wsIOResult(WSAENOBUFS);
        }

        return return_result;
    }

    //
    // WSASendTo
    //
    wsIOResult ctsWSASendTo(
        const std::shared_ptr<ctsSocket>& _shared_socket,
        const ctsIOTask& _task,
        std::function<void(OVERLAPPED*)>&& _callback) NOEXCEPT
    {
        auto socket_lock(ctsGuardSocket(_shared_socket));
        SOCKET socket = socket_lock.get();
        if (INVALID_SOCKET == socket) {
            return wsIOResult(WSAECONNABORTED);
        }

        wsIOResult return_result;
        try {
            const ctl::ctSockaddr& targetAddress = _shared_socket->target_address();

            const auto& io_thread_pool = _shared_socket->thread_pool();
            OVERLAPPED* pov = io_thread_pool->new_request(std::move(_callback));

            WSABUF wsabuf;
            wsabuf.buf = _task.buffer + _task.buffer_offset;
            wsabuf.len = _task.buffer_length;

            if (::WSASendTo(socket, &wsabuf, 1, nullptr, 0, targetAddress.sockaddr(), targetAddress.length(), pov, nullptr) != 0) {
                return_result.error_code = ::WSAGetLastError();
                // IO pended == successfully initiating the IO
                if (return_result.error_code != WSA_IO_PENDING) {
                    // must cancel the IOCP TP if the IO call fails
                    io_thread_pool->cancel_request(pov);
                }
                // will return WSA_IO_PENDING transparently to the caller

            } else {
                if (ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP) {
                    return_result.error_code = ERROR_SUCCESS;
                    // OVERLAPPED.InternalHigh == the number of bytes transferred for the I/O request.
                    // - this member is set when the request is completed inline
                    return_result.bytes_transferred = static_cast<unsigned long>(pov->InternalHigh);
                    // completed inline, so the TP won't be notified
                    io_thread_pool->cancel_request(pov);
                } else {
                    // WSARecvFrom returned success, but inline completions is not enabled
                    // so the IOCP callback will be invoked - thus will return WSA_IO_PENDING
                    return_result.error_code = WSA_IO_PENDING;
                }
            }
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            return wsIOResult(WSAENOBUFS);
        }

        return return_result;
    }

    wsIOResult ctsSetLingertoRSTSocket(SOCKET _socket)
    {
        wsIOResult return_result;
        ::linger linger_option;
        linger_option.l_onoff = 1;
        linger_option.l_linger = 0;
        if (::setsockopt(_socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&linger_option), static_cast<int>(sizeof(linger_option))) != 0) {
            return_result.error_code = ::WSAGetLastError();
        }

        return return_result;
    }
}
