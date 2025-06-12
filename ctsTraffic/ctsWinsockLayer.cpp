/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <exception>
#include <memory>
// using wil::network to pull in all necessary networking headers
#include <wil/network.h>
// ctl headers
#include <ctThreadIocp.hpp>
// project headers
#include "ctsWinsockLayer.h"
#include "ctsIOTask.hpp"
#include "ctsConfig.h"
#include "ctsSocket.h"

//
// These functions encapsulate making Winsock API calls
// - primarily facilitating unit testing of interface logic that calls through Winsock
//   but also simplifying the logic behind the code to make reasoning over the code more straight forward
//
namespace ctsTraffic
{
    // ReSharper disable once CppInconsistentNaming
    wsIOResult ctsWSARecvFrom(
        const std::shared_ptr<ctsSocket>& sharedSocket,
        SOCKET socket,
        const ctsTask& task,
        std::function<void(OVERLAPPED*)>&& callback) noexcept
    {
        if (INVALID_SOCKET == socket)
        {
            return wsIOResult(WSAECONNABORTED);
        }

        wsIOResult returnResult;
        try
        {
            const auto& ioThreadPool = sharedSocket->GetIocpThreadpool();
            OVERLAPPED* pOverlapped = ioThreadPool->new_request(std::move(callback));

            WSABUF wsabuffer;
            wsabuffer.buf = task.m_buffer + task.m_bufferOffset;
            wsabuffer.len = task.m_bufferLength;

            DWORD flags = 0;
            if (WSARecvFrom(socket, &wsabuffer, 1, nullptr, &flags, nullptr, nullptr, pOverlapped, nullptr) != 0)
            {
                returnResult.m_errorCode = WSAGetLastError();
                // IO pended == successfully initiating the IO
                if (returnResult.m_errorCode != WSA_IO_PENDING)
                {
                    // must cancel the IOCP TP if the IO call fails
                    ioThreadPool->cancel_request(pOverlapped);
                }
                // will return WSA_IO_PENDING transparently to the caller
            }
            else
            {
                if (ctsConfig::g_configSettings->Options & ctsConfig::OptionType::HandleInlineIocp)
                {
                    returnResult.m_errorCode = ERROR_SUCCESS;
                    // OVERLAPPED.InternalHigh == the number of bytes transferred for the I/O request.
                    // - this member is set when the request is completed inline
                    returnResult.m_bytesTransferred = static_cast<uint32_t>(pOverlapped->InternalHigh);
                    // completed inline, so the TP won't be notified
                    ioThreadPool->cancel_request(pOverlapped);
                }
                else
                {
                    // WSARecvFrom returned success, but inline completions is not enabled
                    // so the IOCP callback will be invoked - thus will return WSA_IO_PENDING
                    returnResult.m_errorCode = WSA_IO_PENDING;
                }
            }
        }
        catch (...)
        {
            const auto error = ctsConfig::PrintThrownException();
            return wsIOResult(error);
        }

        return returnResult;
    }

    // ReSharper disable once CppInconsistentNaming
    wsIOResult ctsWSASendTo(
        const std::shared_ptr<ctsSocket>& sharedSocket,
        SOCKET socket,
        const ctsTask& task,
        std::function<void(OVERLAPPED*)>&& callback) noexcept
    {
        if (INVALID_SOCKET == socket)
        {
            return wsIOResult(WSAECONNABORTED);
        }

        wsIOResult returnResult;
        try
        {
            const auto& targetAddress = sharedSocket->GetRemoteSockaddr();
            const auto& ioThreadPool = sharedSocket->GetIocpThreadpool();
            OVERLAPPED* pOverlapped = ioThreadPool->new_request(std::move(callback));

            WSABUF wsaBuffer;
            wsaBuffer.buf = task.m_buffer + task.m_bufferOffset;
            wsaBuffer.len = task.m_bufferLength;

            if (WSASendTo(socket, &wsaBuffer, 1, nullptr, 0, targetAddress.sockaddr(),
                          targetAddress.size(), pOverlapped, nullptr) != 0)
            {
                returnResult.m_errorCode = WSAGetLastError();
                // IO pended == successfully initiating the IO
                if (returnResult.m_errorCode != WSA_IO_PENDING)
                {
                    // must cancel the IOCP TP if the IO call fails
                    ioThreadPool->cancel_request(pOverlapped);
                }
                // will return WSA_IO_PENDING transparently to the caller
            }
            else
            {
                if (ctsConfig::g_configSettings->Options & ctsConfig::OptionType::HandleInlineIocp)
                {
                    returnResult.m_errorCode = ERROR_SUCCESS;
                    // OVERLAPPED.InternalHigh == the number of bytes transferred for the I/O request.
                    // - this member is set when the request is completed inline
                    returnResult.m_bytesTransferred = static_cast<uint32_t>(pOverlapped->InternalHigh);
                    // completed inline, so the TP won't be notified
                    ioThreadPool->cancel_request(pOverlapped);
                }
                else
                {
                    // WSARecvFrom returned success, but inline completions is not enabled
                    // so the IOCP callback will be invoked - thus will return WSA_IO_PENDING
                    returnResult.m_errorCode = WSA_IO_PENDING;
                }
            }
        }
        catch (...)
        {
            const auto error = ctsConfig::PrintThrownException();
            return wsIOResult(error);
        }

        return returnResult;
    }

    wsIOResult ctsSetLingerToResetSocket(SOCKET socket) noexcept
    {
        wsIOResult returnResult{};
        linger lingerOption{};
        lingerOption.l_onoff = 1;
        lingerOption.l_linger = 0;
        if (setsockopt(socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&lingerOption),
                       static_cast<int>(sizeof lingerOption)) != 0)
        {
            returnResult.m_errorCode = WSAGetLastError();
            PRINT_DEBUG_INFO(L"\t\tIO Failed: setsockopt(SO_LINGER) (%d)\n", returnResult.m_errorCode);
        }
        else
        {
            PRINT_DEBUG_INFO(L"\t\tIO successfully called setsockopt(SO_LINGER) (%d)\n", returnResult.m_errorCode);
        }

        return returnResult;
    }
}
