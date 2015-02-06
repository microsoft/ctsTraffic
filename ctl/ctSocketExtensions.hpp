/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

#include <Windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <rpc.h> // for GUID

#include "ctException.hpp"
#include "ctScopeGuard.hpp"

namespace ctl {
    ///
    /// anonymous namespace to correctly hide this function to only this file
    /// - and thus avoid any ODR or name collision issues
    ///
    namespace {
        ///
        /// function pointers accessable from only this file (anonymous namespace)
        ///
        const unsigned fn_ptr_count = 9;
        LPFN_TRANSMITFILE            transmitfile = NULL; // WSAID_TRANSMITFILE
        LPFN_ACCEPTEX                acceptex = NULL; // WSAID_ACCEPTEX
        LPFN_GETACCEPTEXSOCKADDRS    getacceptexsockaddrs = NULL; // WSAID_GETACCEPTEXSOCKADDRS
        LPFN_TRANSMITPACKETS         transmitpackets = NULL; // WSAID_TRANSMITPACKETS
        LPFN_CONNECTEX               connectex = NULL; // WSAID_CONNECTEX
        LPFN_DISCONNECTEX            disconnectex = NULL; // WSAID_DISCONNECTEX
        LPFN_WSARECVMSG              wsarecvmsg = NULL; // WSAID_WSARECVMSG
        LPFN_WSASENDMSG              wsasendmsg = NULL; // WSAID_WSASENDMSG
        RIO_EXTENSION_FUNCTION_TABLE rioextensionfunctiontable = { 0 }; // WSAID_MULTIPLE_RIO

        ///
        /// ctSocketExtensionInit
        ///
        /// InitOnce function only to be called locally to ensure WSAStartup is held
        /// for the function pointers to remain accurate
        ///
        static INIT_ONCE s_ctSocketExtensionInitOnce = INIT_ONCE_STATIC_INIT;
        static BOOL CALLBACK s_ctSocketExtensionInitFn(_In_ PINIT_ONCE, _In_ PVOID perror, _In_ PVOID*)
        {
            WSADATA wsadata;
            int wsError = ::WSAStartup(WINSOCK_VERSION, &wsadata);
            if (wsError != 0) {
                *static_cast<int*>(perror) = wsError;
                return FALSE;
            }
            ctlScopeGuard(WSACleanupOnExit, { ::WSACleanup(); });

            // check to see if need to create a temp socket
            SOCKET local_socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            if (INVALID_SOCKET == local_socket) {
                *static_cast<int*>(perror) = ::WSAGetLastError();
                return FALSE;
            }
            ctlScopeGuard(closesocketOnExit, { ::closesocket(local_socket); });

            // control code and the size to fetch the extension function pointers
            for (unsigned fn_loop = 0; fn_loop < fn_ptr_count; ++fn_loop) {

                VOID*  function_ptr = NULL;
                DWORD controlCode = SIO_GET_EXTENSION_FUNCTION_POINTER;
                DWORD bytes = static_cast<DWORD>(sizeof(VOID*));
                // must declare GUID explicitly at a global scope as some commonly used test libraries
                // - incorrectly pull it into their own namespace
                ::GUID guid = { 0 };

                switch (fn_loop) {
                    case 0: {
                        function_ptr = reinterpret_cast<VOID*>(&transmitfile);
                        ::GUID tmp_guid = WSAID_TRANSMITFILE;
                        ::memcpy(&guid, &tmp_guid, sizeof ::GUID);
                        break;
                    }
                    case 1: {
                        function_ptr = reinterpret_cast<VOID*>(&acceptex);
                        ::GUID tmp_guid = WSAID_ACCEPTEX;
                        ::memcpy(&guid, &tmp_guid, sizeof ::GUID);
                        break;
                    }
                    case 2: {
                        function_ptr = reinterpret_cast<VOID*>(&getacceptexsockaddrs);
                        ::GUID tmp_guid = WSAID_GETACCEPTEXSOCKADDRS;
                        ::memcpy(&guid, &tmp_guid, sizeof ::GUID);
                        break;
                    }
                    case 3: {
                        function_ptr = reinterpret_cast<VOID*>(&transmitpackets);
                        ::GUID tmp_guid = WSAID_TRANSMITPACKETS;
                        ::memcpy(&guid, &tmp_guid, sizeof ::GUID);
                        break;
                    }
                    case 4: {
                        function_ptr = reinterpret_cast<VOID*>(&connectex);
                        ::GUID tmp_guid = WSAID_CONNECTEX;
                        ::memcpy(&guid, &tmp_guid, sizeof ::GUID);
                        break;
                    }
                    case 5: {
                        function_ptr = reinterpret_cast<VOID*>(&disconnectex);
                        ::GUID tmp_guid = WSAID_DISCONNECTEX;
                        ::memcpy(&guid, &tmp_guid, sizeof ::GUID);
                        break;
                    }
                    case 6: {
                        function_ptr = reinterpret_cast<VOID*>(&wsarecvmsg);
                        ::GUID tmp_guid = WSAID_WSARECVMSG;
                        ::memcpy(&guid, &tmp_guid, sizeof ::GUID);
                        break;
                    }
                    case 7: {
                        function_ptr = reinterpret_cast<VOID*>(&wsasendmsg);
                        ::GUID tmp_guid = WSAID_WSASENDMSG;
                        ::memcpy(&guid, &tmp_guid, sizeof ::GUID);
                        break;
                    }
                    case 8: {
                        function_ptr = reinterpret_cast<VOID*>(&rioextensionfunctiontable);
                        ::GUID tmp_guid = WSAID_MULTIPLE_RIO;
                        ::memcpy(&guid, &tmp_guid, sizeof ::GUID);
                        controlCode = SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER;
                        bytes = static_cast<DWORD>(sizeof(rioextensionfunctiontable));
                        ::ZeroMemory(&rioextensionfunctiontable, bytes);
                        rioextensionfunctiontable.cbSize = bytes;
                        break;
                    }
                }

                if (0 != ::WSAIoctl(
                    local_socket,
                    controlCode,
                    &guid,
                    static_cast<DWORD>(sizeof(guid)),
                    function_ptr,
                    bytes,
                    &bytes,
                    NULL, // lpOverlapped
                    NULL  // lpCompletionRoutine
                    )) 
                {
                    DWORD errorCode = ::WSAGetLastError();
                    if (8 == fn_loop && errorCode == WSAEOPNOTSUPP) {
                        // ignore not-supported errors for RIO APIs to support Win7
                    } else {
                        *static_cast<int*>(perror) = errorCode;
                        return FALSE;
                    }
                }
            }

            return TRUE;
        }
        static void s_InitSocketExtensions()
        {
            DWORD error = 0;
            if (!::InitOnceExecuteOnce(&s_ctSocketExtensionInitOnce, s_ctSocketExtensionInitFn, &error, nullptr)) {
                throw ctl::ctException(error, L"ctl::ctSocketExtensions", false);
            }
        }
    }; // anonymous namespace

    ///
    /// Dynamic check if RIO is available on this operating system
    ///
    inline bool ctSocketIsRioAvailable()
    {
        s_InitSocketExtensions();
        return (nullptr != rioextensionfunctiontable.RIOReceive);
    }

    ///
    /// TransmitFile
    ///
    inline BOOL ctTransmitFile(
        _In_ SOCKET hSocket,
        _In_ HANDLE hFile,
        _In_ DWORD nNumberOfBytesToWrite,
        _In_ DWORD nNumberOfBytesPerSend,
        _Inout_opt_ LPOVERLAPPED lpOverlapped,
        _In_opt_ LPTRANSMIT_FILE_BUFFERS lpTransmitBuffers,
        _In_  DWORD dwReserved
        )
    {
        s_InitSocketExtensions();
        return transmitfile(
            hSocket,
            hFile,
            nNumberOfBytesToWrite,
            nNumberOfBytesPerSend,
            lpOverlapped,
            lpTransmitBuffers,
            dwReserved
            );
    }

    ///
    /// TransmitPackets
    ///
    inline BOOL ctTransmitPackets(
        _In_ SOCKET hSocket,
        _In_opt_ LPTRANSMIT_PACKETS_ELEMENT lpPacketArray,
        _In_ DWORD nElementCount,
        _In_ DWORD nSendSize,
        _Inout_opt_ LPOVERLAPPED lpOverlapped,
        _In_ DWORD dwFlags
        )
    {
        s_InitSocketExtensions();
        return transmitpackets(
            hSocket,
            lpPacketArray,
            nElementCount,
            nSendSize,
            lpOverlapped,
            dwFlags
            );
    }

    ///
    /// AcceptEx
    ///
    inline BOOL ctAcceptEx(
        _In_ SOCKET sListenSocket,
        _In_ SOCKET sAcceptSocket,
        _Out_writes_bytes_(dwReceiveDataLength + dwLocalAddressLength + dwRemoteAddressLength) PVOID lpOutputBuffer,
        _In_ DWORD dwReceiveDataLength,
        _In_ DWORD dwLocalAddressLength,
        _In_ DWORD dwRemoteAddressLength,
        _Out_ LPDWORD lpdwBytesReceived,
        _Inout_ LPOVERLAPPED lpOverlapped
        )
    {
        s_InitSocketExtensions();
        return acceptex(
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

    ///
    /// GetAcceptExSockaddrs
    ///
    inline VOID ctGetAcceptExSockaddrs(
        _In_reads_bytes_(dwReceiveDataLength + dwLocalAddressLength + dwRemoteAddressLength) PVOID lpOutputBuffer,
        _In_ DWORD dwReceiveDataLength,
        _In_ DWORD dwLocalAddressLength,
        _In_ DWORD dwRemoteAddressLength,
        _Outptr_result_bytebuffer_(*LocalSockaddrLength) struct sockaddr **LocalSockaddr,
        _Out_ LPINT LocalSockaddrLength,
        _Outptr_result_bytebuffer_(*RemoteSockaddrLength) struct sockaddr **RemoteSockaddr,
        _Out_ LPINT RemoteSockaddrLength
        )
    {
        s_InitSocketExtensions();
        return getacceptexsockaddrs(
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

    ///
    /// ConnectEx
    ///
    inline BOOL ctConnectEx(
        _In_ SOCKET s,
        _In_reads_bytes_(namelen) const struct sockaddr FAR *name,
        _In_ int namelen,
        _In_reads_bytes_opt_(dwSendDataLength) PVOID lpSendBuffer,
        _In_ DWORD dwSendDataLength,
        _When_(lpSendBuffer, _Out_) LPDWORD lpdwBytesSent, // optional if lpSendBuffer is null
        _Inout_ LPOVERLAPPED lpOverlapped
        )
    {
        s_InitSocketExtensions();
        return connectex(
            s,
            name,
            namelen,
            lpSendBuffer,
            dwSendDataLength,
            lpdwBytesSent,
            lpOverlapped
            );
    }

    ///
    /// DisconnectEx
    ///
    inline BOOL ctDisconnectEx(
        _In_ SOCKET s,
        _Inout_opt_ LPOVERLAPPED lpOverlapped,
        _In_ DWORD  dwFlags,
        _In_ DWORD  dwReserved
        )
    {
        s_InitSocketExtensions();
        return disconnectex(
            s,
            lpOverlapped,
            dwFlags,
            dwReserved
            );
    }

    ///
    /// WSARecvMsg
    ///
    inline INT ctWSARecvMsg(
        _In_ SOCKET s,
        _Inout_ LPWSAMSG lpMsg,
        _Out_opt_ LPDWORD lpdwNumberOfBytesRecvd,
        _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
        _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        s_InitSocketExtensions();
        return wsarecvmsg(
            s,
            lpMsg,
            lpdwNumberOfBytesRecvd,
            lpOverlapped,
            lpCompletionRoutine
            );
    }

    ///
    /// WSASendMsg
    ///
    inline INT ctWSASendMsg(
        _In_ SOCKET s,
        _In_ LPWSAMSG lpMsg,
        _In_ DWORD dwFlags,
        _Out_opt_ LPDWORD lpNumberOfBytesSent,
        _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
        _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        s_InitSocketExtensions();
        return wsasendmsg(
            s,
            lpMsg,
            dwFlags,
            lpNumberOfBytesSent,
            lpOverlapped,
            lpCompletionRoutine
            );
    }

    ///
    /// RioReceive 
    ///
    inline BOOL ctRIOReceive(
        _In_ RIO_RQ socketQueue,
        _In_reads_(dataBufferCount) PRIO_BUF pData,
        _In_ ULONG dataBufferCount,
        _In_ DWORD dwFlags,
        _In_ PVOID requestContext
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIOReceive(
            socketQueue,
            pData,
            dataBufferCount,
            dwFlags,
            requestContext
            );
    }

    ///
    /// RioReceiveEx 
    ///
    inline int ctRIOReceiveEx(
        _In_ RIO_RQ socketQueue,
        _In_reads_(dataBufferCount) PRIO_BUF pData,
        _In_ ULONG dataBufferCount,
        _In_opt_ PRIO_BUF pLocalAddress,
        _In_opt_ PRIO_BUF pRemoteAddress,
        _In_opt_ PRIO_BUF pControlContext,
        _In_opt_ PRIO_BUF pdwFlags,
        _In_ DWORD dwFlags,
        _In_ PVOID requestContext
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIOReceiveEx(
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

    ///
    /// RioSend
    ///
    inline BOOL ctRIOSend(
        _In_ RIO_RQ socketQueue,
        _In_reads_(dataBufferCount) PRIO_BUF pData,
        _In_ ULONG dataBufferCount,
        _In_ DWORD dwFlags,
        _In_ PVOID requestContext
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIOSend(
            socketQueue,
            pData,
            dataBufferCount,
            dwFlags,
            requestContext
            );
    }

    ///
    /// RioSendEx
    ///
    inline BOOL ctRIOSendEx(
        _In_ RIO_RQ socketQueue,
        _In_reads_(dataBufferCount) PRIO_BUF pData,
        _In_ ULONG dataBufferCount,
        _In_opt_ PRIO_BUF pLocalAddress,
        _In_opt_ PRIO_BUF pRemoteAddress,
        _In_opt_ PRIO_BUF pControlContext,
        _In_opt_ PRIO_BUF pdwFlags,
        _In_ DWORD dwFlags,
        _In_ PVOID requestContext
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIOSendEx(
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

    ///
    /// RioCloseCompletionQueue
    ///
    inline void ctRIOCloseCompletionQueue(
        _In_ RIO_CQ cq
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIOCloseCompletionQueue(
            cq
            );
    }

    ///
    /// RioCreateCompletionQueue
    ///
    inline RIO_CQ ctRIOCreateCompletionQueue(
        _In_ DWORD queueSize,
        _In_opt_ PRIO_NOTIFICATION_COMPLETION pNotificationCompletion
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIOCreateCompletionQueue(
            queueSize,
            pNotificationCompletion
            );
    }

    ///
    /// RioCreateRequestQueue
    ///
    inline RIO_RQ ctRIOCreateRequestQueue(
        _In_ SOCKET socket,
        _In_ ULONG maxOutstandingReceive,
        _In_ ULONG maxReceiveDataBuffers,
        _In_ ULONG maxOutstandingSend,
        _In_ ULONG maxSendDataBuffers,
        _In_ RIO_CQ receiveCq,
        _In_ RIO_CQ sendCq,
        _In_ PVOID socketContext
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIOCreateRequestQueue(
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

    ///
    /// RioDequeueCompletion
    ///
    inline ULONG ctRIODequeueCompletion(
        _In_ RIO_CQ cq,
        _Out_writes_to_(arraySize, return) PRIORESULT array,
        _In_ ULONG arraySize
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIODequeueCompletion(
            cq,
            array,
            arraySize
            );
    }

    ///
    /// RioDeregisterBuffer
    ///
    inline void ctRIODeregisterBuffer(
        _In_ RIO_BUFFERID bufferId
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIODeregisterBuffer(
            bufferId
            );
    }

    ///
    /// RioNotify
    ///
    inline int ctRIONotify(
        _In_ RIO_CQ cq
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIONotify(
            cq
            );
    }

    ///
    /// RioRegisterBuffer 
    ///
    inline RIO_BUFFERID ctRIORegisterBuffer(
        _In_ PCHAR dataBuffer,
        _In_ DWORD dataLength
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIORegisterBuffer(
            dataBuffer,
            dataLength
            );
    }

    ///
    /// RioResizeCompletionQueue
    ///
    inline BOOL ctRIOResizeCompletionQueue(
        _In_ RIO_CQ cq,
        _In_ DWORD queueSize
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIOResizeCompletionQueue(
            cq,
            queueSize
            );
    }

    ///
    /// RioResizeRequestQueue
    ///
    inline BOOL ctRIOResizeRequestQueue(
        _In_ RIO_RQ rq,
        _In_ DWORD maxOutstandingReceive,
        _In_ DWORD maxOutstandingSend
        )
    {
        s_InitSocketExtensions();
        return rioextensionfunctiontable.RIOResizeRequestQueue(
            rq,
            maxOutstandingReceive,
            maxOutstandingSend
            );
    }

} // namespace ctl

