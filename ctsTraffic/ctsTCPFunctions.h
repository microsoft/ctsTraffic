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
// project headers
#include "ctsSocket.h"

namespace ctsTraffic
{
    // ReSharper disable once CppInconsistentNaming
    void ctsWSASocket(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;

    void ctsConnectByName(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;

    void ctsSimpleConnect(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;
    void ctsSimpleAccept(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;

    void ctsAcceptEx(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;
    void ctsConnectEx(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;

    void ctsReadWriteIocp(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;
    void ctsSendRecvIocp(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;
    void ctsRioIocp(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;
}
