/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include <Windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <rpc.h> // for GUID
// wil headers
#include <wil/resource.h>
// ctl headers
#include "ctException.hpp"


namespace ctl
{
	namespace details
	{
        static const unsigned fn_ptr_count = 9;
        static LPFN_TRANSMITFILE transmitfile = nullptr; // WSAID_TRANSMITFILE
        static LPFN_ACCEPTEX acceptex = nullptr; // WSAID_ACCEPTEX
        static LPFN_GETACCEPTEXSOCKADDRS getacceptexsockaddrs = nullptr; // WSAID_GETACCEPTEXSOCKADDRS
        static LPFN_TRANSMITPACKETS transmitpackets = nullptr; // WSAID_TRANSMITPACKETS
        static LPFN_CONNECTEX connectex = nullptr; // WSAID_CONNECTEX
        static LPFN_DISCONNECTEX disconnectex = nullptr; // WSAID_DISCONNECTEX
        static LPFN_WSARECVMSG wsarecvmsg = nullptr; // WSAID_WSARECVMSG
        static LPFN_WSASENDMSG wsasendmsg = nullptr; // WSAID_WSASENDMSG
		static RIO_EXTENSION_FUNCTION_TABLE rioextensionfunctiontable; // WSAID_MULTIPLE_RIO

		//
		// ctSocketExtensionInit
		//
		// InitOnce function only to be called locally to ensure WSAStartup is held
		// for the function pointers to remain accurate
		//
		static BOOL CALLBACK s_ctSocketExtensionInitFn(_In_ PINIT_ONCE, _In_ PVOID, _In_ PVOID*) noexcept
		{
			WSADATA wsadata;
			const auto wsError = ::WSAStartup(WINSOCK_VERSION, &wsadata);
			if (wsError != 0)
			{
				return FALSE;
			}
			auto WSACleanupOnExit = wil::scope_exit([&]() { ::WSACleanup(); });

			// check to see if need to create a temp socket
			SOCKET local_socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
			if (INVALID_SOCKET == local_socket)
			{
				return FALSE;
			}
			auto closesocketOnExit = wil::scope_exit([&]() { ::closesocket(local_socket); });

			// control code and the size to fetch the extension function pointers
			for (unsigned fn_loop = 0; fn_loop < fn_ptr_count; ++fn_loop)
			{
				VOID* function_ptr = nullptr;
				DWORD controlCode = SIO_GET_EXTENSION_FUNCTION_POINTER;
				auto bytes = static_cast<DWORD>(sizeof(VOID*));
				// must declare GUID explicitly at a global scope as some commonly used test libraries
				// - incorrectly pull it into their own namespace
				GUID guid{};

				switch (fn_loop) {
					case 0: {
						function_ptr = reinterpret_cast<VOID*>(&transmitfile);
						GUID tmp_guid = WSAID_TRANSMITFILE;
						memcpy(&guid, &tmp_guid, sizeof GUID);
						break;
					}
					case 1: {
						function_ptr = reinterpret_cast<VOID*>(&acceptex);
						GUID tmp_guid = WSAID_ACCEPTEX;
						memcpy(&guid, &tmp_guid, sizeof GUID);
						break;
					}
					case 2: {
						function_ptr = reinterpret_cast<VOID*>(&getacceptexsockaddrs);
						GUID tmp_guid = WSAID_GETACCEPTEXSOCKADDRS;
						memcpy(&guid, &tmp_guid, sizeof GUID);
						break;
					}
					case 3: {
						function_ptr = reinterpret_cast<VOID*>(&transmitpackets);
						GUID tmp_guid = WSAID_TRANSMITPACKETS;
						memcpy(&guid, &tmp_guid, sizeof GUID);
						break;
					}
					case 4: {
						function_ptr = reinterpret_cast<VOID*>(&connectex);
						GUID tmp_guid = WSAID_CONNECTEX;
						memcpy(&guid, &tmp_guid, sizeof GUID);
						break;
					}
					case 5: {
						function_ptr = reinterpret_cast<VOID*>(&disconnectex);
						GUID tmp_guid = WSAID_DISCONNECTEX;
						memcpy(&guid, &tmp_guid, sizeof GUID);
						break;
					}
					case 6: {
						function_ptr = reinterpret_cast<VOID*>(&wsarecvmsg);
						GUID tmp_guid = WSAID_WSARECVMSG;
						memcpy(&guid, &tmp_guid, sizeof GUID);
						break;
					}
					case 7: {
						function_ptr = reinterpret_cast<VOID*>(&wsasendmsg);
						GUID tmp_guid = WSAID_WSASENDMSG;
						memcpy(&guid, &tmp_guid, sizeof GUID);
						break;
					}
					case 8: {
						function_ptr = reinterpret_cast<VOID*>(&rioextensionfunctiontable);
						GUID tmp_guid = WSAID_MULTIPLE_RIO;
						memcpy(&guid, &tmp_guid, sizeof GUID);
						controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
						bytes = static_cast<DWORD>(sizeof rioextensionfunctiontable);
						::ZeroMemory(&rioextensionfunctiontable, bytes);
						rioextensionfunctiontable.cbSize = bytes;
						break;
					}
					default:
						ctAlwaysFatalCondition(L"Unknown ctSocketExtension function number");
				}

				if (0 != ::WSAIoctl(
					local_socket,
					controlCode,
					&guid,
					static_cast<DWORD>(sizeof guid),
					function_ptr,
					bytes,
					&bytes,
					nullptr, // lpOverlapped
					nullptr))  // lpCompletionRoutine
				{
					const auto errorCode = ::WSAGetLastError();
					if (8 == fn_loop && errorCode == WSAEOPNOTSUPP) {
						// ignore not-supported errors for RIO APIs to support Win7
					} else {
						return FALSE;
					}
				}
			}

			return TRUE;
		}

		static void s_InitSocketExtensions()
		{
            // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
            static INIT_ONCE s_ctSocketExtensionInitOnce = INIT_ONCE_STATIC_INIT;
            FAIL_FAST_IF(!::InitOnceExecuteOnce(&s_ctSocketExtensionInitOnce, s_ctSocketExtensionInitFn, nullptr, nullptr));
		}
	}; // anonymous namespace

	//
	// Dynamic check if RIO is available on this operating system
	//
	inline bool ctSocketIsRioAvailable() noexcept
	{
		details::s_InitSocketExtensions();
		return nullptr != details::rioextensionfunctiontable.RIOReceive;
	}

	//
	// TransmitFile
	//
	inline BOOL ctTransmitFile(
		_In_ SOCKET hSocket,
		_In_ HANDLE hFile,
		_In_ DWORD nNumberOfBytesToWrite,
		_In_ DWORD nNumberOfBytesPerSend,
		_Inout_opt_ LPOVERLAPPED lpOverlapped,
		_In_opt_ LPTRANSMIT_FILE_BUFFERS lpTransmitBuffers,
		_In_  DWORD dwReserved) noexcept
	{
        details::s_InitSocketExtensions();
		return details::transmitfile(
			hSocket,
			hFile,
			nNumberOfBytesToWrite,
			nNumberOfBytesPerSend,
			lpOverlapped,
			lpTransmitBuffers,
			dwReserved);
	}

	//
	// TransmitPackets
	//
	inline BOOL ctTransmitPackets(
		_In_ SOCKET hSocket,
		_In_opt_ LPTRANSMIT_PACKETS_ELEMENT lpPacketArray,
		_In_ DWORD nElementCount,
		_In_ DWORD nSendSize,
		_Inout_opt_ LPOVERLAPPED lpOverlapped,
		_In_ DWORD dwFlags) noexcept
	{
		details::s_InitSocketExtensions();
		return details::transmitpackets(
			hSocket,
			lpPacketArray,
			nElementCount,
			nSendSize,
			lpOverlapped,
			dwFlags
		);
	}

	//
	// AcceptEx
	//
	inline BOOL ctAcceptEx(
		_In_ SOCKET sListenSocket,
		_In_ SOCKET sAcceptSocket,
		_Out_writes_bytes_(dwReceiveDataLength + dwLocalAddressLength + dwRemoteAddressLength) PVOID lpOutputBuffer,
		_In_ DWORD dwReceiveDataLength,
		_In_ DWORD dwLocalAddressLength,
		_In_ DWORD dwRemoteAddressLength,
		_Out_ LPDWORD lpdwBytesReceived,
		_Inout_ LPOVERLAPPED lpOverlapped) noexcept
	{
		details::s_InitSocketExtensions();
		return details::acceptex(
			sListenSocket,
			sAcceptSocket,
			lpOutputBuffer,
			dwReceiveDataLength,
			dwLocalAddressLength,
			dwRemoteAddressLength,
			lpdwBytesReceived,
			lpOverlapped
		);
	}

	//
	// GetAcceptExSockaddrs
	//
	inline VOID ctGetAcceptExSockaddrs(
		_In_reads_bytes_(dwReceiveDataLength + dwLocalAddressLength + dwRemoteAddressLength) PVOID lpOutputBuffer,
		_In_ DWORD dwReceiveDataLength,
		_In_ DWORD dwLocalAddressLength,
		_In_ DWORD dwRemoteAddressLength,
		_Outptr_result_bytebuffer_(*LocalSockaddrLength) struct sockaddr **LocalSockaddr,
		_Out_ LPINT LocalSockaddrLength,
		_Outptr_result_bytebuffer_(*RemoteSockaddrLength) struct sockaddr **RemoteSockaddr,
		_Out_ LPINT RemoteSockaddrLength) noexcept
	{
		details::s_InitSocketExtensions();
		return details::getacceptexsockaddrs(
			lpOutputBuffer,
			dwReceiveDataLength,
			dwLocalAddressLength,
			dwRemoteAddressLength,
			LocalSockaddr,
			LocalSockaddrLength,
			RemoteSockaddr,
			RemoteSockaddrLength
		);
	}

	//
	// ConnectEx
	//
	inline BOOL ctConnectEx(
		_In_ SOCKET s,
		_In_reads_bytes_(namelen) const struct sockaddr FAR *name,
		_In_ int namelen,
		_In_reads_bytes_opt_(dwSendDataLength) PVOID lpSendBuffer,
		_In_ DWORD dwSendDataLength,
		_When_(lpSendBuffer, _Out_) LPDWORD lpdwBytesSent, // optional if lpSendBuffer is null
		_Inout_ LPOVERLAPPED lpOverlapped) noexcept
	{
		details::s_InitSocketExtensions();
		return details::connectex(
			s,
			name,
			namelen,
			lpSendBuffer,
			dwSendDataLength,
			lpdwBytesSent,
			lpOverlapped
		);
	}

	//
	// DisconnectEx
	//
	inline BOOL ctDisconnectEx(
		_In_ SOCKET s,
		_Inout_opt_ LPOVERLAPPED lpOverlapped,
		_In_ DWORD  dwFlags,
		_In_ DWORD  dwReserved) noexcept
	{
		details::s_InitSocketExtensions();
		return details::disconnectex(
			s,
			lpOverlapped,
			dwFlags,
			dwReserved
		);
	}

	//
	// WSARecvMsg
	//
	inline INT ctWSARecvMsg(
		_In_ SOCKET s,
		_Inout_ LPWSAMSG lpMsg,
		_Out_opt_ LPDWORD lpdwNumberOfBytesRecvd,
		_Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
		_In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) noexcept
	{
		details::s_InitSocketExtensions();
		return details::wsarecvmsg(
			s,
			lpMsg,
			lpdwNumberOfBytesRecvd,
			lpOverlapped,
			lpCompletionRoutine
		);
	}

	//
	// WSASendMsg
	//
	inline INT ctWSASendMsg(
		_In_ SOCKET s,
		_In_ LPWSAMSG lpMsg,
		_In_ DWORD dwFlags,
		_Out_opt_ LPDWORD lpNumberOfBytesSent,
		_Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
		_In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) noexcept
	{
		details::s_InitSocketExtensions();
		return details::wsasendmsg(
			s,
			lpMsg,
			dwFlags,
			lpNumberOfBytesSent,
			lpOverlapped,
			lpCompletionRoutine
		);
	}

	//
	// RioReceive 
	//
	inline BOOL ctRIOReceive(
		_In_ RIO_RQ socketQueue,
		_In_reads_(dataBufferCount) PRIO_BUF pData,
		_In_ ULONG dataBufferCount,
		_In_ DWORD dwFlags,
		_In_ PVOID requestContext) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIOReceive(
			socketQueue,
			pData,
			dataBufferCount,
			dwFlags,
			requestContext
		);
	}

	//
	// RioReceiveEx 
	//
	inline int ctRIOReceiveEx(
		_In_ RIO_RQ socketQueue,
		_In_reads_(dataBufferCount) PRIO_BUF pData,
		_In_ ULONG dataBufferCount,
		_In_opt_ PRIO_BUF pLocalAddress,
		_In_opt_ PRIO_BUF pRemoteAddress,
		_In_opt_ PRIO_BUF pControlContext,
		_In_opt_ PRIO_BUF pdwFlags,
		_In_ DWORD dwFlags,
		_In_ PVOID requestContext) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIOReceiveEx(
			socketQueue,
			pData,
			dataBufferCount,
			pLocalAddress,
			pRemoteAddress,
			pControlContext,
			pdwFlags,
			dwFlags,
			requestContext
		);
	}

	//
	// RioSend
	//
	inline BOOL ctRIOSend(
		_In_ RIO_RQ socketQueue,
		_In_reads_(dataBufferCount) PRIO_BUF pData,
		_In_ ULONG dataBufferCount,
		_In_ DWORD dwFlags,
		_In_ PVOID requestContext) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIOSend(
			socketQueue,
			pData,
			dataBufferCount,
			dwFlags,
			requestContext
		);
	}

	//
	// RioSendEx
	//
	inline BOOL ctRIOSendEx(
		_In_ RIO_RQ socketQueue,
		_In_reads_(dataBufferCount) PRIO_BUF pData,
		_In_ ULONG dataBufferCount,
		_In_opt_ PRIO_BUF pLocalAddress,
		_In_opt_ PRIO_BUF pRemoteAddress,
		_In_opt_ PRIO_BUF pControlContext,
		_In_opt_ PRIO_BUF pdwFlags,
		_In_ DWORD dwFlags,
		_In_ PVOID requestContext) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIOSendEx(
			socketQueue,
			pData,
			dataBufferCount,
			pLocalAddress,
			pRemoteAddress,
			pControlContext,
			pdwFlags,
			dwFlags,
			requestContext
		);
	}

	//
	// RioCloseCompletionQueue
	//
	inline void ctRIOCloseCompletionQueue(
		_In_ RIO_CQ cq) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIOCloseCompletionQueue(
			cq
		);
	}

	//
	// RioCreateCompletionQueue
	//
	inline RIO_CQ ctRIOCreateCompletionQueue(
		_In_ DWORD queueSize,
		_In_opt_ PRIO_NOTIFICATION_COMPLETION pNotificationCompletion) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIOCreateCompletionQueue(
			queueSize,
			pNotificationCompletion
		);
	}

	//
	// RioCreateRequestQueue
	//
	inline RIO_RQ ctRIOCreateRequestQueue(
		_In_ SOCKET socket,
		_In_ ULONG maxOutstandingReceive,
		_In_ ULONG maxReceiveDataBuffers,
		_In_ ULONG maxOutstandingSend,
		_In_ ULONG maxSendDataBuffers,
		_In_ RIO_CQ receiveCq,
		_In_ RIO_CQ sendCq,
		_In_ PVOID socketContext) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIOCreateRequestQueue(
			socket,
			maxOutstandingReceive,
			maxReceiveDataBuffers,
			maxOutstandingSend,
			maxSendDataBuffers,
			receiveCq,
			sendCq,
			socketContext
		);
	}

	//
	// RioDequeueCompletion
	//
	inline ULONG ctRIODequeueCompletion(
		_In_ RIO_CQ cq,
		_Out_writes_to_(arraySize, return) PRIORESULT array,
		_In_ ULONG arraySize) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIODequeueCompletion(
			cq,
			array,
			arraySize
		);
	}

	//
	// RioDeregisterBuffer
	//
	inline void ctRIODeregisterBuffer(
		_In_ RIO_BUFFERID bufferId) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIODeregisterBuffer(
			bufferId
		);
	}

	//
	// RioNotify
	//
	inline int ctRIONotify(
		_In_ RIO_CQ cq) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIONotify(
			cq
		);
	}

	//
	// RioRegisterBuffer 
	//
	inline RIO_BUFFERID ctRIORegisterBuffer(
		_In_ PCHAR dataBuffer,
		_In_ DWORD dataLength) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIORegisterBuffer(
			dataBuffer,
			dataLength
		);
	}

	//
	// RioResizeCompletionQueue
	//
	inline BOOL ctRIOResizeCompletionQueue(
		_In_ RIO_CQ cq,
		_In_ DWORD queueSize) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIOResizeCompletionQueue(
			cq,
			queueSize
		);
	}

	//
	// RioResizeRequestQueue
	//
	inline BOOL ctRIOResizeRequestQueue(
		_In_ RIO_RQ rq,
		_In_ DWORD maxOutstandingReceive,
		_In_ DWORD maxOutstandingSend) noexcept
	{
		details::s_InitSocketExtensions();
		return details::rioextensionfunctiontable.RIOResizeRequestQueue(
			rq,
			maxOutstandingReceive,
			maxOutstandingSend
		);
	}
} // namespace ctl
