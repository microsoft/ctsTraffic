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
#include <exception>
// os headers
#include <windows.h>
#include <winsock2.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctSockaddr.hpp>
#include <ctHandle.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"
#include "ctsMediaStreamProtocol.hpp"


///
/// We register both of these functions with ctsConfig:
/// - ctsMediaStreamServerListener is the "Accepting" function
///   - it will complete 'Create' ctsSocket requests as clients send in START requests
///     it will be assumed that a client is unique when its IP:PORT are unique
///
/// - ctsMediaStreamServerIo is the 'IO' function
///   - it queues up IO to a central prioritized queue of work
///     since all IO is triggered to occur at a future point, the queue is sorted by work that comes soonest
///


namespace ctsTraffic {
    namespace ctsMediaStreamServerImpl {
        void init_once();

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Schedule the first IO on the specified ctsSocket
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void schedule_io(const std::weak_ptr<ctsSocket>& _weak_socket, const ctsIOTask& _task);
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Process a new ctsSocket from the ctsSocketBroker
        /// - accept_socket takes the ctsSocket to create a new entry
        ///   which will create a corresponding ctsMediaStreamServerConnectedSocket in the process
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void accept_socket(const std::weak_ptr<ctsSocket>& _weak_socket);
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Process the removal of a connected socket once it is completed
        /// - remove_socket takes the remote address to find the socket
        /// - cannot be called from a TP callback from ctsMediaStreamServerConnectedSocket
        ///   as remove_socket will deadlock as it tries to delete the ctsMediaStreamServerConnectedSocket instance
        ///   (which will wait for all TP threads to complete in the d'tor)
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void remove_socket(const ctl::ctSockaddr& _target_addr);
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Processes the incoming START request from the client
        /// - if we have a waiting ctsSocket to accept it, will add it to connected_sockets
        /// - else we'll queue it to awaiting_endpoints
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void start(const ctl::ctScopedSocket& _socket, const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _target_addr);
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called to 'accept' incoming connections
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamServerListener(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called initiate IO on a datagram socket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamServerIo(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called to remove that socket from the tracked vector of connected sockets
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamServerClose(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT;
} // ctsTraffic namespace
