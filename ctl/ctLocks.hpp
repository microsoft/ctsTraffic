/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include <windows.h>
// ctl headers
#include "ctVersionConversion.hpp"
#include "ctException.hpp"


namespace ctl {

    ///
    /// Pure RAII tracking the state of this CS
    /// - notice there are no methods to explicitly control entering/leaving the CS
    ///
    class ctAutoReleaseCriticalSection {
    public:
        _Acquires_lock_(*this->cs)
        _Post_same_lock_(*_cs, *this->cs)
        explicit ctAutoReleaseCriticalSection(_In_ CRITICAL_SECTION* _cs) NOEXCEPT :
            cs(_cs)
        {
            ::EnterCriticalSection(this->cs);
        }

        _Releases_lock_(*this->cs)
        ~ctAutoReleaseCriticalSection() NOEXCEPT
        {
            ::LeaveCriticalSection(this->cs);
        }

        /// no default c'tor
        ctAutoReleaseCriticalSection() = delete;
        /// non-copyable
        ctAutoReleaseCriticalSection(const ctAutoReleaseCriticalSection&) = delete;
        ctAutoReleaseCriticalSection operator=(const ctAutoReleaseCriticalSection&) = delete;

    private:
        CRITICAL_SECTION* cs;
    };


    class ctPrioritizedCriticalSection {
    public:
        ctPrioritizedCriticalSection() NOEXCEPT
        {
            ::InitializeSRWLock(&srwlock);
            if (!::InitializeCriticalSectionEx(&cs, 4000, 0)) {
                ctAlwaysFatalCondition(
                    L"ctPrioritizedCriticalSection: InitializeCriticalSectionEx failed [%u]",
                    ::GetLastError());
            }
        }
        ~ctPrioritizedCriticalSection() NOEXCEPT
        {
            ::DeleteCriticalSection(&cs);
        }

        // taking an *exclusive* lock to interrupt the *shared* lock taken by the deque IO path
        // - we want to interrupt the IO path so we can initiate more IO if we need to grow the CQ
        _Acquires_exclusive_lock_(this->srwlock)
        _Acquires_lock_(this->cs)
        void priority_lock() NOEXCEPT
        {
            ::AcquireSRWLockExclusive(&srwlock);
            ::EnterCriticalSection(&cs);
        }
        _Releases_lock_(this->cs)
        _Releases_exclusive_lock_(this->srwlock)
        void priority_release() NOEXCEPT
        {
            ::LeaveCriticalSection(&cs);
            ::ReleaseSRWLockExclusive(&srwlock);
        }

        _Acquires_shared_lock_(this->srwlock)
        _Acquires_lock_(this->cs)
        void default_lock() NOEXCEPT
        {
            ::AcquireSRWLockShared(&srwlock);
            ::EnterCriticalSection(&cs);
        }
        _Releases_lock_(this->cs)
        _Releases_shared_lock_(this->srwlock)
        void default_release() NOEXCEPT
        {
            ::LeaveCriticalSection(&cs);
            ::ReleaseSRWLockShared(&srwlock);
        }

        /// not copyable
        ctPrioritizedCriticalSection(const ctPrioritizedCriticalSection&) = delete;
        ctPrioritizedCriticalSection& operator=(const ctPrioritizedCriticalSection&) = delete;

    private:
        SRWLOCK srwlock;
        CRITICAL_SECTION cs;
    };
    class ctAutoReleasePriorityCriticalSection {
    public:
        explicit ctAutoReleasePriorityCriticalSection(ctPrioritizedCriticalSection& _priority_cs) NOEXCEPT :
            prioritized_cs(_priority_cs)
        {
            prioritized_cs.priority_lock();
        }

        ~ctAutoReleasePriorityCriticalSection() NOEXCEPT
        {
            prioritized_cs.priority_release();
        }

        /// no default c'tor
        ctAutoReleasePriorityCriticalSection() = delete;
        /// non-copyable
        ctAutoReleasePriorityCriticalSection(const ctAutoReleasePriorityCriticalSection&) = delete;
        ctAutoReleasePriorityCriticalSection operator=(const ctAutoReleasePriorityCriticalSection&) = delete;

    private:
        ctPrioritizedCriticalSection& prioritized_cs;
    };
    class ctAutoReleaseDefaultCriticalSection {
    public:
        explicit ctAutoReleaseDefaultCriticalSection(ctPrioritizedCriticalSection& _priority_cs) NOEXCEPT :
            prioritized_cs(_priority_cs)
        {
            prioritized_cs.default_lock();
        }

        ~ctAutoReleaseDefaultCriticalSection() NOEXCEPT
        {
            prioritized_cs.default_release();
        }

        /// no default c'tor
        ctAutoReleaseDefaultCriticalSection() = delete;
        /// non-copyable
        ctAutoReleaseDefaultCriticalSection(const ctAutoReleaseDefaultCriticalSection&) = delete;
        ctAutoReleaseDefaultCriticalSection operator=(const ctAutoReleaseDefaultCriticalSection&) = delete;

    private:
        ctPrioritizedCriticalSection& prioritized_cs;
    };

    //////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Can concurrent-safely read from both const and non-const
    ///  long long * 
    ///  long *
    ///
    //////////////////////////////////////////////////////////////////////////////////////////
    inline long long ctMemoryGuardRead(_In_ const long long* _original_value) NOEXCEPT
    {
        return ::InterlockedCompareExchange64(const_cast<volatile long long*>(_original_value), 0LL, 0LL);
    }
    inline long ctMemoryGuardRead(_In_ const long* _original_value) NOEXCEPT
    {
        return ::InterlockedCompareExchange(const_cast<volatile long*>(_original_value), 0LL, 0LL);
    }
    inline long long ctMemoryGuardRead(_In_ long long* _original_value) NOEXCEPT
    {
        return ::InterlockedCompareExchange64(const_cast<volatile long long*>(_original_value), 0LL, 0LL);
    }
    inline long ctMemoryGuardRead(_In_ long* _original_value) NOEXCEPT
    {
        return ::InterlockedCompareExchange(const_cast<volatile long*>(_original_value), 0LL, 0LL);
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Can concurrent-safely update a long long or long value
    /// - *Write returns the *prior* value
    /// - *WriteConditionally returns the *prior* value
    /// - *Add returns the *prior* value
    /// - *Subtract returns the *prior* value
    ///   (Note subtraction is just the adding a negative long value)
    ///
    /// - *Increment returns the *new* value
    /// - *Decrement returns the *new* value
    ///
    //////////////////////////////////////////////////////////////////////////////////////////
    inline long long ctMemoryGuardWrite(_Inout_ long long* _original_value, long long _new_value) NOEXCEPT
    {
        return ::InterlockedExchange64(_original_value, _new_value);
    }
    inline long ctMemoryGuardWrite(_Inout_ long* _original_value, long _new_value) NOEXCEPT
    {
        return ::InterlockedExchange(_original_value, _new_value);
    }

    inline long long ctMemoryGuardWriteConditionally(_Inout_ long long* _original_value, long long _new_value, long long _if_equals) NOEXCEPT
    {
        return ::InterlockedCompareExchange64(_original_value, _new_value, _if_equals);
    }
    inline long ctMemoryGuardWriteConditionally(_Inout_ long* _original_value, long _new_value, long _if_equals) NOEXCEPT
    {
        return ::InterlockedCompareExchange(_original_value, _new_value, _if_equals);
    }

    inline long long ctMemoryGuardAdd(_Inout_ long long* _original_value, long long _add_value) NOEXCEPT
    {
        return ::InterlockedExchangeAdd64(_original_value, _add_value);
    }
    inline long ctMemoryGuardAdd(_Inout_ long* _original_value, long _add_value) NOEXCEPT
    {
        return ::InterlockedExchangeAdd(_original_value, _add_value);
    }

    inline long long ctMemoryGuardSubtract(_Inout_ long long* _original_value, long long _subtract_value) NOEXCEPT
    {
        return ::InterlockedExchangeAdd64(_original_value, _subtract_value * -1LL);
    }
    inline long ctMemoryGuardSubtract(_Inout_ long* _original_value, long _subtract_value) NOEXCEPT
    {
        return ::InterlockedExchangeAdd(_original_value, _subtract_value * -1L);
    }

    inline long long ctMemoryGuardIncrement(_Inout_ long long* _original_value) NOEXCEPT
    {
        return ::InterlockedIncrement64(_original_value);
    }
    inline long ctMemoryGuardIncrement(_Inout_ long* _original_value) NOEXCEPT
    {
        return ::InterlockedIncrement(_original_value);
    }

    inline long long ctMemoryGuardDecrement(_Inout_ long long* _original_value) NOEXCEPT
    {
        return ::InterlockedDecrement64(_original_value);
    }
    inline long ctMemoryGuardDecrement(_Inout_ long* _original_value) NOEXCEPT
    {
        return ::InterlockedDecrement(_original_value);
    }

} // namespace
