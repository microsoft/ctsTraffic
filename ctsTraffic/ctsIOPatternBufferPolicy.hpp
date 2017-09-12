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
    // defined in ctsIOPatternBufferPolicy.cpp
	namespace ctsIOPatternBufferPolicyBuffers
	{
		void Init() NOEXCEPT;
		constexpr unsigned long BufferSize() NOEXCEPT;
		constexpr unsigned long CompletionBufferSize() NOEXCEPT;
		bool Verify(const ctsIOTask& _task, unsigned long _received_bytes) NOEXCEPT;
		RIO_BUFFERID GetRIOSendBuffer() NOEXCEPT;
		ctsIOTask GetSendCompletion() NOEXCEPT;
		ctsIOTask GetRecvCompletion() NOEXCEPT;
		ctsIOTask GetFin() NOEXCEPT;
	}

	// will never validate static buffers
	// will only validate dynamically allocated buffers
    typedef struct ctsIOPatternAllocationTypeStatic_t   ctsIOPatternAllocationTypeStatic;
    typedef struct ctsIOPatternAllocationtypeDynamic_t  ctsIOPatternAllocationtypeDynamic;

    typedef struct ctsIOPatternBufferTypeHeap_t         ctsIOPatternBufferTypeHeap;
    typedef struct ctsIOPatternBufferTypeRegisteredIo_t ctsIOPatternBufferTypeRegisteredIo;

    template <typename AllocationType, typename BufferType>
    class ctsIOPatternBufferPolicy
    {
    public:
		static constexpr ctsIOTask hard_shutdown() NOEXCEPT
		{
			ctsIOTask return_task;
			return_task.ioAction = IOTaskAction::HardShutdown;
			return_task.buffer = nullptr;
			return_task.buffer_length = 0;
			return_task.buffer_offset = 0;
			return_task.track_io = false;
			return_task.buffer_type = ctsIOTask::BufferType::Null;
			return return_task;
		}
		static constexpr ctsIOTask graceful_shutdown() NOEXCEPT
		{
			ctsIOTask return_task;
			return_task.ioAction = IOTaskAction::GracefulShutdown;
			return_task.buffer = nullptr;
			return_task.buffer_length = 0;
			return_task.buffer_offset = 0;
			return_task.track_io = false;
			return_task.buffer_type = ctsIOTask::BufferType::Null;
			return return_task;
		}

        // the final send & recv buffer count is only known by the protocol pattern
        void set_send_count(unsigned long _send_count) NOEXCEPT = 0;
        void set_recv_count(unsigned long _recv_count) NOEXCEPT = 0;

        ctsIOTask send_buffer(size_t _size) NOEXCEPT = 0;
        ctsIOTask recv_buffer(size_t _size) NOEXCEPT = 0;

		ctsIOTask send_completion() NOEXCEPT = 0;
		ctsIOTask recv_completion() NOEXCEPT = 0;
		ctsIOTask recv_fin() NOEXCEPT = 0;

        bool verify_buffer(const ctsIOTask& _task, unsigned long _received_bytes) NOEXCEPT = 0;
    };

    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationTypeStatic,
        ctsIOPatternBufferTypeHeap>
    {
    public:
        ctsIOPatternBufferPolicy() NOEXCEPT
        {
			ctsIOPatternBufferPolicyBuffers::Init();
        }
		ctsIOTask send_buffer(size_t _size) NOEXCEPT
		{

		}
		ctsIOTask recv_buffer(size_t _size) NOEXCEPT
		{

		}
		ctsIOTask send_completion() NOEXCEPT
		{
			return ctsIOPatternBufferPolicyBuffers::GetSendCompletion();
		}
		ctsIOTask recv_completion() NOEXCEPT
		{
			return ctsIOPatternBufferPolicyBuffers::GetRecvCompletion();
		}
		ctsIOTask recv_fin() NOEXCEPT
		{
			return ctsIOPatternBufferPolicyBuffers::GetFin();
		}
		bool verify_buffer(const ctsIOTask&, unsigned long) NOEXCEPT
        {
            return true;
        }
    };

    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationTypeStatic,
        ctsIOPatternBufferTypeRegisteredIo>
    {
    public:
        ctsIOPatternBufferPolicy() NOEXCEPT
        {
            ctsIOPatternBufferPolicyBuffers::Init();
        }
		~ctsIOPatternBufferPolicy() NOEXCEPT
		{
			if (completion_id != RIO_INVALID_BUFFERID) {
				ctl::ctRIODeregisterBuffer(completion_id);
			}
			if (fin_id != RIO_INVALID_BUFFERID) {
				ctl::ctRIODeregisterBuffer(fin_id);
			}
		}

		void set_send_count(unsigned long _send_count) NOEXCEPT
		{

		}
		void set_recv_count(unsigned long _send_count) NOEXCEPT
		{

		}

		ctsIOTask send_buffer(size_t _size) NOEXCEPT
		{

		}
		ctsIOTask recv_buffer(size_t _size) NOEXCEPT
		{

		}

		ctsIOTask send_completion() NOEXCEPT
		{
			ctsIOTask return_task(ctsIOPatternBufferPolicyBuffers::GetSendCompletion());

			// every RIO buffer in flight must have a unique RIO BUFFERID
			// so we can't just create a single static RIO BUFFERID to reuse
			ctl::ctFatalCondition(
				completion_id != RIO_INVALID_BUFFERID,
				L"ctsIOPatternBufferPolicy completion_id has already been used");

			completion_id = ctl::ctRIORegisterBuffer(
				return_task.buffer + return_task.buffer_offset,
				return_task.buffer_length);
			if (RIO_INVALID_BUFFERID == completion_id) {
				ctl::ctAlwaysFatalCondition(L"RIORegisterBuffer failed: %d", ::WSAGetLastError());
			}

			return_task.rio_bufferid = completion_id;
			return return_task;
        }

		ctsIOTask recv_completion() NOEXCEPT
		{
			ctsIOTask return_task(ctsIOPatternBufferPolicyBuffers::GetRecvCompletion());

			// every RIO buffer in flight must have a unique RIO BUFFERID
			// so we can't just create a single static RIO BUFFERID to reuse
			ctl::ctFatalCondition(
				completion_id != RIO_INVALID_BUFFERID,
				L"ctsIOPatternBufferPolicy completion_id has already been used");

			completion_id = ctl::ctRIORegisterBuffer(
				return_task.buffer + return_task.buffer_offset,
				return_task.buffer_length);
			if (RIO_INVALID_BUFFERID == completion_id) {
				ctl::ctAlwaysFatalCondition(L"RIORegisterBuffer failed: %d", ::WSAGetLastError());
			}

			return_task.rio_bufferid = completion_id;
			return return_task;
		}

		ctsIOTask recv_fin() NOEXCEPT
		{
			ctsIOTask return_task(ctsIOPatternBufferPolicyBuffers::GetRecvCompletion());

			// every RIO buffer in flight must have a unique RIO BUFFERID
			// so we can't just create a single static RIO BUFFERID to reuse
			ctl::ctFatalCondition(
				fin_id != RIO_INVALID_BUFFERID,
				L"ctsIOPatternBufferPolicy fin_id has already been used");

			fin_id = ctl::ctRIORegisterBuffer(
				return_task.buffer + return_task.buffer_offset,
				return_task.buffer_length);
			if (RIO_INVALID_BUFFERID == fin_id) {
				ctl::ctAlwaysFatalCondition(L"RIORegisterBuffer failed: %d", ::WSAGetLastError());
			}

			return_task.rio_bufferid = fin_id;
			return return_task;
		}

        bool verify_buffer(_In_ const ctsIOTask&, unsigned long) NOEXCEPT
        {
            return true;
        }

	private:
		RIO_BUFFERID completion_id = RIO_INVALID_BUFFERID;
		RIO_BUFFERID fin_id = RIO_INVALID_BUFFERID;

    };

    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationtypeDynamic,
        ctsIOPatternBufferTypeHeap>
    {
    public:
        ctsIOPatternBufferPolicy()
        {
			ctsIOPatternBufferPolicyBuffers::Init();
		}

        void set_send_count(unsigned long _send_count) NOEXCEPT
        {

        }
        void set_recv_count(unsigned long _send_count) NOEXCEPT
        {

        }

        ctsIOTask send_buffer(size_t _size) NOEXCEPT
        {

        }
        ctsIOTask recv_buffer(size_t _size) NOEXCEPT
        {

        }

		ctsIOTask send_completion() NOEXCEPT
		{
			return ctsIOPatternBufferPolicyBuffers::GetSendCompletion();
		}
		ctsIOTask recv_completion() NOEXCEPT
		{
			ctsIOTask return_task(ctsIOPatternBufferPolicyBuffers::GetRecvCompletion());
			return_task.buffer = recv_completion_buffer;
			return_task.buffer_length = 4;
			return_task.buffer_offset = 0;
			return return_task;
		}
		ctsIOTask recv_fin() NOEXCEPT
		{
			return ctsIOPatternBufferPolicyBuffers::GetFin();
		}

		bool verify_buffer(const ctsIOTask& _task, unsigned long _received_bytes) NOEXCEPT
        {
			return ctsIOPatternBufferPolicyBuffers::Verify(_task, _received_bytes);
		}

	private:
		char recv_completion_buffer[4];
    };

    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationtypeDynamic,
        ctsIOPatternBufferTypeRegisteredIo>
    {
    public:
        ctsIOPatternBufferPolicy()
        {
			ctsIOPatternBufferPolicyBuffers::Init();
			send_buffer_id = ctsIOPatternBufferPolicyBuffers::GetRIOSendBuffer();
		}
        
		void set_send_count(unsigned long _send_count) NOEXCEPT
        {

        }
        void set_recv_count(unsigned long _send_count) NOEXCEPT
        {

        }

        ctsIOTask send_buffer(size_t _size) NOEXCEPT
        {

        }
        ctsIOTask recv_buffer(size_t _size) NOEXCEPT
        {

        }

		bool verify_buffer(const ctsIOTask& _task, unsigned long _received_bytes) NOEXCEPT
		{
			return ctsIOPatternBufferPolicyBuffers::Verify(_task, _received_bytes);
		}

    private:
        RIO_BUFFERID send_buffer_id = RIO_INVALID_BUFFERID;
        size_t recv_pattern_offset = 0;
    };
}
