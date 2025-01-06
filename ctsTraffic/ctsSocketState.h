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
// os headers
#include <Windows.h>
// project headers
#include "ctsConfig.h"
// wil headers
#include <wil/resource.h>

namespace ctsTraffic
{
    //
    // forward declare ctsSocketBroker
    // - can't include ctsSocketBroker.h in this header to avoid circular declarations
    //
    class ctsSocketBroker;

    //
    // forward declare ctsSocket
    // - can't include ctsSocket.h in this header to avoid circular declarations
    //
    class ctsSocket;

    //
    // ctsSocketState class
    //
    // Encapsulates a ctsSocket instance
    // - tracking socket state and corresponding statistics
    //
    class ctsSocketState : public std::enable_shared_from_this<ctsSocketState>
    {
    public:
        enum class InternalState : std::uint8_t
        {
            Creating,
            Created,
            Connecting,
            Connected,
            InitiatingIo,
            InitiatedIo,
            Closing,
            Closed
        };

        // constructor requires a parent ctsSocketBroker
        explicit ctsSocketState(std::weak_ptr<ctsSocketBroker> pBroker);

        ~ctsSocketState() noexcept;

        //
        // explicit method to 'start' the state machine
        // - this is required to ensure the object is fully instantiated before
        //   it is passed to the threadpool thread
        //
        void Start() noexcept;

        //
        // Completes the current socket state
        //
        void CompleteState(DWORD error) noexcept;

        //
        // Accessor to current state information
        //
        InternalState GetCurrentState() const noexcept;

        ctsSocketState(const ctsSocketState&) = delete;
        ctsSocketState& operator=(const ctsSocketState&) = delete;
        ctsSocketState(ctsSocketState&&) = delete;
        ctsSocketState& operator=(ctsSocketState&&) = delete;

    private:
        //
        // private members of ctsSocketState
        // - CS's are mutable to allow taking a CS in a const function
        //
        wil::unique_threadpool_work m_threadPoolWorker;
        mutable wil::critical_section m_stateGuard{ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock};
        std::weak_ptr<ctsSocketBroker> m_broker{};
        std::shared_ptr<ctsSocket> m_socket{};
        InternalState m_state = InternalState::Creating;
        uint32_t m_lastError = 0UL;
        bool m_initiatedIo = false;

        //
        // static threadpool callback function
        //
        static VOID NTAPI ThreadPoolWorker(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK) noexcept;
    };
} // namespace
