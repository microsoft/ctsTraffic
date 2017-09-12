/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// OS headers
#include <windows.h>
// ctl headers
#include <ctVersionConversion.hpp>
#include <ctException.hpp>
#include <ctSocketExtensions.hpp>

#include "ctsIOTask.hpp"
#include "ctsConfig.h"

namespace ctsTraffic
{
    using namespace ctl;
    using namespace std;

    static const unsigned long BufferPatternSize = 0xffff + 0x1; // fill from 0x0000 to 0xffff
    static unsigned char BufferPattern[BufferPatternSize * 2]; // * 2 as unsigned short values are twice as large as unsigned char

    static INIT_ONCE s_IOPatternInitializer = INIT_ONCE_STATIC_INIT;
    static char* s_WriteableSharedBuffer = nullptr;
    static char* s_ProtectedSharedBuffer = nullptr;
    static unsigned long s_SharedBufferSize = 0;

    static const char* s_CompletionMessage = "DONE";
    static const unsigned long s_CompletionMessageSize = 4;
    static const unsigned long s_FinBufferSize = 4; // just 4 bytes for the FIN
    static char s_FinBuffer[s_FinBufferSize];

    BOOL CALLBACK InitOnceIOPatternCallback(PINIT_ONCE, PVOID, PVOID *) NOEXCEPT
    {
        // first create the buffer pattern
        for (unsigned long fill_slot = 0; fill_slot < BufferPatternSize; ++fill_slot) {
            *reinterpret_cast<unsigned short*>(&BufferPattern[fill_slot * 2]) = static_cast<unsigned short>(fill_slot);
        }

        s_SharedBufferSize = BufferPatternSize + ctsConfig::GetMaxBufferSize() + s_CompletionMessageSize;

        s_ProtectedSharedBuffer = reinterpret_cast<char*>(::VirtualAlloc(nullptr, s_SharedBufferSize, MEM_COMMIT, PAGE_READWRITE));
        if (!s_ProtectedSharedBuffer) {
            ctAlwaysFatalCondition(L"VirtualAlloc alloc failed: %u", ::GetLastError());
        }

        s_WriteableSharedBuffer = reinterpret_cast<char*>(::VirtualAlloc(nullptr, s_SharedBufferSize, MEM_COMMIT, PAGE_READWRITE));
        if (!s_WriteableSharedBuffer) {
            ctAlwaysFatalCondition(L"VirtualAlloc alloc failed: %u", ::GetLastError());
        }

        // fill in this allocated buffer while we can write to it
        char* protected_destination = s_ProtectedSharedBuffer;
        char* writeable_destination = s_WriteableSharedBuffer;
        unsigned long write_size_remaining = s_SharedBufferSize;
        while (write_size_remaining > 0) {
            const unsigned long bytes_to_write = (write_size_remaining > BufferPatternSize) ? BufferPatternSize : write_size_remaining;

            auto memerror = ::memcpy_s(protected_destination, write_size_remaining, BufferPattern, bytes_to_write);
            ctFatalCondition(
                memerror != 0,
                L"memcpy_s(%p, %lu, %p, %lu) failed : %d",
                protected_destination, write_size_remaining, BufferPattern, bytes_to_write, memerror);

            memerror = ::memcpy_s(writeable_destination, write_size_remaining, BufferPattern, bytes_to_write);
            ctFatalCondition(
                memerror != 0,
                L"memcpy_s(%p, %lu, %p, %lu) failed : %d",
                writeable_destination, write_size_remaining, BufferPattern, bytes_to_write, memerror);

            protected_destination += bytes_to_write;
            writeable_destination += bytes_to_write;
            write_size_remaining -= bytes_to_write;
        }
        // set the final 4 bytes to the DONE message for the send buffer
        ::memcpy_s(
            s_ProtectedSharedBuffer + s_SharedBufferSize - s_CompletionMessageSize,
            s_CompletionMessageSize,
            s_CompletionMessage,
            s_CompletionMessageSize);
        ::memcpy_s(
            s_WriteableSharedBuffer + s_SharedBufferSize - s_CompletionMessageSize,
            s_CompletionMessageSize,
            s_CompletionMessage,
            s_CompletionMessageSize);

        // guarantee noone will write to our s_ProtectedSharedBuffer
        DWORD old_setting;
        if (!::VirtualProtect(s_ProtectedSharedBuffer, s_SharedBufferSize, PAGE_READONLY, &old_setting)) {
            ctAlwaysFatalCondition(L"VirtualProtect failed: %u", ::GetLastError());
        }

        return TRUE;
    }

	namespace ctsIOPatternBufferPolicyBuffers
	{
		void Init() NOEXCEPT
		{
			(void) ::InitOnceExecuteOnce(&s_IOPatternInitializer, InitOnceIOPatternCallback, nullptr, nullptr);
		}

		constexpr unsigned long BufferSize() NOEXCEPT
		{
			return BufferPatternSize;
		}
		constexpr unsigned long CompletionBufferSize() NOEXCEPT
		{
			return s_CompletionMessageSize;
		}

		bool Verify(const ctsIOTask& _task, unsigned long _received_bytes) NOEXCEPT
		{
			// We're using RtlCompareMemory instead of memcmp because it returns the first offset at which the buffers differ,
			// which is more useful than memcmp's "sign of the difference between the first two differing elements"
			auto pattern_buffer = s_ProtectedSharedBuffer + _task.expected_pattern_offset;
			size_t length_matched = ::RtlCompareMemory(
				pattern_buffer,
				_task.buffer + _task.buffer_offset,
				_received_bytes);

			if (length_matched != _received_bytes) {
				ctsConfig::PrintErrorInfo(
					L"ctsIOPattern found data corruption: detected an invalid byte pattern in the returned buffer (length %u): "
					L"buffer received (%p), expected buffer pattern (%p) - mismatch from expected pattern at offset (%Iu) [expected 32-bit value '0x%x' didn't match '0x%x']",
					_received_bytes,
					_task.buffer + _task.buffer_offset,
					pattern_buffer,
					length_matched,
					pattern_buffer[length_matched],
					*(_task.buffer + _task.buffer_offset + length_matched));
			}

			return (length_matched == _received_bytes);
		}

		RIO_BUFFERID GetRIOSendBuffer()
		{
			// establish a RIO ID for the writable shared buffer if we're using RIO APIs
			auto send_buffer_id = ctl::ctRIORegisterBuffer(s_WriteableSharedBuffer, s_SharedBufferSize);
			if (RIO_INVALID_BUFFERID == send_buffer_id) {
				throw ctl::ctException(::WSAGetLastError(), L"ctl::ctRIORegisterBuffer", L"ctsIOPatternBufferPolicy");
			}
			return send_buffer_id;
		}

		ctsIOTask GetSendCompletion() NOEXCEPT
		{
			ctsIOTask return_task;
			return_task.ioAction = IOTaskAction::Send;
			return_task.buffer = s_ProtectedSharedBuffer;
			return_task.buffer_length = s_CompletionMessageSize;
			return_task.buffer_offset = s_SharedBufferSize - s_CompletionMessageSize;
			return_task.buffer_type = ctsIOTask::BufferType::Static;
			return_task.rio_bufferid = RIO_INVALID_BUFFERID;
			return_task.track_io = false;
			return return_task;
		}
		ctsIOTask GetRecvCompletion() NOEXCEPT
		{
			ctsIOTask return_task;
			return_task.ioAction = IOTaskAction::Recv;
			return_task.buffer = s_WriteableSharedBuffer;
			return_task.buffer_length = s_CompletionMessageSize;
			return_task.buffer_offset = s_SharedBufferSize - s_CompletionMessageSize;
			return_task.buffer_type = ctsIOTask::BufferType::Static;
			return_task.rio_bufferid = RIO_INVALID_BUFFERID;
			return_task.track_io = false;
			return return_task;
		}
		ctsIOTask GetFin() NOEXCEPT
		{
			ctsIOTask return_task;
			return_task.ioAction = IOTaskAction::Recv;
			return_task.buffer = s_FinBuffer;
			return_task.buffer_length = s_FinBufferSize;
			return_task.buffer_offset = 0;
			return_task.buffer_type = ctsIOTask::BufferType::Static;
			return_task.rio_bufferid = RIO_INVALID_BUFFERID;
			return_task.track_io = false;
			return return_task;
		}
	}
}
