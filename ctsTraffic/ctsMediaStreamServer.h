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
// using wil::networking to pull in all necessary networking headers
#include <wil/networking.h>
// project headers
#include "ctsSocket.h"
#include "ctsIOTask.hpp"

// We register both of these functions with ctsConfig:
// - ctsMediaStreamServerListener is the "Accepting" function
//   - it will complete 'Create' ctsSocket requests as clients send in START requests
//     it will be assumed that a client is unique when its IP:PORT are unique
//
// - ctsMediaStreamServerIo is the 'IO' function
//   - it queues up IO to a central prioritized queue of work
//     since all IO is triggered to occur at a future point, the queue is sorted by work that comes soonest

namespace ctsTraffic
{
    namespace ctsMediaStreamServerImpl
    {
        void InitOnce();

        // Schedule the first IO on the specified ctsSocket
        void ScheduleIo(const std::weak_ptr<ctsSocket>& weakSocket, const ctsTask& task);

        // Process a new ctsSocket from the ctsSocketBroker
        // - accept_socket takes the ctsSocket to create a new entry
        //   which will create a corresponding ctsMediaStreamServerConnectedSocket in the process
        void AcceptSocket(const std::weak_ptr<ctsSocket>& weakSocket);

        // Process the removal of a connected socket once it is completed
        // - remove_socket takes the remote address to find the socket
        // - cannot be called from a TP callback from ctsMediaStreamServerConnectedSocket
        //   as remove_socket will deadlock as it tries to delete the ctsMediaStreamServerConnectedSocket instance
        //   (which will wait for all TP threads to complete in the destructor)
        void RemoveSocket(const socket_address& targetAddr);

        // Processes the incoming START request from the client
        // - if we have a waiting ctsSocket to accept it, will add it to connected_sockets
        // - else we'll queue it to awaiting_endpoints
        void Start(SOCKET socket, const socket_address& localAddr, const socket_address& targetAddr);
    }

    // Called to 'accept' incoming connections
    void ctsMediaStreamServerListener(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;

    // Called initiate IO on a datagram socket
    void ctsMediaStreamServerIo(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;

    // Called to remove that socket from the tracked vector of connected sockets
    void ctsMediaStreamServerClose(const std::weak_ptr<ctsSocket>& weakSocket) noexcept;
}
