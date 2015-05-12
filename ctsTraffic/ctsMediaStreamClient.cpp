#include "ctsMediaStreamClient.h"

#include "ctsWinsockLayer.h"


namespace ctsTraffic {

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// implementation of processing a ctsIOTask
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    IoImplStatus ctsMediaStreamClientIoImpl(
        _In_ std::shared_ptr<ctsSocket>& _shared_socket,
        _In_ const ctsIOTask& _next_io) NOEXCEPT
    {
        IoImplStatus return_status;

        switch (_next_io.ioAction) {
            case IOTaskAction::None:
                // nothing failed, just no more IO right now
                return_status.error_code = NO_ERROR;
                return_status.continue_io = false;
                break;

            case IOTaskAction::Abort: {
                // the protocol signaled to immediately abort
                auto shared_pattern(_shared_socket->io_pattern());
                shared_pattern->complete_io(_next_io, 0, 0);
                _shared_socket->close_socket();

                return_status.error_code = NO_ERROR;
                return_status.continue_io = false;
                break;
            }

            case IOTaskAction::FatalAbort: {
                // the protocol indicated to rudely abort the connection
                auto shared_pattern(_shared_socket->io_pattern());
                shared_pattern->complete_io(_next_io, 0, 0);
                _shared_socket->close_socket();

                return_status.error_code = shared_pattern->get_last_error();
                return_status.continue_io = false;
                break;
            }

            case IOTaskAction::Send: // fall-through
            case IOTaskAction::Recv: {
                // add-ref the IO about to start
                LONG io_count = _shared_socket->increment_io();

                std::weak_ptr<ctsSocket> weak_reference(_shared_socket);
                auto callback = [weak_reference, _next_io] (OVERLAPPED* _ov) {
                    ctsMediaStreamClientIoCompletionCallback(_ov, weak_reference, _next_io);
                };

                LPCWSTR function_name = nullptr;
                wsIOResult result;
                if (IOTaskAction::Send == _next_io.ioAction) {
                    function_name = L"WSASendTo";
                    result = ctsWSASendTo(_shared_socket, _next_io, callback);
                } else if (IOTaskAction::Recv == _next_io.ioAction) {
                    function_name = L"WSARecvFrom";
                    result = ctsWSARecvFrom(_shared_socket, _next_io, callback);
                }

                if (WSA_IO_PENDING == result.error_code) {
                    // if successful but did not complete inline
                    return_status.error_code = result.error_code;
                    return_status.continue_io = true;

                } else {
                    // IO successfully completed inline and the async completion won't be invoke
                    // - or the IO failed

                    // hold a reference on the iopattern
                    auto shared_pattern(_shared_socket->io_pattern());
                    ctsIOStatus protocol_status = shared_pattern->complete_io(
                        _next_io,
                        result.bytes_transferred,
                        result.error_code);

                    switch (protocol_status) {
                        case ctsIOStatus::ContinueIo:
                            // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                            ctsConfig::PrintDebugIfFailed(function_name, result.error_code, L"ctsMediaStreamClient");
                            // the protocol wants to ignore the error and send more data
                            return_status.error_code = NO_ERROR;
                            return_status.continue_io = true;
                            break;

                        case ctsIOStatus::CompletedIo:
                            // write to PrintDebug if the IO failed - only debug since the protocol ignored the error
                            ctsConfig::PrintDebugIfFailed(function_name, result.error_code, L"ctsMediaStreamClient");
                            // the protocol wants to ignore the error but is done with IO
                            _shared_socket->close_socket();
                            return_status.error_code = NO_ERROR;
                            return_status.continue_io = false;
                            break;

                        case ctsIOStatus::FailedIo:
                            // write out the error
                            ctsConfig::PrintErrorIfFailed(function_name, result.error_code);
                            // the protocol acknoledged the failure - socket is done with IO
                            _shared_socket->close_socket();
                            return_status.error_code = shared_pattern->get_last_error();
                            return_status.continue_io = false;
                            break;

                        default:
                            ctl::ctAlwaysFatalCondition(L"ctsMediaStreamClientIoImpl: unknown ctsSocket::IOStatus - %u\n", protocol_status);
                    }

                    // decrement the IO count if failed and/or inlined-completed
                    io_count = _shared_socket->decrement_io();
                    // IO count should never be zero: callers should be guaranteeing a refcount before calling Impl
                    ctl::ctFatalCondition(
                        0 == io_count,
                        L"ctsMediaStreamClient : ctsSocket::io_count fell to zero while the Impl function was called (dt %p ctsTraffic::ctsSocket)",
                        _shared_socket.get());
                }
            }
        }

        return return_status;
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// IO Threadpool completion callback 
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClientIoCompletionCallback(
        OVERLAPPED* _overlapped,
        std::weak_ptr<ctsSocket> _weak_socket,
        ctsIOTask _io_task
        ) NOEXCEPT
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        int gle = NO_ERROR;
        DWORD transferred = 0;
        // scope to the socket lock
        {
            auto socket_lock(ctsSocket::LockSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (socket != INVALID_SOCKET) {
                DWORD flags;
                if (!::WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags)) {
                    gle = ::WSAGetLastError();
                }
            } else {
                // we're intentionally ignoring the error when we have closed it early
                // - doing this because that's how we shutdown the client after processing all frames
                gle = NO_ERROR;
            }
        }

        // hold a reference on the iopattern
        auto shared_pattern(shared_socket->io_pattern());
        // see if complete_io requests more IO
        ctsIOStatus protocol_status = shared_pattern->complete_io(_io_task, transferred, gle);
        switch (protocol_status) {
            case ctsIOStatus::ContinueIo: {
                // more IO is requested from the protocol
                IoImplStatus status;
                do {
                    // invoke the new IO call while holding a refcount to the prior IO in a tight loop
                    status = ctsMediaStreamClientIoImpl(shared_socket, shared_pattern->initiate_io());
                } while (status.continue_io);

                gle = status.error_code;
                break;
            }

            case ctsIOStatus::CompletedIo:
                shared_socket->close_socket();
                gle = NO_ERROR;
                break;

            case ctsIOStatus::FailedIo:
                if (gle != 0) {
                    // the failure may have been a protocol error - in which case gle would just be NO_ERROR
                    ctsConfig::PrintErrorInfo(
                        L"ctsMediaStreamClientIoCompletionCallback IO failed (%s) with error %d\n",
                        (_io_task.ioAction == IOTaskAction::Recv) ? L"WSARecvFrom" : L"WSASendTo",
                        gle);
                }
                shared_socket->close_socket();
                gle = shared_pattern->get_last_error();
                break;

            default:
                ctl::ctAlwaysFatalCondition(
                    L"ctsMediaStreamClientIoCompletionCallback: unknown ctsSocket::IOStatus - %u\n", 
                    protocol_status);
        }

        // always decrement *after* attempting new IO - the prior IO is now formally "done"
        if (shared_socket->decrement_io() == 0) {
            // if we have no more IO pended, complete the state
            shared_socket->complete_state(gle);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// IO Threadpool completion callback for the 'connect' request
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void ctsMediaStreamClientConnectionCompletionCallback(
        OVERLAPPED* _overlapped,
        std::weak_ptr<ctsSocket> _weak_socket,
        ctl::ctSockaddr _target_address
        ) NOEXCEPT
    {
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        int gle = NO_ERROR;
        DWORD transferred = 0;
        // scope to the socket lock
        {
            auto socket_lock(ctsSocket::LockSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (INVALID_SOCKET == socket) {
                gle = WSAECONNABORTED;
            } else {
                DWORD flags;
                if (!::WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags)) {
                    gle = ::WSAGetLastError();
                }
            }

            ctsConfig::PrintErrorIfFailed(L"\tWSASendTo (START request)", gle);

            if (NO_ERROR == gle) {
                // set the local and remote addr's
                ctl::ctSockaddr local_addr;
                int local_addr_len = local_addr.length();
                if (0 == ::getsockname(socket, local_addr.sockaddr(), &local_addr_len)) {
                    shared_socket->set_local_address(local_addr);
                }
                shared_socket->set_target_address(_target_address);
                ctsConfig::PrintNewConnection(local_addr, _target_address);
            }
        }

        shared_socket->complete_state(gle);
    }

} // namespace