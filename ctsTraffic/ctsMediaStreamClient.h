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
#include <winsock2.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctThreadIocp.hpp>
#include <ctSockaddr.hpp>
// local headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"
#include "ctsWinsockLayer.h"

#include "ctsMediaStreamProtocol.hpp"


namespace ctsTraffic {

    struct IoImplStatus {
        unsigned long error_code;
        bool continue_io;

        IoImplStatus() NOEXCEPT : error_code(0), continue_io(false)
        {
        }
    };

    IoImplStatus ctsMediaStreamClientIoImpl(
        _In_ std::shared_ptr<ctsSocket>& _shared_socket, 
        _In_ const ctsIOTask& _next_io) NOEXCEPT;

    void ctsMediaStreamClientIoCompletionCallback(
        OVERLAPPED* _overlapped,
        std::weak_ptr<ctsSocket> _weak_socket,
        ctsIOTask _io_task
        ) NOEXCEPT;

    void ctsMediaStreamClientConnectionCompletionCallback(
        OVERLAPPED* _overlapped,
        std::weak_ptr<ctsSocket> _weak_socket,
        ctl::ctSockaddr _target_address
        ) NOEXCEPT;


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to run Winsock IO using IO Completion Ports
    /// - with the specified ctsSocket
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamClient(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }
        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());

        // always register our ctsIOPattern callback since it's necessary for this IO Pattern
        // this callback can be invoked out-of-band directly from the IO Pattern class
        shared_pattern->register_callback(
            [_weak_socket] (const ctsIOTask& _task) {
            // attempt to get a reference to the socket
            auto lambda_shared_socket(_weak_socket.lock());
            if (!lambda_shared_socket) {
                return;
            }

            //
            // the below check with increment_io avoids a possible race-condition: 
            // - if increment_io() returns 1, it means our IO count in the main loop
            //   hit an io_count of 0 : which means that main thread will be completing this socket
            // - if this OOB callback ever returns 1, we cannot use this socket, since this socket
            //   will either be completed soon, or will have already been completed
            //
            // this special scenario exists because the callback doesn't hold a ref-count
            // - so this callback could be invoked after the mainline completed
            // this is still 'safe' due to the above socket locks
            //

            // increment IO count while issuing this Impl so we hold a ref-count during this out of band callback
            if (lambda_shared_socket->increment_io() > 1) {
                // only running this one task in the OOB callback
                IoImplStatus status = ctsMediaStreamClientIoImpl(lambda_shared_socket, _task);
                // decrement the IO count that we added before calling the Impl
                // - complete_state if this happened to be the final IO refcount
                if (lambda_shared_socket->decrement_io() == 0) {
                    lambda_shared_socket->complete_state(status.error_code);
                }
            } else {
                // in this case, the io_count in the ctsSocket was zero, so no IO was in flight to interrupt
                // just decrement the IO count that we added before calling the Impl (no IO attempted)
                lambda_shared_socket->decrement_io();
            }
        });

        // increment IO count while issuing this Impl so we hold a ref-count during this out of band callback
        shared_socket->increment_io();
        IoImplStatus status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
        while (status.continue_io) {
            // invoke the new IO call while holding a refcount to the prior IO in a tight loop
            status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
        }
        if (0 == shared_socket->decrement_io()) {
            shared_socket->complete_state(status.error_code);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The function that is registered with ctsTraffic to 'connect' to the target server by sending a START command
    /// using IO Completion Ports
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline
    void ctsMediaStreamClientConnect(std::weak_ptr<ctsSocket> _weak_socket) NOEXCEPT
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        // scope to lock
        {
            auto socket_lock(ctsSocket::LockSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (INVALID_SOCKET == socket) {
                shared_socket->complete_state(WSAECONNABORTED);
                return;
            }

            auto error = ctsConfig::SetPreConnectOptions(socket);
            ctsConfig::PrintErrorIfFailed(L"SetPreConnectOptions", error);
            if (error != NO_ERROR) {
                shared_socket->complete_state(error);
                return;
            }
        }

        ctl::ctSockaddr targetAddress(shared_socket->target_address());
        ctsIOTask start_task = ctsMediaStreamMessage::Construct(MediaStreamAction::START);

        // Not add-ref'ing the IO on the socket since this is a single send() simulating connect()
        auto response = ctsWSASendTo(
            shared_socket, 
            start_task, 
            [_weak_socket, targetAddress] (OVERLAPPED* ov) {
                ctsMediaStreamClientConnectionCompletionCallback(ov, _weak_socket, targetAddress);
            });

        if (NO_ERROR == response.error_code) {
            auto socket_lock(ctsSocket::LockSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (INVALID_SOCKET == socket) {
                shared_socket->complete_state(WSAECONNABORTED);
                return;
            }

            // set the local and remote addresses on the socket object
            ctl::ctSockaddr local_addr;
            int local_addr_len = local_addr.length();
            if (0 == ::getsockname(socket, local_addr.sockaddr(), &local_addr_len)) {
                shared_socket->set_local_address(local_addr);
            }
            shared_socket->set_target_address(targetAddress);
            
            ctsConfig::PrintNewConnection(local_addr, targetAddress);

            try {
                ctsConfig::PrintDebug(
                    L"\t\tctsMediaStreamClient sent its START message to %s\n",
                    targetAddress.writeCompleteAddress().c_str());
            }
            catch (const std::exception&) {
                // best-effort
            }
        }

        // complete only on failure or successfully completed inline (otherwise will complete in the IOCP callback)
        if (response.error_code != WSA_IO_PENDING) {
            shared_socket->complete_state(response.error_code);
        }
    }
} // namespace
