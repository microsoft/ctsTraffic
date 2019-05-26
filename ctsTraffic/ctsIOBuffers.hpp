/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <vector>
// os headers
#include <windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
// ctl headers
#include <ctException.hpp>
#include <ctLocks.hpp>
// project headers
#include "ctsConfig.h"
#include "ctsStatistics.hpp"
#include "ctSocketExtensions.hpp"
#include "ctsIOTask.hpp"
#include "ctsSafeInt.hpp"

namespace ctsTraffic {

    namespace statics {
        // forward-declarations
        inline static bool GrowConnectionIdBuffer() noexcept;

        // pre-reserving for up to 1 million concurrent connections
        static const unsigned long ServerMaxConnections = 1000000UL;
        static unsigned long ServerConnectionGrowthRate = 2500UL;

        static unsigned long CurrentAllocatedConnectionCount = 0;
        static unsigned long SystemPageSize = 0UL;

        // ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
        static ::INIT_ONCE ConnectionIdInitOnce = INIT_ONCE_STATIC_INIT;
        static char* ConnectionIdBuffer = nullptr;
        static ::std::vector<char*>* ConnectionIdVector = nullptr;
        static ::RIO_BUFFERID ConnectionIdRioBufferId = RIO_INVALID_BUFFERID;
        static ::CRITICAL_SECTION ConnectionIdLock;

        static BOOL CALLBACK InitOnceIOPatternCallback(PINIT_ONCE, PVOID, PVOID *) noexcept
        {
            using ::ctsTraffic::ctsConfig::Settings;
            using ::ctsTraffic::ctsConfig::IsListening;
            using ::ctsTraffic::ctsStatistics::ConnectionIdLength;

            if (!::InitializeCriticalSectionEx(&statics::ConnectionIdLock, 4000, 0)) {
                ::ctl::ctAlwaysFatalCondition(L"InitializeCriticalSectionEx failed: %d", ::WSAGetLastError());
            }

            ::SYSTEM_INFO sysInfo;         // Useful information about the system
            ::GetSystemInfo(&sysInfo);     // Initialize the structure.
            statics::SystemPageSize = sysInfo.dwPageSize;

            if (!IsListening()) {
                statics::ConnectionIdBuffer = static_cast<char*>(::VirtualAlloc(
                    nullptr,
                    ConnectionIdLength * Settings->ConnectionLimit,
                    MEM_RESERVE | MEM_COMMIT,
                    PAGE_READWRITE));
                if (!statics::ConnectionIdBuffer) {
                    ::ctl::ctAlwaysFatalCondition(L"VirtualAlloc alloc failed: %u", ::GetLastError());
                }

                // assign a buffer* for each connection_id buffer
                unsigned long connection_count = 0;
                statics::ConnectionIdVector = new ::std::vector<char*>(Settings->ConnectionLimit);
                for (auto& buffer : *statics::ConnectionIdVector) {
                    buffer = statics::ConnectionIdBuffer + (connection_count * ConnectionIdLength);
                    ++connection_count;
                }

                if (Settings->SocketFlags & WSA_FLAG_REGISTERED_IO) {
                    statics::ConnectionIdRioBufferId = ::ctl::ctRIORegisterBuffer(
                        statics::ConnectionIdBuffer,
                        ConnectionIdLength * Settings->ConnectionLimit);
                    if (RIO_INVALID_BUFFERID == statics::ConnectionIdRioBufferId) {
                        ::ctl::ctAlwaysFatalCondition(L"RIORegisterBuffer failed: %d", ::WSAGetLastError());
                    }
                }
            } else {
                //
                // since servers don't know beforehand exactly how many connections they might be fielding
                // - they'll need to ensure they can grow their # of Connection ID buffers as necessary
                // This will be achieved by first reserving one contiguous memory range to guarantee a contiguous
                // - address range -> we will default to reserving enough contiguous space for 1,000,000 active connections
                // Then we'll commit chunks of that memory range as we need them
                // This greatly simplifies tracking of individual buffers, as these are guaranteed contiguous
                //
                statics::ConnectionIdBuffer = static_cast<char*>(::VirtualAlloc(
                    nullptr,
                    ConnectionIdLength * statics::ServerMaxConnections,
                    MEM_RESERVE,
                    PAGE_READWRITE));
                if (!statics::ConnectionIdBuffer) {
                    ::ctl::ctAlwaysFatalCondition(L"VirtualAlloc alloc failed: %u", ::GetLastError());
                }

                statics::ConnectionIdVector = new ::std::vector<char*>();
                statics::CurrentAllocatedConnectionCount = 0;
                if (!statics::GrowConnectionIdBuffer()) {
                    ::ctl::ctAlwaysFatalCondition(L"VirtualAlloc or new vector<char*> failed");
                }

                // TODO: how will we track the RIO Buffer Id across regrowths of this address space?
                // - need to trace overlapping RIO Buffer Ids - so we create a new one
                // - and once all buffers are returned from the old Id, we can release it?
                // must take into account that we could potentially regrow it before all of N-1 RIO buffers are returned
                // - so we'll have to be prepared to refcount N number of RIO buffer Id's
                if ((Settings->SocketFlags & WSA_FLAG_REGISTERED_IO) && (RIO_INVALID_BUFFERID == statics::ConnectionIdRioBufferId)) {
                    statics::ConnectionIdRioBufferId = ::ctl::ctRIORegisterBuffer(
                        statics::ConnectionIdBuffer,
                        statics::CurrentAllocatedConnectionCount * ConnectionIdLength);
                    if (RIO_INVALID_BUFFERID == statics::ConnectionIdRioBufferId) {
                        ::ctl::ctAlwaysFatalCondition(L"RIORegisterBuffer failed: %d", ::WSAGetLastError());
                    }
                }

            }
            return TRUE;
        }
        //////////////////////////////////////////////////////////////////////////
        //
        // GrowConnectionIdBuffer
        //
        // called when the server needs to grow the # of committed pages 
        // - to handle more incoming connections
        //
        //////////////////////////////////////////////////////////////////////////
        inline static bool GrowConnectionIdBuffer() noexcept
        {
            using ::ctsTraffic::ctsStatistics::ConnectionIdLength;
            using ::ctsTraffic::ctsUnsignedLong;

            const ctsUnsignedLong original_connections = statics::CurrentAllocatedConnectionCount;
            const ctsUnsignedLong increased_available_connections = statics::CurrentAllocatedConnectionCount + statics::ServerConnectionGrowthRate;
            const ctsUnsignedLongLong commit_size_bytes = increased_available_connections * ConnectionIdLength;
            if (!::VirtualAlloc(statics::ConnectionIdBuffer, commit_size_bytes, MEM_COMMIT, PAGE_READWRITE)) {
                return false;
            }

            try {
                // work on a temp vector: not risking our currently used vector
                // copy what pointers are still in our static buffer
                std::vector<char*> temp_connection_id_vector(*statics::ConnectionIdVector);
                // guarantee we have allocated enough for all connections by reserving the necessary size
                temp_connection_id_vector.reserve(increased_available_connections);

                // if all dynamic allocations for the vector have succeeded, save the new count
                statics::CurrentAllocatedConnectionCount = increased_available_connections;

                // as some buffers may be given out, we can't just populate the buffer of char* across the entire newly committed buffer
                // - we need to copy over what buffers we still had in our static vector (done in the above vector c'tor)
                // - and only add buffers that were added with this new commit
                char* iter_buffer_value = statics::ConnectionIdBuffer + static_cast<unsigned long>(original_connections * ConnectionIdLength);
                char* end_buffer_value = statics::ConnectionIdBuffer + static_cast<unsigned long>(commit_size_bytes);
                for (; iter_buffer_value < end_buffer_value; iter_buffer_value += ConnectionIdLength) {
                    temp_connection_id_vector.push_back(iter_buffer_value);
                }

                // no-fail: swap the temp vector into our static vector
                statics::ConnectionIdVector->swap(temp_connection_id_vector);
            }
            catch (const ::std::exception&) {
                return false;
            }
            return true;
        }

    }

    namespace ctsIOBuffers {

        //////////////////////////////////////////////////////////////////////////
        //
        // Will throw std::bad_alloc on low resource conditions
        //
        //////////////////////////////////////////////////////////////////////////
        inline ::ctsTraffic::ctsIOTask NewConnectionIdBuffer(_In_reads_(ctsStatistics::ConnectionIdLength) char* _connection_id)
        {
            // this init-once call is no-fail
            (void) ::InitOnceExecuteOnce(&statics::ConnectionIdInitOnce, statics::InitOnceIOPatternCallback, nullptr, nullptr);

            ::ctsTraffic::ctsIOTask return_task;
            char* next_buffer;
            {
                const ::ctl::ctAutoReleaseCriticalSection connection_id_lock(&statics::ConnectionIdLock);
                if (statics::ConnectionIdVector->empty()) {
                    ::ctl::ctFatalCondition(
                        !::ctsTraffic::ctsConfig::IsListening(),
                        L"The ConnectionId vector should never be empty for clients: it should be pre-allocated with exactly the number necessary");
                    if (!statics::GrowConnectionIdBuffer()) {
                        throw std::bad_alloc();
                    }
                    ::ctl::ctFatalCondition(
                        statics::ConnectionIdVector->empty(),
                        L"The ConnectionId vector should never be empty after re-growing it");
                }

                next_buffer = *statics::ConnectionIdVector->rbegin();
                statics::ConnectionIdVector->pop_back();
            }

            const auto copy_error = ::memcpy_s(next_buffer, ctsStatistics::ConnectionIdLength, _connection_id, ctsStatistics::ConnectionIdLength);
            ctl::ctFatalCondition(
                copy_error != 0,
                L"ctsIOBuffers::NewConnectionIdBuffer : memcpy_s failed trying to copy the connection ID - buffer (%p) (error : %d)",
                next_buffer,
                copy_error);

            if (::ctsTraffic::ctsConfig::Settings->SocketFlags & WSA_FLAG_REGISTERED_IO) {
                // RIO is registered at the ConnectionIdBuffer address
                // - thus needs to specify the offset to get to the unique buffer for this request
                return_task.buffer = statics::ConnectionIdBuffer;
                return_task.buffer_offset = static_cast<unsigned long>(next_buffer - statics::ConnectionIdBuffer);
                return_task.buffer_length = ::ctsTraffic::ctsStatistics::ConnectionIdLength;
                return_task.rio_bufferid = statics::ConnectionIdRioBufferId;
                return_task.buffer_type = ctsIOTask::BufferType::TcpConnectionId;
                return_task.track_io = false;
            } else {
                return_task.buffer = next_buffer;
                return_task.buffer_offset = 0;
                return_task.buffer_length = ::ctsTraffic::ctsStatistics::ConnectionIdLength;
                return_task.rio_bufferid = RIO_INVALID_BUFFERID;
                return_task.buffer_type = ctsIOTask::BufferType::TcpConnectionId;
                return_task.track_io = false;
            }
            return return_task;
        }

        inline void ReleaseConnectionIdBuffer(const ::ctsTraffic::ctsIOTask& _task) noexcept
        {
            const ::ctl::ctAutoReleaseCriticalSection connection_id_lock(&statics::ConnectionIdLock);
            try {
                // the vector was initially reserved to be large enough to hold all possible buffers
                // - push-back() is no-throw in these cases
                if (::ctsTraffic::ctsConfig::Settings->SocketFlags & WSA_FLAG_REGISTERED_IO) {
                    // RIO gives out offsets from the base address of the buffers
                    statics::ConnectionIdVector->push_back(statics::ConnectionIdBuffer + _task.buffer_offset);
                } else {
                    statics::ConnectionIdVector->push_back(_task.buffer);
                }
            }
            catch (const ::std::exception&) {
                ::ctl::ctAlwaysFatalCondition(L"Returning buffer* back to the vector should never fail: the vector is always pre-allocated");
            }
        }

        inline bool SetConnectionId(_Inout_updates_(ctsStatistics::ConnectionIdLength) char* _target_buffer, const ::ctsTraffic::ctsIOTask& _task, unsigned long _current_transfer) noexcept
        {
            if (_current_transfer != ctsStatistics::ConnectionIdLength) {
                PrintDebugInfo(
                    L"\t\tctsIOBuffers::SetConnectionId : the bytes received (%u) do not equal the expected length for the connection Id (%u)\n",
                    _current_transfer, ctsStatistics::ConnectionIdLength);
                return false;
            }

            char* io_buffer;
            if (::ctsTraffic::ctsConfig::Settings->SocketFlags & WSA_FLAG_REGISTERED_IO) {
                // RIO is registered at the ConnectionIdBuffer address
                // - thus needs to specify the offset to get to the unique buffer for this request
                io_buffer = statics::ConnectionIdBuffer + _task.buffer_offset;
            } else {
                io_buffer = _task.buffer;
            }

            const auto copy_error = ::memcpy_s(_target_buffer, ctsStatistics::ConnectionIdLength, io_buffer, ctsStatistics::ConnectionIdLength);
            ctl::ctFatalCondition(
                copy_error != 0,
                L"ctsIOBuffers::SetConnectionId : memcpy_s failed trying to copy the connection ID - target buffer (%p) ctsIOTask (%p) (error : %d)",
                _target_buffer,
                &_task,
                copy_error);
            return true;
        }
    }
}
