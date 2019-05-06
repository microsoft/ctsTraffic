/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

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
#include "ctsSocketGuard.hpp"

namespace ctsTraffic {

	/// forward delcaration
	void ctsWSAPollEventThreadpool(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept;

	struct ctsSendRecvStatus
	{
		// Winsock error code
		unsigned long io_errorcode = NO_ERROR;
		// flag if to request another ctsIOTask
		bool io_done = false;
		// returns if IO was started (since can return !io_done, but I/O wasn't started yet)
		bool io_started = false;
	};

	///
	/// IO Threadpool completion callback 
	///
	static void ctsIoCompletionCallback(
		_In_ OVERLAPPED* _overlapped,
		const std::weak_ptr<ctsSocket>& _weak_socket,
		const ctsIOTask& _io_task) noexcept
	{
		auto shared_socket(_weak_socket.lock());
		if (!shared_socket) {
			return;
		}

		// hold a reference on the iopattern
		auto shared_pattern = shared_socket->io_pattern();

		// try to get the success/error code and bytes transferred (under the socket lock)
		int gle = NO_ERROR;
		DWORD transferred = 0;
		// scoping the socket lock
		{
			auto socketlock(ctsGuardSocket(shared_socket));
			const SOCKET socket = socketlock.get();
			// if we no longer have a valid socket or the pattern was destroyed, return early
			if (!shared_pattern || INVALID_SOCKET == socket) {
				gle = WSAECONNABORTED;
			}
			else {
				DWORD flags;
				if (!::WSAGetOverlappedResult(socket, _overlapped, &transferred, FALSE, &flags)) {
					gle = ::WSAGetLastError();
				}
			}
		}

		// write to PrintError if the IO failed
		const wchar_t* function = (IOTaskAction::Send == _io_task.ioAction) ? L"WSASend" : L"WSARecv";
		if (gle != 0) PrintDebugInfo(L"\t\tIO Failed: %ws (%d) [ctsSendRecvIocp]\n", function, gle);
		// see if complete_io requests more IO
		const ctsIOStatus protocol_status = shared_pattern->complete_io(_io_task, transferred, gle);
		switch (protocol_status) {
		case ctsIOStatus::ContinueIo:
			// more IO is requested from the protocol : invoke the new IO call while holding a refcount to the prior IO
			ctsSendRecvIocp(_weak_socket);
			break;

		case ctsIOStatus::CompletedIo:
			// no more IO is requested from the protocol : indicate success
			gle = NO_ERROR;
			break;

		case ctsIOStatus::FailedIo:
			// write out the error to the error log since the protocol sees this as a hard error
			ctsConfig::PrintErrorIfFailed(function, gle);
			// protocol sees this as a failure : capture the error the protocol recorded
			gle = shared_pattern->get_last_error();
			break;

		default:
			ctl::ctAlwaysFatalCondition(L"ctsSendRecvIocp : unknown ctsSocket::IOStatus (%u)", static_cast<unsigned>(protocol_status));
		}

		// always decrement *after* attempting new IO : the prior IO is now formally "done"
		if (shared_socket->decrement_io() == 0) {
			// if we have no more IO pended, complete the state
			shared_socket->complete_state(gle);
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	///
	/// Attempts the IO specified in the ctsIOTask on the ctsSocket
	///
	/// ** ctsSocket::increment_io must have been called before this function was invoked
	///
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	static ctsSendRecvStatus ctsProcessIOTask(SOCKET _socket, const std::shared_ptr<ctsSocket>& _shared_socket, const std::shared_ptr<ctsIOPattern>& _shared_pattern, const ctsIOTask& next_io) noexcept
	{
		ctsSendRecvStatus return_status;

		// if we no longer have a valid socket return early
		if (INVALID_SOCKET == _socket) {
			return_status.io_errorcode = WSAECONNABORTED;
			return_status.io_started = false;
			return_status.io_done = true;
			// even if the socket was closed we still must complete the IO request
			_shared_pattern->complete_io(next_io, 0, return_status.io_errorcode);
			return return_status;
		}

		if (IOTaskAction::GracefulShutdown == next_io.ioAction) {
			if (0 != ::shutdown(_socket, SD_SEND)) {
				return_status.io_errorcode = ::WSAGetLastError();
			}
			return_status.io_done = (_shared_pattern->complete_io(next_io, 0, return_status.io_errorcode) != ctsIOStatus::ContinueIo);
			return_status.io_started = false;

		}
		else if (IOTaskAction::HardShutdown == next_io.ioAction) {
			// pass through -1 to force an RST with the closesocket
			return_status.io_errorcode = _shared_socket->close_socket(-1);
			return_status.io_done = (_shared_pattern->complete_io(next_io, 0, return_status.io_errorcode) != ctsIOStatus::ContinueIo);
			return_status.io_started = false;

		}
		else
		{
			try 
			{
				OVERLAPPED* pov = nullptr;
				// attempt to allocate an IO thread-pool object
				const std::shared_ptr<ctl::ctThreadIocp>& io_thread_pool(_shared_socket->iocp_threadpool());
                if (!io_thread_pool)
                {
                    throw ctl::ctException(WSAECONNABORTED, L"ctsSocket::iocp_threadpool", false);
                }
                pov = io_thread_pool->new_request(
					[weak_reference = std::weak_ptr<ctsSocket>(_shared_socket), next_io](OVERLAPPED* _ov) noexcept
					{ ctsIoCompletionCallback(_ov, weak_reference, next_io); });

				WSABUF wsabuf;
				wsabuf.buf = next_io.buffer + next_io.buffer_offset;
				wsabuf.len = next_io.buffer_length;

				const wchar_t* function_name = nullptr;
				if (IOTaskAction::Send == next_io.ioAction) {
					function_name = L"WSASend";
					if (::WSASend(_socket, &wsabuf, 1, nullptr, 0, pov, nullptr) != 0) {
						return_status.io_errorcode = ::WSAGetLastError();
					}
				}
				else {
					function_name = L"WSARecv";
					DWORD flags = 0;
					if (::WSARecv(_socket, &wsabuf, 1, nullptr, &flags, pov, nullptr) != 0) {
						return_status.io_errorcode = ::WSAGetLastError();
					}
				}
				//
				// not calling complete_io if returned IO pended 
				// not calling complete_io if returned success but not handling inline completions
				//
				if ((WSA_IO_PENDING == return_status.io_errorcode) ||
					(NO_ERROR == return_status.io_errorcode && !(ctsConfig::Settings->Options & ctsConfig::OptionType::HANDLE_INLINE_IOCP))) {
					return_status.io_errorcode = NO_ERROR;
					return_status.io_started = true;
					return_status.io_done = false;

				}
				else {
					// process the completion if the API call failed, or if it succeeded and we're handling the completion inline, 
					return_status.io_started = false;
					// determine # of bytes transferred, if any
					DWORD bytes_transferred = 0;
					if (NO_ERROR == return_status.io_errorcode) {
						DWORD flags;
						if (!::WSAGetOverlappedResult(_socket, pov, &bytes_transferred, FALSE, &flags)) {
							ctl::ctAlwaysFatalCondition(
								L"WSAGetOverlappedResult failed (%d) after the IO request (%ws) succeeded", ::WSAGetLastError(), function_name);
						}
					}
					// must cancel the IOCP TP since IO is not pended
					io_thread_pool->cancel_request(pov);
					// call back to the socket to see if wants more IO
					const ctsIOStatus protocol_status = _shared_pattern->complete_io(next_io, bytes_transferred, return_status.io_errorcode);
					switch (protocol_status) {
					case ctsIOStatus::ContinueIo:
						// The protocol layer wants to transfer more data
						// if prior IO failed, the protocol wants to ignore the error
						return_status.io_errorcode = NO_ERROR;
						return_status.io_done = false;
						break;

					case ctsIOStatus::CompletedIo:
						// The protocol layer has successfully complete all IO on this connection
						// if prior IO failed, the protocol wants to ignore the error
						return_status.io_errorcode = NO_ERROR;
						return_status.io_done = true;
						break;

					case ctsIOStatus::FailedIo:
						// write out the error
						ctsConfig::PrintErrorIfFailed(function_name, _shared_pattern->get_last_error());
						// the protocol acknoledged the failure - socket is done with IO
						return_status.io_errorcode = _shared_pattern->get_last_error();
						return_status.io_done = true;
						break;

					default:
						ctl::ctAlwaysFatalCondition(L"ctsSendRecvIocp: unknown ctsSocket::IOStatus - %u\n", static_cast<unsigned>(protocol_status));
					}
				}
			}
			catch (const std::exception& e) {
				ctsConfig::PrintException(e);
				return_status.io_errorcode = ctl::ctErrorCode(e);
				return_status.io_done = (_shared_pattern->complete_io(next_io, 0, return_status.io_errorcode) != ctsIOStatus::ContinueIo);
				return_status.io_started = false;
			}
		}

		return return_status;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	///
	/// This is the callback for the threadpool timer.
	/// Processes the given task and then calls ctsSendRecvIocp function to deal with any additional tasks
	///
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	static void ctsProcessIOTaskCallback(const std::weak_ptr<ctsSocket>& _weak_socket, const ctsIOTask& next_io) noexcept
	{
		// attempt to get a reference to the socket
		auto shared_socket(_weak_socket.lock());
		if (!shared_socket) {
			return;
		}
		// take a lock on the socket before working with it
		auto socketlock(ctsGuardSocket(shared_socket));
		// increment IO for this IO request
		shared_socket->increment_io();

		// run the ctsIOTask (next_io) that was scheduled through the TP timer
		const ctsSendRecvStatus status = ctsProcessIOTask(socketlock.get(), shared_socket, shared_socket->io_pattern(), next_io);
		// if no IO was started, decrement the IO counter
		if (!status.io_started) {
			if (0 == shared_socket->decrement_io()) {
				// this should never be zero since we should be holding a refcount for this callback
				ctl::ctAlwaysFatalCondition(
					L"The refcount of the ctsSocket object (%p) fell to zero during a scheduled callback", shared_socket.get());
			}
		}
		// continue requesting IO if this connection still isn't done with all IO after scheduling the prior IO
		if (!status.io_done) {
			ctsWSAPoolEventThreadpool(_weak_socket);
		}
		// finally decrement the IO that was counted for this IO that was completed async
		if (shared_socket->decrement_io() == 0) {
			// if we have no more IO pended, complete the state
			shared_socket->complete_state(status.io_errorcode);
		}
	}

	///
	/// The function registered with ctsConfig
	///
	void ctsWSAPoolEventThreadpool(const std::weak_ptr<ctsSocket>& _weak_socket) noexcept
	{
		// attempt to get a reference to the socket
		auto shared_socket(_weak_socket.lock());
		if (!shared_socket) {
			return;
		}
		// take a lock on the socket before working with it
		auto socketlock(ctsGuardSocket(shared_socket));
		// hold a reference on the iopattern
		auto shared_pattern(shared_socket->io_pattern());
		//
		// loop until failure or initiate_io returns None
		//
		// IO is always done in the ctsProcessIOTask function,
		// - either synchronously or scheduled through a timer object
		//
		// The IO refcount must be incremented here to hold an IO count on the socket
		// - so that we won't inadvertently call complete_state() while IO is still being scheduled
		//
		shared_socket->increment_io();

		ctsSendRecvStatus status;
		while (!status.io_done) {
			const ctsIOTask next_io = shared_pattern->initiate_io();
			if (IOTaskAction::None == next_io.ioAction) {
				// nothing failed, just no more IO right now
				break;
			}

			// increment IO for each individual request
			shared_socket->increment_io();

			if (next_io.time_offset_milliseconds > 0) {
				// set_timer can throw
				try {
					shared_socket->set_timer(next_io, ctsProcessIOTaskCallback);
					status.io_started = true; // IO started in the context of keeping the count incremented
				}
				catch (const std::exception& e) {
					ctsConfig::PrintException(e);
					status.io_started = false;
					status.io_errorcode = ctl::ctErrorCode(e);
				}

			}
			else {
				status = ctsProcessIOTask(socketlock.get(), shared_socket, shared_pattern, next_io);
			}

			// if no IO was started, decrement the IO counter
			if (!status.io_started) {
				// since IO is not pended, remove the refcount
				if (0 == shared_socket->decrement_io()) {
					// this should never be zero as we are holding a reference outside the loop
					ctl::ctAlwaysFatalCondition(
						L"The ctsSocket (%p) refcount fell to zero while this function was holding a reference", shared_socket.get());
				}
			}
		}
		// decrement IO at the end to release the refcount held before the loop
		if (0 == shared_socket->decrement_io()) {
			shared_socket->complete_state(status.io_errorcode);
		}
	}

} // namespace
