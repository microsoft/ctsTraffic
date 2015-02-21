/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <algorithm>
#include <utility>
#include <functional>
#include <vector>
// os headers
#include <Windows.h>
// ctl headers
#include "ctVersionConversion.hpp"
#include "ctException.hpp"
#include "ctScopeGuard.hpp"
#include "ctTimer.hpp"
#include "ctLocks.hpp"


namespace ctl {

    ///
    /// not using unnamed as this is incredibly difficult to debug with Windows debuggers
    ///
    /// typedef used for the std::function to be invoked by ctThreadpoolTimerCallbackInfo
    ///  - created when the user calls schedule()
    ///
    typedef std::function<void(void)> ctThreadpoolTimerCallback_t;

    struct ctThreadpoolTimerCallbackInfo {
        ctThreadpoolTimerCallback_t callback;
        FILETIME timer_expiration;
        unsigned long reoccuring_period;

        ctThreadpoolTimerCallbackInfo() NOEXCEPT :
            callback(nullptr),
            timer_expiration(),
            reoccuring_period(0UL)
        {
            ::ZeroMemory(&timer_expiration, sizeof timer_expiration);
        }

        explicit
        ctThreadpoolTimerCallbackInfo(ctThreadpoolTimerCallback_t&& _callback, long long _milliseconds) :
            callback(std::move(_callback)),
            timer_expiration(),
            reoccuring_period(0UL)
        {
            using namespace ctl::ctTimer;
            timer_expiration = convert_msec_absolute_filetime(snap_system_time_msec() + _milliseconds);
        }
        explicit
        ctThreadpoolTimerCallbackInfo(ctThreadpoolTimerCallback_t&& _callback, long long _milliseconds, unsigned long _period) :
            callback(std::move(_callback)),
            timer_expiration(),
            reoccuring_period(_period)
        {
            using namespace ctl::ctTimer;
            timer_expiration = convert_msec_absolute_filetime(snap_system_time_msec() + _milliseconds);
        }

        // supporting only move semantics
        ctThreadpoolTimerCallbackInfo(ctThreadpoolTimerCallbackInfo&& _callback_info) NOEXCEPT
        {
            callback = std::move(_callback_info.callback);
            timer_expiration = std::move(_callback_info.timer_expiration);
            reoccuring_period = std::move(_callback_info.reoccuring_period);
        }

        // update FILETIME to the next time based off the reoccuring period
        void update_expiration() NOEXCEPT
        {
            // addition in hundredNs to avoid loss of precision if were to convert to milliseconds
            using namespace ctl::ctTimer;
            long long next_timer_hundredNs = convert_filetime_hundredNs(this->timer_expiration) + convert_msec_hundredNs(this->reoccuring_period);
            this->timer_expiration = convert_hundredNs_absolute_filetime(next_timer_hundredNs);
        }

        void swap(ctThreadpoolTimerCallbackInfo& _in) NOEXCEPT
        {
            using std::swap;
            swap(this->callback, _in.callback);
            swap(this->timer_expiration, _in.timer_expiration);
            swap(this->reoccuring_period, _in.reoccuring_period);
        }

        // non-copyable
        ctThreadpoolTimerCallbackInfo(const ctThreadpoolTimerCallbackInfo&) = delete;
        ctThreadpoolTimerCallbackInfo& operator=(const ctThreadpoolTimerCallbackInfo&) = delete;
    };


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctThreadpoolTimer
    ///
    /// class that encapsulates the new-to-Vista ThreadPool APIs around Timer usage
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctThreadpoolTimer {
    public:
        ///
        /// These c'tors can fail under low resources
        /// - ctl::ctException (from the ThreadPool APIs)
        ///
        ctThreadpoolTimer(_In_opt_ PTP_CALLBACK_ENVIRON _ptp_env = nullptr) :
            tp_environment(_ptp_env),
            timer_lock(),
            tp_timers(),
            callback_objects()
        {
            if (!::InitializeCriticalSectionEx(&timer_lock, 4000, 0)) {
                throw ctl::ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctl::ctThreadpoolTimer", false);
            }
        }
        ~ctThreadpoolTimer()
        {
            // wait for all callbacks
            ::EnterCriticalSection(&timer_lock);
            for (const auto& timer : tp_timers) {
                ::SetThreadpoolTimer(timer, nullptr, 0, 0);
            }
            ::LeaveCriticalSection(&timer_lock);

            for (const auto& timer : tp_timers) {
                ::WaitForThreadpoolTimerCallbacks(timer, TRUE);
                ::CloseThreadpoolTimer(timer);
            }

            ::DeleteCriticalSection(&timer_lock);
        }

        template <typename F>
        void schedule_singleton(F _function, long long _millisecond_offset)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            this->insert_callback_info(
                ctThreadpoolTimerCallbackInfo(
                    [_function] () -> void { _function(); },
                    _millisecond_offset));
        }
        template <typename F>
        void schedule_reoccuring(F _function, long long _millisecond_offset, unsigned long _period)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            this->insert_callback_info(
                ctThreadpoolTimerCallbackInfo(
                    [_function] () -> void { _function(); },
                    _millisecond_offset,
                    _period));
        }


        template <typename F, typename C>
        void schedule_singleton(F _function, C _function_arg, long long _millisecond_offset)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            this->insert_callback_info(
                ctThreadpoolTimerCallbackInfo(
                    [_function, _function_arg] () -> void { _function(_function_arg); },
                    _millisecond_offset));
        }
        template <typename F, typename C>
        void schedule_reoccuring(F _function, C _function_arg, long long _millisecond_offset, unsigned long _period)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            this->insert_callback_info(
                ctThreadpoolTimerCallbackInfo(
                    [_function, _function_arg] () -> void { _function(_function_arg); },
                    _millisecond_offset,
                    _period));
        }


        template <typename F, typename C1, typename C2>
        void schedule_singleton(F _function, C1 _function_arg1, C2 _function_arg2, long long _millisecond_offset)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            this->insert_callback_info(
                ctThreadpoolTimerCallbackInfo(
                    [_function, _function_arg1, _function_arg2] () -> void { _function(_function_arg1, _function_arg2); },
                    _millisecond_offset));
        }
        template <typename F, typename C1, typename C2>
        void schedule_reoccuring(F _function, C1 _function_arg1, C2 _function_arg2, long long _millisecond_offset, unsigned long _period)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            this->insert_callback_info(
                ctThreadpoolTimerCallbackInfo(
                    [_function, _function_arg1, _function_arg2] () -> void { _function(_function_arg1, _function_arg2); },
                    _millisecond_offset,
                    _period));
        }


        template <typename F, typename C1, typename C2, typename C3>
        void schedule_singleton(F _function, C1 _function_arg1, C2 _function_arg2, C3 _function_arg3, long long _millisecond_offset)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            this->insert_callback_info(
                ctThreadpoolTimerCallbackInfo(
                    [_function, _function_arg1, _function_arg2, _function_arg3] () -> void { _function(_function_arg1, _function_arg2, _function_arg3); },
                    _millisecond_offset));
        }
        template <typename F, typename C1, typename C2, typename C3>
        void schedule_reoccuring(F _function, C1 _function_arg1, C2 _function_arg2, C3 _function_arg3, long long _millisecond_offset, unsigned long _period)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            this->insert_callback_info(
                ctThreadpoolTimerCallbackInfo(
                    [_function, _function_arg1, _function_arg2, _function_arg3] () -> void { _function(_function_arg1, _function_arg2, _function_arg3); },
                    _millisecond_offset,
                    _period));
        }


        ///
        /// No copy c'tors
        ///
        ctThreadpoolTimer(const ctThreadpoolTimer&) = delete;
        ctThreadpoolTimer& operator=(const ctThreadpoolTimer&) = delete;

    private:
        ///
        /// Private members
        ///
        CRITICAL_SECTION timer_lock;
        PTP_CALLBACK_ENVIRON tp_environment;
        std::vector<PTP_TIMER> tp_timers;
        std::vector<ctThreadpoolTimerCallbackInfo> callback_objects;

        PTP_TIMER create_tp()
        {
            PTP_TIMER ptp_timer = ::CreateThreadpoolTimer(ctThreadPoolTimerCallback, this, this->tp_environment);
            if (nullptr == ptp_timer) {
                throw ctl::ctException(::GetLastError(), L"CreateThreadpoolTimer", L"ctl::ctThreadpoolTimer", false);
            }
            return ptp_timer;
        }
        ///
        /// must insert the callback info sorted based of the expected time to complete
        ///
        void insert_callback_info(ctThreadpoolTimerCallbackInfo&& _new_request)
        {
            ctl::ctAutoReleaseCriticalSection lock_timer(&this->timer_lock);

            // compare each callback_object to check if it contains a null function ptr
            auto unused_callback = std::find_if(
                std::begin(this->callback_objects),
                std::end(this->callback_objects),
                [] (const ctThreadpoolTimerCallbackInfo& _info) {
                // returns if a null callback (not being used)
                return !static_cast<bool>(_info.callback);
            });

            if (unused_callback == std::end(this->callback_objects)) {
                //
                // need room in both the callback_objects && tp_timers vector for a new timer
                //
                this->callback_objects.emplace_back(std::move(_new_request));
                // ensure this is exception safe with a scope guard
                ctlScopeGuard(removeCallbackObjectOnFailure, { this->callback_objects.pop_back(); });

                PTP_TIMER temp_timer = this->create_tp();
                // ensure the timer is closed (is exception safe) with a scope guard
                ctlScopeGuard(deleteTemporaryTimerOnFailure, { ::CloseThreadpoolTimer(temp_timer); });

                // now attempt to store the new timer
                this->tp_timers.push_back(temp_timer);

                // all succeeded : dismiss the scope guards
                deleteTemporaryTimerOnFailure.dismiss();
                removeCallbackObjectOnFailure.dismiss();

                unused_callback = this->callback_objects.end() - 1;

            } else {
                // if we found an empty slot, swap in the user's new object
                unused_callback->swap(_new_request);
            }

            // using iterator_offect to directly express the relationship between tp_timers and callback_objects
            // - each of the vector offsets are functionally paired together
            auto iterator_offset = unused_callback - this->callback_objects.begin();
            ::SetThreadpoolTimer(
                this->tp_timers[iterator_offset],
                &this->callback_objects[iterator_offset].timer_expiration,
                this->callback_objects[iterator_offset].reoccuring_period,
                0); // specifying window length of zero for now: not a need to be less precise -> more power efficient yet
        }

        static void CALLBACK ctThreadPoolTimerCallback(
          PTP_CALLBACK_INSTANCE /*_instance*/,
          PVOID _context,
          PTP_TIMER _timer)
        {
            ctThreadpoolTimer* this_ptr = reinterpret_cast<ctThreadpoolTimer*>(_context);
            // save off the functor to invoke outside the lock
            ctThreadpoolTimerCallback_t functor;

            // scope for the CS lock
            {
                ctl::ctAutoReleaseCriticalSection lock_timer(&this_ptr->timer_lock);
                // find the timer that was fired to run its callback
                auto found_timer = std::find_if(
                    std::begin(this_ptr->tp_timers),
                    std::end(this_ptr->tp_timers),
                    [_timer] (PTP_TIMER _callback_timer) {
                    // returns if a null callback (not being used)
                    return (_timer == _callback_timer);
                });
                ctl::ctFatalCondition(
                    found_timer == std::end(this_ptr->tp_timers),
                    L"ctThreadPoolTimer - failed to find the PTP_TIMER (%p) which initiated this timer callback (ctl::ctThreadPoolTimer %p)",
                    _timer, this_ptr);

                auto iterator_offset = found_timer - this_ptr->tp_timers.begin();
                functor = this_ptr->callback_objects[iterator_offset].callback;

                if (0 == this_ptr->callback_objects[iterator_offset].reoccuring_period) {
                    // clear the internal callback structure
                    ctThreadpoolTimerCallbackInfo empty;
                    this_ptr->callback_objects[iterator_offset].swap(empty);

                } else {
                    // recalculate the next time this scheduled event will fire
                    this_ptr->callback_objects[iterator_offset].update_expiration();
                }
            }

            // now run the user's callback
            functor();
        }
    };

} // namespace

