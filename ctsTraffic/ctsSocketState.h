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
// ctl headers
#include <ctVersionConversion.hpp>

namespace ctsTraffic {
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
    class ctsSocketState : public std::enable_shared_from_this<ctsSocketState> {
    public:
        enum class InternalState
        {
            Creating,
            Created,
            Connecting,
            Connected,
            InitiatingIO,
            InitiatedIO,
            Closing,
            Closed
        };

        //
        // c'tor requiring a parent ctsSocketBroker
        //
        explicit ctsSocketState(std::weak_ptr<ctsSocketBroker> _broker);

        ~ctsSocketState() NOEXCEPT;

        //
        // explicit method to 'start' the state machine
        // - this is required to ensure the object is fully instatiated before
        //   it is passed to the threadpool thread
        //
        void start() NOEXCEPT;

        //
        // Completes the current socket state
        //
        void complete_state(DWORD _error) NOEXCEPT;

        //
        // Accessor to current state information
        //
        InternalState current_state() const NOEXCEPT;

        //
        // copy c'tor and assignment
        //
        ctsSocketState(const ctsSocketState&) = delete;
        ctsSocketState& operator=(const ctsSocketState&) = delete;
        ctsSocketState(ctsSocketState&&) = delete;
        ctsSocketState& operator=(ctsSocketState&&) = delete;

    private:
        //
        // private members of ctsSocketState
        // - CS's are mutable to allow taking a CS in a const function
        //
        PTP_WORK                       thread_pool_worker = nullptr;
        mutable CRITICAL_SECTION       state_guard{};
        std::weak_ptr<ctsSocketBroker> broker{};
        std::shared_ptr<ctsSocket>     socket{};
        unsigned long                  last_error = 0UL;
        InternalState                  state = InternalState::Creating;
        bool                           initiated_io = false;

        //
        // static threadpool callback function
        //
        static
        VOID NTAPI ThreadPoolWorker(PTP_CALLBACK_INSTANCE /*_instance*/, PVOID _context, PTP_WORK /*_work*/) NOEXCEPT;
    };

} // namespace
