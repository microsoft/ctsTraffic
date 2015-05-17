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

// ctl headers
#include <ctVersionConversion.hpp>

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
        int error_code;
        unsigned long bytes_transferred;

        wsIOResult() NOEXCEPT : error_code(0), bytes_transferred(0)
        {
        }

        wsIOResult(int _error) NOEXCEPT : error_code(_error), bytes_transferred(0)
        {
        }
    };

    ///
    /// WSARecvFrom
    ///
    wsIOResult ctsWSARecvFrom(
        std::shared_ptr<ctsSocket>& _shared_socket,
        const ctsIOTask& _task,
        std::function<void(OVERLAPPED*)> _callback) NOEXCEPT;

    ///
    /// WSASendTo
    ///
    wsIOResult ctsWSASendTo(
        std::shared_ptr<ctsSocket>& _shared_socket,
        const ctsIOTask& _task,
        std::function<void(OVERLAPPED*)> _callback) NOEXCEPT;
}