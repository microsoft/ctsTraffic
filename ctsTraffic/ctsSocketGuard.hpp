/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include <WinSock2.h>


namespace ctsTraffic {
    //
    // Callers should call ctsGuardSocket() in order to gain access to the SOCKET.
    // Callers then have exclusive access to the SOCKET through the returned ctsSocketGuard
    // - ctsSocketGuard's d'tor will release the lock on the socket
    // Callers are expected to hold this lock just long enough to make API calls with the SOCKET
    //
    // Callers are *not* allowed to call closesocket() with the returned SOCKET, even under a lock
    // - as doing so changes this SOCKET state outside of this container's knowledge
    // - callers must call close_socket on the ctsSocket object
    //
    // Callers may call any other method in ctsSocket with or without this lock
    //
    // ctsSocketGuard holds a const reference to the const ctsSocket& that it was given
    // - thus the ctsSocket object must out-live this ctsSocketGuard object
    // 
    template <typename T>
    class ctsSocketGuard;

    template <typename T>
    static ctsSocketGuard<T> ctsGuardSocket(const T& _t) noexcept
    {
        return ctsSocketGuard<T>(_t);
    }

    template <typename T>
    class ctsSocketGuard
    {
    public:
        // _Releases_lock_
        ~ctsSocketGuard() noexcept
        {
            // will be null if moved-from
            if (!movedFrom) {
                t->unlock_socket();
            }
        }

        // movable
        ctsSocketGuard(ctsSocketGuard&& _rvalue) noexcept
        : t(std::move(_rvalue.t)), movedFrom(false)
        {
            _rvalue.movedFrom = true;
        }
        ctsSocketGuard& operator=(ctsSocketGuard&& _rvalue) noexcept = delete;

        SOCKET get() const noexcept
        {
            return t->socket;
        }

        // no default c'tor
        // not copyable
        ctsSocketGuard() = delete;
        ctsSocketGuard(const ctsSocketGuard&) = delete;
        ctsSocketGuard& operator=(const ctsSocketGuard&) = delete;

    private:
        template <typename G>
        friend ctsSocketGuard<G> ctsGuardSocket(const G&) noexcept;

        const T& t;
        // tracking moved from by hand, as we cannot modify the const ref
        bool movedFrom;

        // private c'tor guarded by the factory function
        // _Acquires_lock_
        explicit ctsSocketGuard(const T& _t) noexcept 
        : t(_t), movedFrom(false)
        {
            t->lock_socket();
        }
    };
}
