/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

namespace ctsTraffic {

    typedef struct ctsIOPatternAllocationTypeStatic_t   ctsIOPatternAllocationTypeStatic;
    typedef struct ctsIOPatternAllocationtypeDynamic_t  ctsIOPatternAllocationtypeDynamic;

    typedef struct ctsIOPatternBufferTypeHeap_t         ctsIOPatternBufferTypeHeap;
    typedef struct ctsIOPatternBufferTypeRegisteredIo_t ctsIOPatternBufferTypeRegisteredIo;


    template <typename AllocationType, typename BufferType>
    class ctsIOPatternBufferPolicy
    {
    public:
        char* get_buffer(size_t _size) noexcept;
        bool  verify_buffer(const char* _buffer) noexcept;
    };


    //
    // Static heap buffers
    // - won't be verified
    //
    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationTypeStatic, 
        ctsIOPatternBufferTypeHeap> 
    {

    };

    //
    // Static RIO buffers
    // - won't be verified
    //
    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationTypeStatic,
        ctsIOPatternBufferTypeRegisteredIo>
    {

    };

    //
    // Dynamic heap buffers
    // - won't be verified
    //
    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationtypeDynamic,
        ctsIOPatternBufferTypeHeap>
    {

    };

    //
    // Static RIO buffers
    // - won't be verified
    //
    template<>
    class ctsIOPatternBufferPolicy<
        ctsIOPatternAllocationtypeDynamic,
        ctsIOPatternBufferTypeRegisteredIo>
    {

    };
}
