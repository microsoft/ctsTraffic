/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// cpp headers
#include <memory>
#include <vector>
// os headers
#include <Windows.h>
#include <winsock2.h>
#include <mswsock.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctSocketExtensions.hpp>
#include <ctSockaddr.hpp>
#include <ctScopeGuard.hpp>
#include <ctLocks.hpp>
// local headers
#include "ctsConfig.h"
#include "ctsSocket.h"
#include "ctsIOTask.hpp"
#include "ctsSocketGuard.hpp"

namespace ctsTraffic {

    ///
    /// constants for everything related to ctsRioIocp
    ///
    static const LONG RioResultArrayLength = 20;
    static const LONG RioRQGrowthFactor = 2;
    static const LONG RioMaxDataBuffers = 1; // this is the only value accepted as of Win8
    static const LONG RioDefaultCQSize = 1000;
    static const ULONG_PTR ExitCompletionKey = 0xffffffff;

    ///
    /// forward-declaring CQ-functions leveraging the below variables
    ///
    static void  s_make_room_in_cq(ULONG _new_slots);
    static void  s_release_room_in_cq(ULONG _slots) NOEXCEPT;
    static ULONG s_deque_from_cq(_Out_writes_(RioResultArrayLength) RIORESULT* _rio_results) NOEXCEPT;
    static void  s_delete_all_cqs() NOEXCEPT;
    ///
    /// Forward-declaring the IOCP threadpool function
    ///
    static DWORD WINAPI RioIocpThreadProc(LPVOID) NOEXCEPT;

    ///
    /// Management of the CQ and its corresponding threadpool implemented in this unnamed namespace
    /// - initialized with InitOneExecuteOnce
    /// 
    static BOOL CALLBACK s_init_once_cq(PINIT_ONCE, PVOID, PVOID *) NOEXCEPT;
    static INIT_ONCE s_sharedbuffer_initializer = INIT_ONCE_STATIC_INIT;

    static ctl::ctPrioritizedCriticalSection* s_prioritized_cs = nullptr;
    static RIO_NOTIFICATION_COMPLETION s_rio_notify_setttings;
    static RIO_CQ  s_rio_cq = RIO_INVALID_CQ;
    static ULONG   s_rio_cq_size = 0;
    static ULONG   s_rio_cq_used = 0;
    static HANDLE* s_rio_worker_threads = nullptr;
    static DWORD   s_rio_worker_thread_count = 0;


    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Make room in the CQ for a new IO
    ///
    /// - check if there is room in the CQ for the new IO
    ///   - if not, take a writer lock around the CS to halt readers and to write to cq_used
    ///     then take the CS over the CQ, and resize the CQ by 1.5 times current size
    ///
    /// - can throw under low resources or failure to resize
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static void s_make_room_in_cq(ULONG _new_slots)
    {
        // taking an priority lock to interrupt the general lock taken by the deque IO path
        // - we want to interrupt the IO path so we can initiate more IO if we need to grow the CQ
        ctl::ctAutoReleasePriorityCriticalSection priority_lock_on_static_cs(*s_prioritized_cs);

        ULONG new_cq_used = s_rio_cq_used + _new_slots;
        ULONG new_cq_size = s_rio_cq_size; // not yet resized
        if (s_rio_cq_size < new_cq_used) {
            // fail hard if we are already at the max CQ size and can't grow it for more IO
            ctl::ctFatalCondition(
                ((RIO_MAX_CQ_SIZE == s_rio_cq_size) || (new_cq_used > RIO_MAX_CQ_SIZE)),
                L"ctsRioIocp: attempting to grow the CQ beyond RIO_MAX_CQ_SIZE");

            // multiply new_cq_used by 1.25 for bettery growth patterns
            new_cq_size = static_cast<ULONG>(new_cq_used * 1.5);
            if (new_cq_size > RIO_MAX_CQ_SIZE) {
                static_assert(MAXLONG / 1.5 > RIO_MAX_CQ_SIZE, "s_rio_cq_size can overflow");
                new_cq_size = RIO_MAX_CQ_SIZE;
            }
            PrintDebugInfo(
                L"\t\tctsRioIocp: Resizing the CQ from %u to %u (used slots = %u increasing used slots to %u)\n",
                s_rio_cq_size,
                new_cq_size,
                s_rio_cq_used,
                new_cq_used);

            if (!ctl::ctRIOResizeCompletionQueue(s_rio_cq, new_cq_size)) {
                throw ctl::ctException(::WSAGetLastError(), L"ctRIOResizeCompletionQueue", L"ctsRioIocp", false);
            }
        } else {
            PrintDebugInfo(
                L"\t\tctsRioIocp: Not resizing the CQ from %u (used slots = %u increasing to %u)\n",
                s_rio_cq_size, s_rio_cq_used, new_cq_used);
        }
        // update cq_used and cq_size on the success path
        s_rio_cq_used = new_cq_used;
        s_rio_cq_size = new_cq_size;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Release slots in the CQ
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static void s_release_room_in_cq(ULONG _slots) NOEXCEPT
    {
        ctl::ctAutoReleasePriorityCriticalSection priority_lock_on_static_cs(*s_prioritized_cs);

        ctl::ctFatalCondition(
            s_rio_cq_used < _slots,
            L"ctsRioIocp::s_release_room_in_cq(%u): underflow - current s_rio_cq_used value (%u)",
            _slots, s_rio_cq_used);

        PrintDebugInfo(
            L"\t\tctsRioIocp: Reducing the CQ used slots from %u to %u\n",
            s_rio_cq_used,
            s_rio_cq_used - _slots);

        s_rio_cq_used -= _slots;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Safely dequeus from the CQ into the supplied RIORESULT vector
    /// - will always post a Notify with proper synchronization
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static ULONG s_deque_from_cq(_Out_writes_(RioResultArrayLength) RIORESULT* _rio_results) NOEXCEPT
    {
        // taking a lower-priority lock, to allow the priority lock to interrupt dequeing
        // - so it can add space to the CQ
        ctl::ctAutoReleaseDefaultCriticalSection default_lock_on_cs(*s_prioritized_cs);

        ULONG deque_result = ctl::ctRIODequeueCompletion(s_rio_cq, _rio_results, RioResultArrayLength);
        // We were notified there were completions, but we can't dequeue any IO
        // - something has gone horribly wrong - likely our CQ is corrupt
        // Will kill the test into the debugger to investigate
        ctl::ctFatalCondition(
            ((0 == deque_result) || (RIO_CORRUPT_CQ == deque_result)),
            L"ctRIODequeueCompletion on(%p) returned [%u] : expected to have dequeued IO after being signaled",
            s_rio_cq, deque_result);
        //
        // Immediately after invoking Dequeue, post another Notify
        //
        int notify_result = ctl::ctRIONotify(s_rio_cq);
        // if notify fails, we can't reliably know when the next IO completes
        // - this will cause everything to come to a grinding halt
        // Will kill the test into the debugger to investigate
        ctl::ctFatalCondition(
            (notify_result != 0),
            L"RIONotify(%p) failed [%d]", s_rio_cq, notify_result);

        return deque_result;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Shutdown all IOCP threads and close the CQ
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static void s_delete_all_cqs() NOEXCEPT
    {
        unsigned threads_alive = 0;
        // send an exit key to all threads, then wait on all threads to exit
        for (unsigned loop_workers = 0; loop_workers < s_rio_worker_thread_count; ++loop_workers) {
            // queue an exit key to the worker thread
            if (s_rio_worker_threads[loop_workers] != nullptr) {
                ++threads_alive;
                if (!PostQueuedCompletionStatus(
                    s_rio_notify_setttings.Iocp.IocpHandle,
                    0,
                    ExitCompletionKey,
                    reinterpret_cast<OVERLAPPED*>(s_rio_notify_setttings.Iocp.Overlapped))) {
                    // if can't indicate to exit, kill the process to see why
                    ctl::ctAlwaysFatalCondition(
                        L"PostQueuedCompletionStatus(%p) failed [%u] to tear down the threadpool",
                        s_rio_notify_setttings.Iocp.IocpHandle, ::GetLastError());
                }
            }
        }
        // wait for threads to exit
        if (threads_alive > 0) {
            if (::WaitForMultipleObjects(
                threads_alive,
                &s_rio_worker_threads[0],
                TRUE,
                INFINITE) != WAIT_OBJECT_0) {
                // if can't wait for the worker threads, kill the process to see why
                ctl::ctAlwaysFatalCondition(
                    L"WaitForMultipleObjects(%p) failed [%u] to wait on the threadpool",
                    &s_rio_worker_threads[0], ::GetLastError());
            }
        }
        // now can close the thread handles
        for (unsigned loop_workers = 0; loop_workers < s_rio_worker_thread_count; ++loop_workers) {
            if (s_rio_worker_threads[loop_workers] != nullptr) {
                ::CloseHandle(s_rio_worker_threads[loop_workers]);
            }
        }

        ::free(s_rio_worker_threads);
        s_rio_worker_threads = nullptr;
        s_rio_worker_thread_count = 0;

        if (s_rio_cq != RIO_INVALID_CQ) {
            ctl::ctRIOCloseCompletionQueue(s_rio_cq);
            s_rio_cq = RIO_INVALID_CQ;
        }

        if (s_rio_notify_setttings.Iocp.IocpHandle != NULL) {
            ::CloseHandle(s_rio_notify_setttings.Iocp.IocpHandle);
            s_rio_notify_setttings.Iocp.IocpHandle = NULL;
        }

        ::free(s_rio_notify_setttings.Iocp.Overlapped);
        s_rio_notify_setttings.Iocp.Overlapped = nullptr;

        delete s_prioritized_cs;
        s_prioritized_cs = nullptr;

        s_rio_cq_size = 0;
        s_rio_cq_used = 0;
    }


    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Singleton initialization routine for the global CQ and its corresponding IOCP thread pool
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static BOOL CALLBACK s_init_once_cq(PINIT_ONCE, PVOID, PVOID *) NOEXCEPT
    {
        // delete all cq's on error
        ctlScopeGuard(deleteAllCqsOnError, { s_delete_all_cqs(); });

        s_prioritized_cs = new (std::nothrow) ctl::ctPrioritizedCriticalSection;
        if (nullptr == s_prioritized_cs) {
            return FALSE;
        }
        // delete the prioritized cs on error
        ctlScopeGuard(deletePrioritzedCsOnError, { delete s_prioritized_cs; });

        ::ZeroMemory(&s_rio_notify_setttings, sizeof s_rio_notify_setttings);
        // completion key for RioNotify IOCP is the ctsRioIocpImpl*
        s_rio_notify_setttings.Type = RIO_NOTIFICATION_COMPLETION_TYPE::RIO_IOCP_COMPLETION;
        s_rio_notify_setttings.Iocp.CompletionKey = nullptr;
        s_rio_notify_setttings.Iocp.Overlapped = nullptr;
        s_rio_notify_setttings.Iocp.IocpHandle = NULL;

        s_rio_notify_setttings.Iocp.Overlapped = ::calloc(1, sizeof OVERLAPPED);
        if (nullptr == s_rio_notify_setttings.Iocp.Overlapped) {
            ctsConfig::PrintException(std::bad_alloc());
            ::SetLastError(WSAENOBUFS);
            return FALSE;
        }
        // free the OVERLAPPED on error
        ctlScopeGuard(freeOverlappedOnError, { ::free(s_rio_notify_setttings.Iocp.Overlapped); });

        s_rio_notify_setttings.Iocp.IocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (NULL == s_rio_notify_setttings.Iocp.IocpHandle) {
            DWORD gle = ::GetLastError();
            ctsConfig::PrintException(ctl::ctException(gle, L"CreateIoCompletionPort", L"ctsRioIocp", false));
            ::SetLastError(gle);
            return FALSE;
        }
        // close the IOCP handle on error
        ctlScopeGuard(deleteIoCPOnError, { ::CloseHandle(s_rio_notify_setttings.Iocp.IocpHandle); });

        // with RIO, we don't associate the IOCP handle with the socket like 'typical' sockets
        // - instead we directly pass the IOCP handle through RIOCreateCompletionQueue
        DWORD new_queue_size = RioDefaultCQSize;
        if (!ctsConfig::IsListening()) {
            // for clients we'll know the CQ size since we know the concurrent connection count
            new_queue_size = ctsConfig::Settings->ConnectionLimit * 2;
        }
        s_rio_cq = ctl::ctRIOCreateCompletionQueue(new_queue_size, &s_rio_notify_setttings);
        if (RIO_INVALID_CQ == s_rio_cq) {
            DWORD gle = ::WSAGetLastError();
            ctsConfig::PrintException(ctl::ctException(gle, L"ctRIOCreateCompletionQueue", L"ctsRioIocp", false));
            ::SetLastError(gle);
            return FALSE;
        }
        // close the RIO CQ on error
        ctlScopeGuard(closeCQOnError, { ctl::ctRIOCloseCompletionQueue(s_rio_cq); });

        // now that the CQ is created, update info
        s_rio_cq_size = new_queue_size;
        s_rio_cq_used = 0;

        // reserve space for handles
        SYSTEM_INFO system_info;
        ::GetSystemInfo(&system_info);
        s_rio_worker_threads = reinterpret_cast <HANDLE*>(::calloc(system_info.dwNumberOfProcessors, sizeof HANDLE));
        if (nullptr == s_rio_worker_threads) {
            ctsConfig::PrintException(std::bad_alloc());
            ::SetLastError(WSAENOBUFS);
            return FALSE;
        }
        // free the handle array on error
        ctlScopeGuard(freeHandleArrayOnError, { ::free(s_rio_worker_threads); });

        s_rio_worker_thread_count = system_info.dwNumberOfProcessors;

        // now that we are ready to go, kick off our thread-pool
        for (unsigned loop_workers = 0; loop_workers < s_rio_worker_thread_count; ++loop_workers) {
            s_rio_worker_threads[loop_workers] = ::CreateThread(nullptr, 0, RioIocpThreadProc, nullptr, 0, nullptr);
            if (NULL == s_rio_worker_threads[loop_workers]) {
                DWORD gle = ::GetLastError();
                ctsConfig::PrintException(ctl::ctException(gle, L"CreateThread", L"ctsRioIocp", false));
                ::SetLastError(gle);
                return FALSE;
            }
        }
        // scopedDeleteAllCqs will take care of cleaning up these threads on failure

        // if everything succeeds, post a Notify to catch the first set of IO
        int notify = ctl::ctRIONotify(s_rio_cq);
        if (notify != NO_ERROR) {
            ctsConfig::PrintException(ctl::ctException(notify, L"ctRIONotify", L"ctsRioIocp", false));
            ::SetLastError(notify);
            return FALSE;
        }

        // dismiss all scope guards - successfully initialized
        freeHandleArrayOnError.dismiss();
        closeCQOnError.dismiss();
        deleteIoCPOnError.dismiss();
        freeOverlappedOnError.dismiss();
        deletePrioritzedCsOnError.dismiss();
        deleteAllCqsOnError.dismiss();
        return TRUE;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// RioSocketContext
    ///
    /// This ptr is passed through RIO APIs as the SOCKET context for that IO operation
    /// 
    /// This stores all relevant information with regards to the RIO SOCKET
    /// Including encapsulating the RIO_RQ associated with the socket
    /// 
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class RioSocketContext {
    private:
        std::weak_ptr<ctsSocket> weak_socket;
        ctl::ctSockaddr remote_sockaddr;
        RIO_RQ rio_rq;
        RIO_BUF rio_remote_address;
        size_t rqueue_reserved;
        size_t rqueue_used;

        ///
        /// Guarantees that there is roon in the RQ for the next IO request
        /// - assumes the caller has locked the this->weak_socket -> ctsSocket
        ///
        /// Returns NO_ERROR for success, or a Win32 error on failure
        ///
        DWORD make_room_in_rq() NOEXCEPT
        {
            try {
                size_t new_rqueue_used = this->rqueue_used + RioRQGrowthFactor;
                // guarantee room in the RQ for this next IO
                if (new_rqueue_used > this->rqueue_reserved) {
                    // making room in the CQ for these next 2 slots in the RQ - can throw
                    s_make_room_in_cq(RioRQGrowthFactor);
                    ctlScopeGuard(releaseCqSlotsOnFailure, { s_release_room_in_cq(RioRQGrowthFactor); });

                    // guarantee room in the RQ for this next IO
                    PrintDebugInfo(
                        L"\t\tctRIOResizeRequestQueue: Resizing the RQ to %Iu\n", new_rqueue_used);
                    if (!ctl::ctRIOResizeRequestQueue(
                        this->rio_rq,
                        static_cast<DWORD>(new_rqueue_used / 2),
                        static_cast<DWORD>(new_rqueue_used / 2))) {
                        throw ctl::ctException(::WSAGetLastError(), L"RIOResizeRequestQueue", false);
                    }

                    // since it succeeded, update reserved with the new size
                    this->rqueue_reserved = new_rqueue_used;
                    // RQ was updated successfully, dismiss the scope guard
                    releaseCqSlotsOnFailure.dismiss();
                }

                // everything succeeded - update rqueue_used with the new slots being used for this next IO
                this->rqueue_used = new_rqueue_used;
            }
            catch (const ctl::ctException& e) {
                ctsConfig::PrintException(e);
                return e.why();
            }
            catch (const std::exception& e) {
                ctsConfig::PrintException(e);
                return WSAENOBUFS;
            }
            return NO_ERROR;
        }

        ///
        /// Guarantees that there is roon in the RQ for the next IO request
        /// - assumes the caller has locked the this->weak_socket -> ctsSocket
        ///
        void release_room_in_rq() NOEXCEPT
        {
            this->rqueue_used -= RioRQGrowthFactor;
        }

    public:
        explicit RioSocketContext(const std::weak_ptr<ctsSocket>& _weak_socket)
            : weak_socket(_weak_socket),
            remote_sockaddr(),
            rio_rq(RIO_INVALID_RQ),
            rio_remote_address(),
            rqueue_reserved(RioRQGrowthFactor),
            rqueue_used(0)
        {
            // first initialize the RIO structure
            rio_remote_address.BufferId = RIO_INVALID_BUFFERID;
            rio_remote_address.Length = 0;
            rio_remote_address.Offset = 0;

            auto shared_socket = weak_socket.lock();
            if (!shared_socket) {
                throw std::exception("ctsRioIocp: null socket given to RioSocketContext");
            }

            // lock the socket when doing IO on it
            auto socket_lock(ctsGuardSocket(shared_socket));
            SOCKET socket = socket_lock.get();
            if (INVALID_SOCKET == socket) {
                throw std::exception("ctsRioIocp: invalid socket given to RioSocketContext");
            }

            s_make_room_in_cq(RioRQGrowthFactor);
            ctlScopeGuard(releaseRoomInCqOnFailure, { s_release_room_in_cq(RioRQGrowthFactor); });

            // create the RQ for this socket
            // don't need a scope guard to close the RQ on error - the RQ is freed when the RIO socket is closed
            rio_rq = ctl::ctRIOCreateRequestQueue(
                socket,
                RioRQGrowthFactor / 2, RioMaxDataBuffers,
                RioRQGrowthFactor / 2, RioMaxDataBuffers,
                s_rio_cq,
                s_rio_cq,
                this);
            if (RIO_INVALID_RQ == rio_rq) {
                throw ctl::ctException(::WSAGetLastError(), L"RIOCreateRequestQueue", false);
            }

            // now register the target remote address for UDP for RIOSendTo
            if (ctsConfig::ProtocolType::UDP == ctsConfig::Settings->Protocol) {
                this->remote_sockaddr = shared_socket->target_address();
                this->rio_remote_address.Length = static_cast<ULONG>(sizeof SOCKADDR_INET);
                this->rio_remote_address.BufferId =
                    ctl::ctRIORegisterBuffer(
                        reinterpret_cast<PCHAR>(this->remote_sockaddr.sockaddr_inet()),
                        static_cast<DWORD>(sizeof SOCKADDR_INET));
                if (RIO_INVALID_BUFFERID == this->rio_remote_address.BufferId) {
                    throw ctl::ctException(::WSAGetLastError(), L"RIORegisterBuffer", false);
                }
            }

            // no failures
            releaseRoomInCqOnFailure.dismiss();
        }
        ~RioSocketContext() NOEXCEPT
        {
            // release all the space in the CQ for this RQ
            s_release_room_in_cq(static_cast<ULONG>(this->rqueue_reserved));

            if (this->rio_remote_address.BufferId != RIO_INVALID_BUFFERID) {
                ctl::ctRIODeregisterBuffer(this->rio_remote_address.BufferId);
            }
        }

        ///
        /// Exposes access to a shared_ptr for the contained ctsSocket
        ///
        std::shared_ptr<ctsSocket> get_socket() const NOEXCEPT
        {
            return this->weak_socket.lock();
        }

        /// 
        /// Should be called once for every IO that was completed
        /// Returns the current # of outstanding IO on the socket
        ///
        LONG complete_io(const ctsIOTask& _task, ULONG _transferred, LONG _status) NOEXCEPT
        {
            // get a reference on the ctsSocket and IOPattern
            auto shared_socket(this->weak_socket.lock());
            if (!shared_socket) {
                return 0;
            }
            //
            // hold a reference on the iopattern
            //
            auto shared_pattern(shared_socket->io_pattern());
            //
            // Must lock the socket before doing anything on it
            //
            auto socket_lock(ctsGuardSocket(shared_socket));
            //
            // decrement the counter in our RQ for the completed IO
            //
            this->release_room_in_rq();
            //
            // Complete_io() to see if need to issue more IO
            // - will return this error unless the protocol wants more IO 
            // - if the protocol wants more IO even though this failed, 
            //   will return the error from the next IO
            //
            const wchar_t* RIOFunction = nullptr;
            if (ctsConfig::ProtocolType::TCP == ctsConfig::Settings->Protocol) {
                switch (_task.ioAction) {
                case IOTaskAction::Recv:
                    RIOFunction = L"RIOReceive";
                    break;
                case IOTaskAction::Send:
                    RIOFunction = L"RIOSend";
                    break;
                }
            } else {
                switch (_task.ioAction) {
                case IOTaskAction::Recv:
                    RIOFunction = L"RIOReceiveEx";
                    break;
                case IOTaskAction::Send:
                    RIOFunction = L"RIOSendEx";
                    break;
                }
            }
            _Analysis_assume_(RIOFunction != nullptr);

            if (_status != 0) PrintDebugInfo(L"\t\tIO Failed: %s (%d) [ctsReadWriteIocp]\n", RIOFunction, _status);

            DWORD error = _status;
            ctsIOStatus protocol_status = shared_pattern->complete_io(_task, _transferred, _status);
            switch (protocol_status) {
            case ctsIOStatus::ContinueIo:
                // more IO is requested from the protocol
                // launch the next IO while holding the socket lock in complete_io
                error = this->execute_io();
                break;

            case ctsIOStatus::CompletedIo:
                // no more IO is requested from the protocol
                error = NO_ERROR;
                break;

            case ctsIOStatus::FailedIo:
                // write out the error
                ctsConfig::PrintErrorIfFailed(RIOFunction, _status);
                // protocol sees this as a failure - capture the error the protocol recorded
                error = shared_pattern->get_last_error();
                break;

            default:
                ctl::ctAlwaysFatalCondition(L"ctsSendRecvIocp: unknown ctsSocket::IOStatus - %u\n", static_cast<unsigned>(protocol_status));
            }
            //
            // finally decrement the IO counter for the completed IO that triggered this complete_io function call
            // 
            LONG current_io = shared_socket->decrement_io();
            if (0 == current_io) {
                shared_socket->complete_state(error);
            }

            return current_io;
        }
        ///
        /// Attempts to send/recv IO on the socket
        /// Returns the counter of pended IO on the socket
        ///
        LONG execute_io() NOEXCEPT
        {
            auto shared_socket(this->weak_socket.lock());
            ctl::ctFatalCondition(
                !shared_socket,
                L"RioSocketContext::execute_io (this == %p): the ctsSocket should always be valid - it's now nullshared_socket, get() should always return a valid ptr", this);
            // hold a reference on the iopattern
            auto shared_pattern(shared_socket->io_pattern());

            // hold onto the RIO socket lock while posting IO on it
            auto socket_lock(ctsGuardSocket(shared_socket));
            SOCKET rio_socket = socket_lock.get();
            if (INVALID_SOCKET == rio_socket) {
                return WSAECONNABORTED;
            }

            // can't initialize to zero - zero indicates to complete_state()
            long refcount_io = -1;
            bool continue_io = true;
            // loop until complete_io() doesn't offer IO
            while (continue_io) {
                DWORD error = NO_ERROR;

                // push IO until None is returned
                ctsIOTask next_io = shared_pattern->initiate_io();
                if (IOTaskAction::None == next_io.ioAction) {
                    break;
                }

                if (IOTaskAction::GracefulShutdown == next_io.ioAction) {
                    if (0 != ::shutdown(rio_socket, SD_SEND)) {
                        error = ::WSAGetLastError();
                    }
                    continue_io = (shared_pattern->complete_io(next_io, 0, error) == ctsIOStatus::ContinueIo);
                    continue;
                }

                if (IOTaskAction::HardShutdown == next_io.ioAction) {
                    // pass through -1 to force an RST with the closesocket
                    error = shared_socket->close_socket(-1);
                    rio_socket = INVALID_SOCKET;

                    continue_io = (shared_pattern->complete_io(next_io, 0, error) == ctsIOStatus::ContinueIo);
                    continue;
                }

                // else we're attempting IO
                // we need IO so pre-incremenet the IO on the socket before issuing the IO
                refcount_io = shared_socket->increment_io();
                const wchar_t* RIOFunction = nullptr;

                // Allocate a context to pass with each request (the ctsIOTask)
                std::unique_ptr<ctsIOTask> request_context(new (std::nothrow) ctsIOTask(next_io));
                if (nullptr == request_context.get()) {
                    RIOFunction = L"alloc ctsIOTask";
                    error = WSAENOBUFS;
                }

                bool release_rq_slots = false;
                RIO_BUF rio_buffer;
                if (NO_ERROR == error) {
                    // with the IOTask, we can construct the RIO_BUF to send/recv
                    rio_buffer.BufferId = request_context->rio_bufferid;
                    rio_buffer.Length = request_context->buffer_length;
                    rio_buffer.Offset = request_context->buffer_offset;
                    // must ensure we have room in the RQ & CQ before initiating the IO
                    RIOFunction = L"RIOResizeRequestQueue";
                    error = this->make_room_in_rq();
                    // track to release slots if successfully made room
                    release_rq_slots = (NO_ERROR == error);
                }

                if (NO_ERROR == error) {
                    // invoke the requested IO now that we have room in our queues
                    if (ctsConfig::ProtocolType::TCP == ctsConfig::Settings->Protocol) {
                        switch (request_context->ioAction) {
                        case IOTaskAction::Recv:
                            RIOFunction = L"RIOReceive";
                            if (!ctl::ctRIOReceive(this->rio_rq, &rio_buffer, 1, 0, request_context.get())) {
                                error = ::WSAGetLastError();
                            }
                            break;
                        case IOTaskAction::Send:
                            RIOFunction = L"RIOSend";
                            if (!ctl::ctRIOSend(this->rio_rq, &rio_buffer, 1, 0, request_context.get())) {
                                error = ::WSAGetLastError();
                            }
                            break;
                        }
                    } else {
                        switch (request_context->ioAction) {
                        case IOTaskAction::Recv:
                            RIOFunction = L"RIOReceiveEx";
                            if (!ctl::ctRIOReceiveEx(this->rio_rq, &rio_buffer, 1, nullptr, nullptr, nullptr, nullptr, 0, request_context.get())) {
                                error = ::WSAGetLastError();
                            }
                            break;
                        case IOTaskAction::Send:
                            RIOFunction = L"RIOSendEx";
                            PRIO_BUF premote = &this->rio_remote_address;
                            if (!ctl::ctRIOSendEx(this->rio_rq, &rio_buffer, 1, nullptr, premote, nullptr, nullptr, 0, request_context.get())) {
                                error = ::WSAGetLastError();
                            }
                            break;
                        }
                    }
                }

                // if IO was not initiated, complete the IO back the IO pattern
                if (error != NO_ERROR) {
                    ctsConfig::PrintException(ctl::ctException(error, RIOFunction, false));
                    continue_io = (shared_pattern->complete_io(next_io, 0, error) == ctsIOStatus::ContinueIo);
                    refcount_io = shared_socket->decrement_io();
                    if (release_rq_slots) {
                        this->release_room_in_rq();
                    }
                } else {
                    // don't let the request_context be freed: it's now the context ptr in the RIO request
                    request_context.release();
                }
            } // while (...)

            return refcount_io;
        }

        // non-copyable
        RioSocketContext(const RioSocketContext&) = delete;
        RioSocketContext& operator=(const RioSocketContext&) = delete;
    };


    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Logic for the thread pool function
    ///
    /// - Wait for Notify to wake up the IOCP
    /// - once notified, take a reader lock over the cq (hold off the writers)
    ///   - subsequently taking the CS
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static DWORD WINAPI RioIocpThreadProc(LPVOID) NOEXCEPT
    {
        RIORESULT rio_result_array[RioResultArrayLength];

        for (;;) {
            DWORD transferred;
            ULONG_PTR Key = 0;
            OVERLAPPED* pov = nullptr;
            //
            // Wait for the IOCP to be queued from RIO that we have results in our CQ
            //
            if (!::GetQueuedCompletionStatus(
                s_rio_notify_setttings.Iocp.IocpHandle,
                &transferred,
                &Key,
                &pov,
                INFINITE)) {
                DWORD gle = ::GetLastError();

                // no IO was dequeued from the IOCP
                // - no idea why this failed
                // break to see what's going on in case it's a real bug
                ctl::ctFatalCondition(
                    (nullptr == pov),
                    L"GetQueuedCompletionStatus(%p) failed [%u] without dequeing any IO",
                    s_rio_notify_setttings.Iocp.IocpHandle, gle);

                // IO was dequeued from the IOCP, meaning the deque operation failed
                // - if Deque failed, we don't know what is going on
                //   This should mean the CQ is in a bad state ???
                //   Will kill the test into the debugger to investigate
                //     - this is not recoverable other than closing every socket 
                //       and making a new CQ
                ctl::ctFatalCondition(
                    (nullptr != pov),
                    L"GetQueuedCompletionStatus(%p) dequeued a failed IO [%u] - OVERLAPPED [%p]",
                    s_rio_notify_setttings.Iocp.IocpHandle, gle, pov);
            }

            if (ExitCompletionKey == Key) {
                break;
            }

            //
            // Dequeue from the RIO socket under our locks
            // - note: Dequeue will invoke a RIONotify
            //
            ULONG deque_result = s_deque_from_cq(rio_result_array);

            // Now that we have dequeued the IO
            // - iterate through each one and take next steps:
            //   - once we have no more IO on that socket (returned from complete_io)
            //   - delete the socket context
            //   - note: interactions with the ctsSocket* are all contained in the socket_context
            //           never directly interacting with the ctsSocket* here
            for (ULONG iter_result = 0; iter_result < deque_result; ++iter_result) {

                transferred = rio_result_array[iter_result].BytesTransferred;
                LONG status = rio_result_array[iter_result].Status;
                ctsIOTask* request_context = reinterpret_cast<ctsIOTask*>(rio_result_array[iter_result].RequestContext);
                RioSocketContext* socket_context = reinterpret_cast<RioSocketContext*>(rio_result_array[iter_result].SocketContext);
                //
                // Complete the dequeued IO to track the IO
                // - will kick off another IO if required
                // Returns the # of IO outstanding on that socket
                // - if zero, we're done with it
                //
                if (0 == socket_context->complete_io(*request_context, transferred, status)) {
                    delete socket_context;
                }
                // always delete the prior request_context
                delete request_context;
            } // for (iter_results)
        } // for (;;)

        return 0;
    } // RioIocpThreadProc


    void ctsRioIocp(const std::weak_ptr<ctsSocket>& _weak_socket) NOEXCEPT
    {
        // attempt to get a reference to the socket
        auto shared_socket(_weak_socket.lock());
        if (!shared_socket) {
            return;
        }

        //
        // guarantee fully initialized
        //
        if (!::InitOnceExecuteOnce(&s_sharedbuffer_initializer, s_init_once_cq, nullptr, nullptr)) {
            auto gle = ::GetLastError();
            if (0 == gle) {
                gle = WSAENOBUFS;
            }
            ctsConfig::PrintException(ctl::ctException(gle, L"InitOnceExecuteOnce", L"ctsIOPattern", false));
            shared_socket->complete_state(gle);
            return;
        }

        //
        // context ptrs are declared outside the try block
        // - in the case something fails
        // - we must pass these as raw ptrs to RIO, so can't use std containers
        //
        long io_count = 0;
        DWORD error = 0;
        RioSocketContext* socket_context = nullptr;
        try {
            // Allocate the socket context to pass through every IO on this socket
            socket_context = new RioSocketContext(_weak_socket);
            // kick off IO on this RIO socket
            io_count = socket_context->execute_io();
        }
        catch (const ctl::ctException& e) {
            ctsConfig::PrintException(e);
            error = (0 == e.why()) ? WSAENOBUFS : e.why();
        }
        catch (const std::exception& e) {
            ctsConfig::PrintException(e);
            error = WSAENOBUFS;
        }

        // complete the socket state back to the parent if there is no pended IO
        if (0 == io_count) {
            // shared_socket was indeed initialized - there is no path where it could not have been uninitialized
#pragma warning(suppress: 6001)
            shared_socket->complete_state(error);
            delete socket_context;
        }
    }
}
