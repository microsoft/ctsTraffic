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
// ctl headers
#include <ctVersionConversion.hpp>
// project headers
#include "ctsSocket.h"

namespace ctsTraffic {

    void ctsWSASocket(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT;

    void ctsSimpleConnect(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT;
    void ctsSimpleAccept(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT;
    
    void ctsAcceptEx(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT;
    void ctsConnectEx(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT;

    void ctsReadWriteIocp(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT;
    void ctsSendRecvIocp(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT;
    void ctsRioIocp(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT;
}
