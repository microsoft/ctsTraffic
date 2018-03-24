/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include <mswsock.h>
// ctl headers
#include <ctVersionConversion.hpp>

// ** NOTE ** should not include any local project cts headers - to avoid circular references

namespace ctsTraffic {

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The ctsIOTask struct instructs the caller on what action to perform
    /// - and provides it the buffer it should use to send/recv data
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    enum class IOTaskAction
    {
        None,
        Send,
        Recv,
        GracefulShutdown,
        HardShutdown,
        Abort,
        FatalAbort
    };

    struct ctsIOTask {
        long long time_offset_milliseconds = 0LL;
        RIO_BUFFERID rio_bufferid = RIO_INVALID_BUFFERID;

        _Field_size_full_(buffer_length)
        char* buffer = nullptr;
        unsigned long buffer_length = 0UL;
        unsigned long buffer_offset = 0UL;
        unsigned long expected_pattern_offset = 0UL;
        IOTaskAction ioAction = IOTaskAction::None;

        // (internal) flag identifying the type of buffer
        enum class BufferType
        {
            Null,
            TcpConnectionId,
            UdpConnectionId,
            Static,
            Tracked
        } buffer_type = BufferType::Null;
        // (internal) flag if this IO request is tracked and verified
        bool track_io = false;

        static LPCWSTR PrintIOAction(const IOTaskAction& _action) NOEXCEPT
        {
            switch (_action) {
                case IOTaskAction::None: return L"None";
                case IOTaskAction::Send: return L"Send";
                case IOTaskAction::Recv: return L"Recv";
                case IOTaskAction::GracefulShutdown: return L"GracefulShutdown";
                case IOTaskAction::HardShutdown: return L"HardShutdown";
                case IOTaskAction::Abort: return L"Abort";
                case IOTaskAction::FatalAbort: return L"FatalAbort";
            }

            return L"Unknown IOAction";
        }
    };

} // namespace