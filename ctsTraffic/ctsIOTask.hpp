/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// using wil::network to pull in all necessary networking headers
#include <wil/network.h>

// ** NOTE ** should not include any local project cts headers - to avoid circular references

namespace ctsTraffic
{
    // The ctsIOTask struct instructs the caller on what action to perform
    // - and provides it the buffer it should use to send/recv data
    enum class ctsTaskAction : std::uint8_t
    {
        None,
        Send,
        Recv,
        GracefulShutdown,
        HardShutdown,
        Abort,
        FatalAbort
    };

    struct ctsTask
    {
        int64_t m_timeOffsetMilliseconds = 0LL;
        RIO_BUFFERID m_rioBufferid = RIO_INVALID_BUFFERID;

        _Field_size_full_(m_bufferLength) char* m_buffer = nullptr;
        uint32_t m_bufferLength = 0UL;
        uint32_t m_bufferOffset = 0UL;
        uint32_t m_expectedPatternOffset = 0UL;
        ctsTaskAction m_ioAction = ctsTaskAction::None;

        // (internal) flag identifying the type of buffer
        enum class BufferType : std::uint8_t
        {
            Null,
            TcpConnectionId,
            UdpConnectionId,
            CompletionMessage,
            Static,
            Dynamic
        } m_bufferType = BufferType::Null;

        // (internal) flag if this IO request is tracked and verified
        bool m_trackIo = false;

        static PCWSTR PrintTaskAction(const ctsTaskAction& action) noexcept
        {
            switch (action)
            {
            case ctsTaskAction::None:
                return L"None";
            case ctsTaskAction::Send:
                return L"Send";
            case ctsTaskAction::Recv:
                return L"Recv";
            case ctsTaskAction::GracefulShutdown:
                return L"GracefulShutdown";
            case ctsTaskAction::HardShutdown:
                return L"HardShutdown";
            case ctsTaskAction::Abort:
                return L"Abort";
            case ctsTaskAction::FatalAbort:
                return L"FatalAbort";
            }

            return L"Unknown IOAction";
        }
    };
} // namespace
