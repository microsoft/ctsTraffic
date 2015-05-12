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
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"
#include "ctsMediaStreamProtocol.hpp"
// project headers
#include <ctVersionConversion.hpp>
#include <ctSockaddr.hpp>
#include <ctHandle.hpp>


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
        /// 
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void remove_socket(const ctl::ctSockaddr& _target_addr, unsigned long _error_code);
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Processes the incoming START request from the client
        /// - if we have a waiting ctsSocket to accept it, will add it to connected_sockets
        /// - else we'll queue it to awaiting_endpoints
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void start(const ctl::ctScopedSocket& _socket, const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _target_addr);
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// Process an incoming RESEND request
        ///
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void resend(const ctsMediaStreamMessage& _message, const ctl::ctSockaddr& _target_addr);
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called to 'accept' incoming connections
    /// - adds them to accepting_sockets
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamServerListener(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT
    {
        try {
            ctsMediaStreamServerImpl::init_once();
            // ctsMediaStreamServerImpl will complete the ctsSocket object
            // when a client request comes in to be 'accepted'
            ctsMediaStreamServerImpl::accept_socket(_weak_socket);
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            auto shared_socket(_weak_socket.lock());
            if (shared_socket) {
                shared_socket->complete_state(ERROR_OUTOFMEMORY);
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Called initiate IO on a datagram socket
    /// - the original ctsSocket is already in the connected_sockets vector
    /// - adding the next_io request to the IO queue
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamServerIo(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT
    {
        ctsIOTask next_task;
        try {
            ctsMediaStreamServerImpl::init_once();

            auto shared_socket(_weak_socket.lock());
            if (shared_socket) {
                // hold a reference on the iopattern
                auto shared_pattern(shared_socket->io_pattern());
                do {
                    next_task = shared_pattern->initiate_io();
                    if (next_task.ioAction != IOTaskAction::None) {
                        ctsMediaStreamServerImpl::schedule_io(_weak_socket, next_task);
                    }
                } while (next_task.ioAction != IOTaskAction::None);
            }
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            if (next_task.ioAction != IOTaskAction::None) {
                auto exception_shared_socket(_weak_socket.lock());
                if (exception_shared_socket) {
                    // hold a reference on the iopattern
                    auto exception_shared_pattern(exception_shared_socket->io_pattern());
                    // must complete any IO that was requested but not scheduled
                    exception_shared_pattern->complete_io(next_task, 0, WSAENOBUFS);
                    if (0 == exception_shared_socket->pended_io()) {
                        exception_shared_socket->complete_state(ERROR_OUTOFMEMORY);
                    }
                }
            }
        }
    }

} // ctsTraffic namespace
