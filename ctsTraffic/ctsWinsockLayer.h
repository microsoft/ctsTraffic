/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <memory>
#include <functional>

// os headers
#include <Windows.h>

// project headers
#include "ctsIOTask.hpp"
#include "ctsSocket.h"

//
// These functions encapsulate making Winsock API calls
// - primarily facilitating unit testing of interface logic that calls through Winsock
//   but also simplifying the logic behind the code to make reasoning over the code more straight forward
//

namespace ctsTraffic {

    struct wsIOResult
    {
        int error_code = 0;
        unsigned long bytes_transferred = 0;

        wsIOResult() noexcept
        {
        }
        explicit wsIOResult(int _error) noexcept
        {
            error_code = _error;
        }
    };

    //
    // WSARecvFrom
    //
    wsIOResult ctsWSARecvFrom(
        const std::shared_ptr<ctsSocket>& _shared_socket,
        const ctsIOTask& _task,
        std::function<void(OVERLAPPED*)>&& _callback) noexcept;

    //
    // WSASendTo
    //
    wsIOResult ctsWSASendTo(
        const std::shared_ptr<ctsSocket>& _shared_socket,
        const ctsIOTask& _task,
        std::function<void(OVERLAPPED*)>&& _callback) noexcept;

    //
    // Set LINGER options to force a RST when the socket is closed
    //
    wsIOResult ctsSetLingertoRSTSocket(SOCKET _socket) noexcept;
}