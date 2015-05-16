/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <memory>
#include <utility>

// os headers
#include <WinSock2.h>
// ctl headers
#include <ctVersionConversion.hpp>


namespace ctsTraffic {
    //
    // Callers should call LockSocket() in order to gain access to the SOCKET.
    // Callers then have exclusive access to the SOCKET through the returned ctsSocketGuard
    // - ctsSocketGuard's d'tor will release the lock on the socket
    // Callers are expected to hold this lock just long enough to make API calls with the SOCKET
    //
    // Callers are *not* allowed to call closesocket() with the returned SOCKET, even under a lock
    // - as doing so changes this SOCKET state outside of this container's knowledge
    //
    // Callers may call any other method in ctsSocket with or without this lock
    //

    //
    // type T must implement ->lock_socket() to acquire a lock over its encapsulated SOCKET
    // type T must implement ->unlock_socket() to release the lock
    //
    template <typename T>
    class ctsSocketGuard;

    template <typename T>
    static ctsSocketGuard<T> ctsGuardSocket(T _t)
    {
        return ctsSocketGuard<T>(_t);
    }

    template <typename T>
    class ctsSocketGuard
    {
    public:
        // _Releases_lock_
        ~ctsSocketGuard() NOEXCEPT
        {
            // will be null if moved-from
            if (t) {
                t->unlock_socket();
            }
        }

        // movable
        ctsSocketGuard(ctsSocketGuard&& _rvalue) NOEXCEPT : t(std::move(_rvalue.t))
        {
            _rvalue.t = nullptr;
        }

        SOCKET get() const NOEXCEPT
        {
            return t->socket;
        }

        // no default c'tor
        // not copyable
        ctsSocketGuard() = delete;
        ctsSocketGuard(const ctsSocketGuard&) = delete;
        ctsSocketGuard& operator=(const ctsSocketGuard&) = delete;

    private:
        template <typename R>
        friend ctsSocketGuard<R> ctsGuardSocket(R);
        T t;

        // private c'tor guarded by the factory function
        // _Acquires_lock_
        template <typename R>
        ctsSocketGuard(R _r) NOEXCEPT : t(std::forward<R>(_r))
        {
            t->lock_socket();
        }
    };
}