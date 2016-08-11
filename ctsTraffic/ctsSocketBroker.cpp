/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// parent header
#include "ctsSocketBroker.h"

// cpp headers
#include <exception>
#include <algorithm>
#include <memory>
#include <iterator>

// os headers
#include <Windows.h>

// ctl headers
#include <ctException.hpp>
#include <ctLocks.hpp>
#include <ctThreadPoolTimer.hpp>
#include <ctScopeGuard.hpp>

// project headers
#include "ctsConfig.h"
#include "ctsSocketState.h"



namespace ctsTraffic {

    using namespace ctl;
    using namespace std;

    ctsSocketBroker::ctsSocketBroker() :
        cs(),
        done_event(),
        socket_pool(),
        wakeup_timer(new ctThreadpoolTimer()),
        total_connections_remaining(0),
        pending_limit(0),
        pending_sockets(0),
        active_sockets(0)
    {
        if (ctsConfig::Settings->AcceptFunction) {
            // server 'accept' settings
            total_connections_remaining = ctsConfig::Settings->ServerExitLimit;
            pending_limit = ctsConfig::Settings->AcceptLimit;

        } else {
            // client 'connect' settings
            if (ctsConfig::Settings->Iterations == MAXULONGLONG) {
                total_connections_remaining = MAXULONGLONG;
            } else {
                total_connections_remaining = ctsConfig::Settings->Iterations * static_cast<ULONGLONG>(ctsConfig::Settings->ConnectionLimit);
            }
            pending_limit = ctsConfig::Settings->ConnectionLimit;
        }
        // make sure pending_limit cannot be larger than total_connections_remaining
        if (pending_limit > total_connections_remaining) {
            pending_limit = static_cast<unsigned long>(total_connections_remaining);
        }

        if (!::InitializeCriticalSectionEx(&cs, 4000, 0)) {
            throw ctException(::GetLastError(), L"InitializeCriticalSectionEx", L"ctsSocketBroker", false);
        }
        ctlScopeGuard(deleteCsOnExit, { ::DeleteCriticalSection(&this->cs); });

        // create our manual-reset notification event
        done_event.reset(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
        if (NULL == done_event.get()) {
            throw ctException(::GetLastError(), L"CreateEvent", L"ctsSocketBroker", false);
        }

		// no failures, dismiss the scope guards
        deleteCsOnExit.dismiss();
    }

    ctsSocketBroker::~ctsSocketBroker() NOEXCEPT
    {
        // first, turn off the timer to stop creating/tearing down the socket pool
        wakeup_timer.reset();

        // now delete all children, guaranteeing they stop processing
        // - must do this explicitly before deleting the CS
        //   in case they were calling back while we called detach
        socket_pool.clear();

        // now can delete the CS
        ::DeleteCriticalSection(&cs);
    }

	void ctsSocketBroker::start()
	{
		ctsConfig::PrintDebug(
			L"\t\tStarting broker: total connections remaining (%llu), pending limit (%u)\n",
			total_connections_remaining, pending_limit);

		// must always guard access to the vector
		ctAutoReleaseCriticalSection csLock(&cs);

		// only loop to pending_limit
		while (total_connections_remaining > 0 && pending_sockets < pending_limit) {
			// for outgoing connections, limit to ConnectionThrottleLimit 
			// - to prevent killing the box with DPCs with too many concurrent connect attempts
			// checking first since TimerCallback might have already established connections
			if (!ctsConfig::Settings->AcceptFunction &&
				this->pending_sockets >= ctsConfig::Settings->ConnectionThrottleLimit) {
				break;
			}

			socket_pool.push_back(make_shared<ctsSocketState>(shared_from_this()));
			(*this->socket_pool.rbegin())->start();
			++this->pending_sockets;
			--this->total_connections_remaining;
		}

		// intiate the threadpool timer
		wakeup_timer->schedule_reoccuring(
			[this]() { ctsSocketBroker::TimerCallback(this); },
			0LL,
			TimerCallbackTimeout);
	}
    //
    // SocketState is indicating the socket is now 'connected'
    // - and will be pumping IO
    // Update pending and active counts under guard
    //
    void ctsSocketBroker::initiating_io() NOEXCEPT
    {
        ctAutoReleaseCriticalSection lock_broker(&this->cs);

        ctFatalCondition(
            (this->pending_sockets == 0),
            L"ctsSocketBroker::initiating_io - About to decrement pending_sockets, but pending_sockets == 0 (active_sockets == %u)",
            this->active_sockets);

        --this->pending_sockets;
        ++this->active_sockets;
    }
    //
    // SocketState is indicating the socket is now 'closed'
    // Update pending or active counts (depending on prior state) under guard
    //
    void ctsSocketBroker::closing(bool _was_active) NOEXCEPT
    {
        ctAutoReleaseCriticalSection lock_broker(&this->cs);

        if (_was_active) {
            ctFatalCondition(
                (this->active_sockets == 0),
                L"ctsSocketBroker::closing - About to decrement active_sockets, but active_sockets == 0 (pending_sockets == %u)",
                this->pending_sockets);
            --this->active_sockets;
        } else {
            ctFatalCondition(
                (this->pending_sockets == 0),
                L"ctsSocketBroker::closing - About to decrement pending_sockets, but pending_sockets == 0 (active_sockets == %u)",
                this->active_sockets);
            --this->pending_sockets;
        }
    }

    bool ctsSocketBroker::wait(DWORD _milliseconds) NOEXCEPT
    {
        HANDLE arWait[2] = { this->done_event.get(), ctsConfig::Settings->CtrlCHandle };

        bool fReturn = false;
        switch (::WaitForMultipleObjects(2, arWait, FALSE, _milliseconds)) {
            // we are done with our sockets, or user hit ctrl'c
            // - in either case we need to tell the caller to exit
            case WAIT_OBJECT_0:
            case WAIT_OBJECT_0 + 1:
                fReturn = true;
                break;

            case WAIT_TIMEOUT:
                fReturn = false;
                break;

            case WAIT_FAILED:
                ctAlwaysFatalCondition(
                    L"ctsSocketBroker - WaitForMultipleObjects(%p) failed [%u]",
                    arWait, ::GetLastError());
        }
        return fReturn;
    }

    //
    // Timer callback to scavenge any closed sockets
    // Then refresh sockets that should be created anew
    //
    void ctsSocketBroker::TimerCallback(_In_ ctsSocketBroker* _broker) NOEXCEPT
    {
        // removed_objects will delete the closed objects outside of the broker lock
        vector<shared_ptr<ctsSocketState>> removed_objects;
        {
            if (!::TryEnterCriticalSection(&_broker->cs)) {
                return;
            }

            // refresh our pool of sockets if more sockets should be added
            try {
                //
                // Everything must occur under the broker lock
                // - touching the socket_pool
                // - touching the socket / connection counters
                //
				for (auto& socket_pool_entry : _broker->socket_pool) {
					if (ctsSocketState::InternalState::Closed == socket_pool_entry->current_state()) {
						removed_objects.push_back(socket_pool_entry);
						socket_pool_entry.reset();
					}
				}

				_broker->socket_pool.erase(
					remove(
						begin(_broker->socket_pool),
						end(_broker->socket_pool),
						nullptr),
					end(_broker->socket_pool));

                if (0 == _broker->total_connections_remaining &&
                    0 == _broker->pending_sockets &&
                    0 == _broker->active_sockets) {
                    // it's time to exit if no more work is to be done
                    ::SetEvent(_broker->done_event.get());

                } else {
                    // don't spin up more if the user asked to shutdown
                    if (WAIT_OBJECT_0 != ::WaitForSingleObject(_broker->done_event.get(), 0)) {
                        // catch up to the expected # of pended connections
                        while (_broker->pending_sockets < _broker->pending_limit && _broker->total_connections_remaining > 0) {
                            // not throttling the server accepting sockets based off total # of connections (pending + active)
                            // - only throttling total connections for outgoing connections
                            if (!ctsConfig::Settings->AcceptFunction) {
                                if ((_broker->pending_sockets + _broker->active_sockets) >= ctsConfig::Settings->ConnectionLimit) {
                                    break;
                                }
                                // throttle pending connection attempts as specified
                                if (_broker->pending_sockets >= ctsConfig::Settings->ConnectionThrottleLimit) {
                                    break;
                                }
                            }

                            _broker->socket_pool.push_back(make_shared<ctsSocketState>(_broker->shared_from_this()));
                            (*_broker->socket_pool.rbegin())->start();
                            ++_broker->pending_sockets;
                            --_broker->total_connections_remaining;
                        }
                    }
                }
            }
            catch (const exception&) {
                // if failed to create a socket will eventually reschedule
            }

            ::LeaveCriticalSection(&_broker->cs);
        }
    }

} // namespace
