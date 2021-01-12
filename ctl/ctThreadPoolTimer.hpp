/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// ReSharper disable CppInconsistentNaming
#pragma once

// cpp headers
#include <algorithm>
#include <utility>
#include <functional>
#include <vector>
// os headers
#include <Windows.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include "ctTimer.hpp"


namespace ctl
{
    namespace Details
    {
        struct ctThreadpoolTimerCallbackInfo
        {
            std::function<void()> Callback;
            FILETIME TimerExpiration{ 0, 0 };
            unsigned long ReoccuringPeriod = 0;

            ctThreadpoolTimerCallbackInfo() = default;
            ~ctThreadpoolTimerCallbackInfo() = default;
            // non-copyable
            ctThreadpoolTimerCallbackInfo(const ctThreadpoolTimerCallbackInfo&) = delete;
            ctThreadpoolTimerCallbackInfo& operator=(const ctThreadpoolTimerCallbackInfo&) = delete;

            explicit ctThreadpoolTimerCallbackInfo(std::function<void()>&& callback, long long milliseconds, unsigned long period) noexcept :
                Callback(std::move(callback)),
                ReoccuringPeriod(period)
            {
                using namespace ctTimer;
                TimerExpiration = ConvertMillisToAbsoluteFiletime(SnapSystemTimeInMillis() + milliseconds);
            }

            // supporting only move semantics
            ctThreadpoolTimerCallbackInfo(ctThreadpoolTimerCallbackInfo&& callback_info) noexcept
            {
                Callback = std::move(callback_info.Callback);
                TimerExpiration = callback_info.TimerExpiration;
                ReoccuringPeriod = callback_info.ReoccuringPeriod;
            }
            ctThreadpoolTimerCallbackInfo& operator=(ctThreadpoolTimerCallbackInfo&& callback_info) noexcept
            {
                Callback = std::move(callback_info.Callback);
                TimerExpiration = callback_info.TimerExpiration;
                ReoccuringPeriod = callback_info.ReoccuringPeriod;
                return *this;
            }

            // update FILETIME to the next time based off the reoccuring period
            void update_expiration() noexcept
            {
                // addition in hundredNs to avoid loss of precision if were to convert to milliseconds
                using namespace ctTimer;
                const auto nextTimerHundredNs = ConvertFiletimeToHundredNs(TimerExpiration) + ConvertMillisToHundredNs(ReoccuringPeriod);
                TimerExpiration = ConvertHundredNsToAbsoluteFiletime(nextTimerHundredNs);
            }

            void swap(ctThreadpoolTimerCallbackInfo& rhs) noexcept
            {
                using std::swap;
                swap(this->Callback, rhs.Callback);
                swap(this->TimerExpiration, rhs.TimerExpiration);
                swap(this->ReoccuringPeriod, rhs.ReoccuringPeriod);
            }
        };
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctThreadpoolTimer
    ///
    /// class that encapsulates the new-to-Vista ThreadPool APIs around Timer usage
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctThreadpoolTimer
    {
    public:
        explicit ctThreadpoolTimer(_In_opt_ const PTP_CALLBACK_ENVIRON ptp_env = nullptr) noexcept :  // NOLINT(misc-misplaced-const)
            m_tpEnvironment(ptp_env)
        {
        }

        ~ctThreadpoolTimer() noexcept
        {
            // wait for all callbacks
            auto lock_timer = m_timerLock.lock();
            // block any more items being scheduled
            m_exiting = true;
            lock_timer.reset();
            stop_all_timers();

            for (const auto& timer : m_tpTimers)
            {
                CloseThreadpoolTimer(timer);
            }
        }

        // non-copyable
        ctThreadpoolTimer(const ctThreadpoolTimer&) = delete;
        ctThreadpoolTimer& operator=(const ctThreadpoolTimer&) = delete;
        ctThreadpoolTimer(ctThreadpoolTimer&&) = delete;
        ctThreadpoolTimer& operator=(ctThreadpoolTimer&&) = delete;

        void schedule_reoccuring(std::function<void()> function, long long millisecond_offset, unsigned long period)
        {
            // capture the caller's context in a lambda to be invoked in the callback
            this->insert_callback_info(
                Details::ctThreadpoolTimerCallbackInfo(
                    std::move(function),
                    millisecond_offset,
                    period));
        }

        void stop_all_timers() noexcept
        {
            auto lock_timer = m_timerLock.lock();
            for (const auto& timer : m_tpTimers)
            {
                SetThreadpoolTimer(timer, nullptr, 0, 0);
            }
            lock_timer.reset();

            for (const auto& timer : m_tpTimers)
            {
                WaitForThreadpoolTimerCallbacks(timer, TRUE);
            }
        }

    private:
        //
        // Private members
        //
        wil::critical_section m_timerLock{500};
        const PTP_CALLBACK_ENVIRON m_tpEnvironment = nullptr;  // NOLINT(misc-misplaced-const)
        std::vector<PTP_TIMER> m_tpTimers;
        std::vector<Details::ctThreadpoolTimerCallbackInfo> m_callbackObjects;
        bool m_exiting = false;

        PTP_TIMER create_tp()
        {
            const PTP_TIMER ptp_timer = CreateThreadpoolTimer(ThreadPoolTimerCallback, this, m_tpEnvironment); // NOLINT(misc-misplaced-const)
            if (!ptp_timer)
            {
                THROW_WIN32_MSG(GetLastError(), "CreateThreadpoolTimer");
            }
            return ptp_timer;
        }

        //
        // must insert the callback info sorted based of the expected time to complete
        //
        void insert_callback_info(Details::ctThreadpoolTimerCallbackInfo&& new_request)
        {
            const auto lock_timer = m_timerLock.lock();
            if (m_exiting)
            {
                return;
            }

            // compare each callback_object to check if it contains a null function ptr
            auto unused_callback = std::find_if(
                std::begin(m_callbackObjects),
                std::end(m_callbackObjects),
                [](const Details::ctThreadpoolTimerCallbackInfo& info) noexcept {
                    // returns if a null callback (not being used)
                    return !static_cast<bool>(info.Callback);
                });

            if (unused_callback == std::end(m_callbackObjects))
            {
                //
                // need room in both the callback_objects && tp_timers vector for a new timer
                //
                m_callbackObjects.emplace_back(std::move(new_request));
                // ensure this is exception safe with a scope guard
                auto removeCallbackObjectOnFailure = wil::scope_exit([&]() noexcept { m_callbackObjects.pop_back(); });

                PTP_TIMER temp_timer = this->create_tp();
                // ensure the timer is closed (is exception safe) with a scope guard
                auto deleteTemporaryTimerOnFailure = wil::scope_exit([&]() noexcept { CloseThreadpoolTimer(temp_timer); });

                // now attempt to store the new timer
                m_tpTimers.push_back(temp_timer);

                // all succeeded : dismiss the scope guards
                deleteTemporaryTimerOnFailure.release();
                removeCallbackObjectOnFailure.release();

                unused_callback = m_callbackObjects.end() - 1;
            }
            else
            {
                // if we found an empty slot, swap in the user's new object
                unused_callback->swap(new_request);
            }

            // using iterator_offect to directly express the relationship between tp_timers and callback_objects
            // - each of the vector offsets are functionally paired together
            const auto iterator_offset = unused_callback - m_callbackObjects.begin();
            SetThreadpoolTimer(
                m_tpTimers[iterator_offset],
                &m_callbackObjects[iterator_offset].TimerExpiration,
                m_callbackObjects[iterator_offset].ReoccuringPeriod,
                0); // specifying window length of zero for now: not a need to be less precise -> more power efficient yet
        }

        static void CALLBACK ThreadPoolTimerCallback(
            PTP_CALLBACK_INSTANCE,
            PVOID context,
            PTP_TIMER timer) noexcept
            try
        {
            auto* this_ptr = static_cast<ctThreadpoolTimer*>(context);

            // save off the functor to invoke outside the lock
            std::function<void()> functor;

            // scope for the CS lock
            {
                const auto lock_timer = this_ptr->m_timerLock.lock();
                if (this_ptr->m_exiting)
                {
                    return;
                }


                // find the timer that was fired to run its callback
                const auto found_timer = std::find_if(
                    std::begin(this_ptr->m_tpTimers),
                    std::end(this_ptr->m_tpTimers),
                    [timer](PTP_TIMER callback_timer) noexcept {
                        // returns if a null callback (not being used)
                        return timer == callback_timer;
                    });
                FAIL_FAST_IF_MSG(
                    found_timer == std::end(this_ptr->m_tpTimers),
                    "ctThreadPoolTimer - failed to find the PTP_TIMER (%p) which initiated this timer callback (ctl::ctThreadPoolTimer %p)",
                    timer, this_ptr);

                const auto iterator_offset = found_timer - this_ptr->m_tpTimers.begin();
                functor = this_ptr->m_callbackObjects[iterator_offset].Callback;

                // recalculate the next time this scheduled event will fire
                this_ptr->m_callbackObjects[iterator_offset].update_expiration();
            }

            // now run the user's callback outside the internal lock
            functor();
        }
        catch (...)
        {
        }
    };
} // namespace
