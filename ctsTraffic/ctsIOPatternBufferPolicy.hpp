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
		bool Verify(const ctsIOTask& _task, unsigned long _current_transfer) NOEXCEPT;
		RIO_BUFFERID GetRIOSendBuffer() NOEXCEPT;
	}

    typedef struct ctsIOPatternAllocationTypeStatic_t   ctsIOPatternAllocationTypeStatic;
    typedef struct ctsIOPatternAllocationtypeDynamic_t  ctsIOPatternAllocationtypeDynamic;

    typedef struct ctsIOPatternBufferTypeHeap_t         ctsIOPatternBufferTypeHeap;
    typedef struct ctsIOPatternBufferTypeRegisteredIo_t ctsIOPatternBufferTypeRegisteredIo;

	typedef struct ctsIOPatternVerifyBufferTypeNone_t   ctsIOPatternVerifyBufferTypeNone;
	typedef struct ctsIOPatternVerifyBufferTypeRecv_t   ctsIOPatternVerifyBufferTypeRecv;

    template <typename AllocationType, typename BufferType, typename VerifyType>
    class ctsIOPatternBufferPolicy
    {
    public:
        // the final send & recv buffer count is only known by the protocol pattern
        void set_send_count(unsigned long _send_count) NOEXCEPT;
        void set_recv_count(unsigned long _recv_count) NOEXCEPT;

        ctsIOTask get_send_buffer(size_t _size) NOEXCEPT = 0;
        ctsIOTask get_recv_buffer(size_t _size) NOEXCEPT = 0;
        bool verify_buffer(const ctsIOTask& _task, unsigned long _current_transfer) NOEXCEPT = 0;
    };

    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationTypeStatic,
        ctsIOPatternBufferTypeHeap,
	    ctsIOPatternVerifyBufferTypeNone>
    {
    public:
        ctsIOPatternBufferPolicy() NOEXCEPT
        {
			ctsIOPatternBufferPolicyBuffers::Init();
        }
        void set_send_count(unsigned long _send_count) NOEXCEPT
        {

        }
        void set_recv_count(unsigned long _send_count) NOEXCEPT
        {

        }
        ctsIOTask get_send_buffer(size_t _size) NOEXCEPT
        {

        }
        ctsIOTask get_recv_buffer(size_t _size) NOEXCEPT
        {

        }
        bool verify_buffer(const ctsIOTask&, unsigned long) NOEXCEPT
        {
            return true;
        }
    };

    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationTypeStatic,
        ctsIOPatternBufferTypeRegisteredIo,
		ctsIOPatternVerifyBufferTypeNone>
    {
    public:
        ctsIOPatternBufferPolicy() NOEXCEPT
        {
            ctsIOPatternBufferPolicyBuffers::Init();
        }
        void set_send_count(unsigned long _send_count) NOEXCEPT
        {

        }
        void set_recv_count(unsigned long _send_count) NOEXCEPT
        {

        }
        ctsIOTask get_send_buffer(size_t _size) NOEXCEPT
        {

        }
        ctsIOTask get_recv_buffer(size_t _size) NOEXCEPT
        {

        }
        bool verify_buffer(_In_ const ctsIOTask&, unsigned long) NOEXCEPT
        {
            return true;
        }
    };

    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationtypeDynamic,
        ctsIOPatternBufferTypeHeap,
		ctsIOPatternVerifyBufferTypeNone>
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
        ctsIOTask get_send_buffer(size_t _size) NOEXCEPT
        {

        }
        ctsIOTask get_recv_buffer(size_t _size) NOEXCEPT
        {

        }
        bool verify_buffer(const ctsIOTask&, unsigned long) NOEXCEPT
        {
            return true;
        }
    };

	template<>
	class ctsIOPatternBufferPolicy<
		ctsIOPatternAllocationtypeDynamic,
		ctsIOPatternBufferTypeHeap,
		ctsIOPatternVerifyBufferTypeRecv>
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
		ctsIOTask get_send_buffer(size_t _size) NOEXCEPT
		{

		}
		ctsIOTask get_recv_buffer(size_t _size) NOEXCEPT
		{

		}
		bool verify_buffer(const ctsIOTask& _task, unsigned long _current_transfer) NOEXCEPT
		{
			if (_task.track_io && _task.ioAction == IOTaskAction::Recv) {
				recv_pattern_offset += _current_transfer;
				recv_pattern_offset %= ctsIOPatternBufferPolicyBuffers::BufferSize();

				return ctsIOPatternBufferPolicyBuffers::Verify(_task, _current_transfer);
			}
		}

	private:
		size_t recv_pattern_offset = 0;
	};
	
    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationtypeDynamic,
        ctsIOPatternBufferTypeRegisteredIo,
		ctsIOPatternVerifyBufferTypeNone>
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
        ctsIOTask get_send_buffer(size_t _size) NOEXCEPT
        {

        }
        ctsIOTask get_recv_buffer(size_t _size) NOEXCEPT
        {

        }
        bool verify_buffer(const ctsIOTask&, unsigned long) NOEXCEPT
        {
            return true;
        }

    private:
        RIO_BUFFERID send_buffer_id = RIO_INVALID_BUFFERID;
        size_t recv_pattern_offset = 0;
    };

	template<>
	class ctsIOPatternBufferPolicy<
		ctsIOPatternAllocationtypeDynamic,
		ctsIOPatternBufferTypeRegisteredIo,
		ctsIOPatternVerifyBufferTypeRecv>
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
		ctsIOTask get_send_buffer(size_t _size) NOEXCEPT
		{

		}
		ctsIOTask get_recv_buffer(size_t _size) NOEXCEPT
		{

		}
		bool verify_buffer(const ctsIOTask& _task, unsigned long _current_transfer) NOEXCEPT
		{
			if (_task.track_io && _task.ioAction == IOTaskAction::Recv) {
				recv_pattern_offset += _current_transfer;
				recv_pattern_offset %= ctsIOPatternBufferPolicyBuffers::BufferSize();

				return ctsIOPatternBufferPolicyBuffers::Verify(_task, _current_transfer);
			}
		}

	private:
		RIO_BUFFERID send_buffer_id = RIO_INVALID_BUFFERID;
		size_t recv_pattern_offset = 0;
	};
}
