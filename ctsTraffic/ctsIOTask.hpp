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
#include <winsock2.h>
#include <mswsock.h>

// ** NOTE ** should not include any local project cts headers - to avoid circular references

namespace ctsTraffic {

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// The ctsIOTask struct instructs the caller on what action to perform
    /// - and provides it the buffer it should use to send/recv data
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    struct ctsIOTask {
        ctsIOTask() throw()
        : ioAction(None),
          buffer(nullptr),
          buffer_length(0),
          buffer_offset(0),
          expected_pattern_offset(0),
          time_offset_milliseconds(0LL),
          rio_bufferid(RIO_INVALID_BUFFERID),
          tracked_io(false),
          unlisted_buffer(false)
        {
        }

        enum IOAction {
            Send,
            Recv,
            None,
            Abort,
            FatalAbort
        } ioAction;

        _Field_size_full_(buffer_length)
        char* buffer;
        unsigned long buffer_length;
        unsigned long buffer_offset;
        unsigned long expected_pattern_offset;
        long long time_offset_milliseconds;
        RIO_BUFFERID rio_bufferid;

        //
        // boolean values are internal to ctsIOPattern
        //

        // (internal) flag if this IO request is tracked with inflight counters
        bool tracked_io;
        // (internal) flag if this is a non-listed-buffer (meaning the base class isn't containing it)
        bool unlisted_buffer;
    };

} // namespace