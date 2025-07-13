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
#include <atomic>
#include <cstring>
// os headers
#include <Windows.h>
#include <rpc.h>
// ctl headers
#include <ctTimer.hpp>
// wil headers always included last
#include <wil/resource.h>

namespace ctsTraffic {
	namespace ctsStatistics
	{
		inline void __stdcall UniqueAnyRpcStringFree(_Pre_opt_valid_ _Frees_ptr_opt_ RPC_CSTR str)
		{
			::RpcStringFreeA(&str);
		}
		using unique_rpc_cstr = wil::unique_any<RPC_CSTR, decltype(&UniqueAnyRpcStringFree), UniqueAnyRpcStringFree>;

		constexpr uint32_t ConnectionIdLength = 36 + 1; // UUID strings are 36 chars

		template <typename T>
		void GenerateConnectionId(_In_ T& statisticsObject)
		{
			UUID connectionId;
			RPC_STATUS status = UuidCreate(&connectionId);
			if (status != RPC_S_OK)
			{
				THROW_WIN32_MSG(status, "UuidCreate (ctsStatistics)");
			}

			unique_rpc_cstr connectionIdString;
			status = UuidToStringA(&connectionId, &connectionIdString);
			if (status != RPC_S_OK)
			{
				THROW_WIN32_MSG(status, "UuidToStringA (ctsStatistics)");
			}
			FAIL_FAST_IF_MSG(
				// ReSharper disable once CppRedundantParentheses
				strlen(reinterpret_cast<const char*>(connectionIdString.get())) != (ConnectionIdLength - 1),
				"UuidToString returned a string not 36 characters long (%zu)",
				strlen(reinterpret_cast<const char*>(connectionIdString.get())));

			const auto copyError = ::memcpy_s(statisticsObject.m_connectionIdentifier, ConnectionIdLength, connectionIdString.get(), ConnectionIdLength);
			FAIL_FAST_IF_MSG(
				copyError != 0,
				"memcpy_s failed trying to copy a UUID string (%d)", copyError);

			statisticsObject.m_connectionIdentifier[ConnectionIdLength - 1] = '\0';
		}

		template <typename T>
		void Start(_In_ T& statisticsObject) noexcept
		{
			// only calculate the QPC the first time
			// - willing to take the cost of 2 interlocked operations the first time this is initialized
			//   versus taking a QPC hit on every IO request
			if (0LL == statisticsObject.start_time.get())
			{
				statisticsObject.m_startTime.set_conditionally(ctl::ctTimer::snap_qpc_as_msec(), 0LL);
			}
		}

		template <typename T>
		void End(_In_ T& statisticsObject) noexcept
		{
			statisticsObject.m_endTime.set_conditionally(ctl::ctTimer::snap_qpc_as_msec(), 0LL);
		}
	}

	struct ctsStatsTracking
	{
	private:
#if INTPTR_MAX == INT64_MAX
		// 64-bit builds can use atomic<intptr_t>
		std::atomic_signed_lock_free m_currentValue{};
		std::atomic_signed_lock_free m_previousValue{};
#else
		std::atomic<int64_t> m_currentValue{};
		std::atomic<int64_t> m_previousValue{};
#endif
	public:
		ctsStatsTracking() noexcept = default;

		explicit ctsStatsTracking(int64_t initial_value) noexcept :
			m_currentValue{ initial_value },
			m_previousValue{ initial_value }
		{
		}

		~ctsStatsTracking() noexcept = default;

		ctsStatsTracking(const ctsStatsTracking& in) noexcept :
			m_currentValue(in.m_currentValue.load()),
			m_previousValue(in.m_previousValue.load())
		{
		}

		ctsStatsTracking(ctsStatsTracking&& in) noexcept :
			m_currentValue(in.m_currentValue.load()),
			m_previousValue(in.m_previousValue.load())
		{
		}

		// not allowing assignment operator - must be explicit
		ctsStatsTracking& operator=(const ctsStatsTracking&) = delete;
		ctsStatsTracking& operator=(ctsStatsTracking&&) = delete;

		[[nodiscard]] int64_t GetValue() const noexcept
		{
			return m_currentValue;
		}

		void SetValue(int64_t new_value) noexcept
		{
			m_currentValue.store(new_value);
		}

		void SetConditionally(int64_t new_value, int64_t if_equals) noexcept
		{
			m_currentValue.compare_exchange_strong(if_equals, new_value);
		}

		void Increment() noexcept
		{
			Add(1);
		}

		void Decrement() noexcept
		{
			Subtract(1);
		}

		//
		// Adds the [in] value to the current value, returning the original value
		//
		void Add(int64_t value) noexcept
		{
			m_currentValue.fetch_add(value);
		}

		//
		// Subtracts the [in] value from the current value
		//
		void Subtract(int64_t value) noexcept
		{
			m_currentValue.fetch_sub(value);
		}

		//
		// Get / Sets a new value to the 'previous' value, returning the prior 'previous' value
		//
		[[nodiscard]] int64_t GetPriorValue() noexcept
		{
			return m_previousValue;
		}

		[[nodiscard]] int64_t SetPriorValue(int64_t new_value) noexcept
		{
			return m_previousValue.exchange(new_value);
		}

		//
		// Updates the previous value with the current value
		// - returning the difference (current_value - previous_value)
		//
		[[nodiscard]] int64_t SnapValueDifference() noexcept
		{
			const auto captureCurrentValue = m_currentValue.load();
			const auto capturePriorValue = m_previousValue.exchange(captureCurrentValue);
			return captureCurrentValue - capturePriorValue;
		}

		//
		// Returns the difference (current_value - previous_value)
		// - without modifying either value
		//
		[[nodiscard]] int64_t ReadValueDifference() const noexcept
		{
			return m_currentValue.load() - m_previousValue.load();
		}
	};


	struct ctsConnectionStatistics
	{
		ctsStatsTracking m_startTime;
		ctsStatsTracking m_endTime;
		ctsStatsTracking m_activeConnectionCount;
		ctsStatsTracking m_successfulCompletionCount;
		ctsStatsTracking m_connectionErrorCount;
		ctsStatsTracking m_protocolErrorCount;

		explicit ctsConnectionStatistics(int64_t start_time = 0LL) noexcept :
			m_startTime(start_time)
		{
		}

		~ctsConnectionStatistics() noexcept = default;
		ctsConnectionStatistics(const ctsConnectionStatistics&) = default;
		ctsConnectionStatistics(ctsConnectionStatistics&&) = default;
		// not implementing the assignment operator
		// only implementing the copy constructor (due to maintaining memory barriers)
		ctsConnectionStatistics& operator=(const ctsConnectionStatistics&) = delete;
		ctsConnectionStatistics& operator=(ctsConnectionStatistics&&) = delete;

		//
		// snap_view() will return a statistics object capturing the current values
		// - resetting only the start_time value if the _In_ bool is true
		// - not resetting the other values even when _clear_settings == true since
		//   connection values in status messages always display the aggregate values
		//   (not displaying only changes in connection settings over each time slice)
		//
		ctsConnectionStatistics SnapView(bool clear_settings) noexcept
		{
			const int64_t currentTime = ctl::ctTimer::snap_qpc_as_msec();
			const int64_t priorTimeRead = clear_settings ?
				m_startTime.SetPriorValue(currentTime) :
				m_startTime.GetPriorValue();

			ctsConnectionStatistics returnStats(priorTimeRead);
			returnStats.m_endTime.SetValue(currentTime);

			returnStats.m_activeConnectionCount.SetValue(m_activeConnectionCount.GetValue());
			returnStats.m_successfulCompletionCount.SetValue(m_successfulCompletionCount.GetValue());
			returnStats.m_connectionErrorCount.SetValue(m_connectionErrorCount.GetValue());
			returnStats.m_protocolErrorCount.SetValue(m_protocolErrorCount.GetValue());

			return returnStats;
		}
	};

	struct ctsUdpStatistics
	{
		ctsStatsTracking m_startTime;
		ctsStatsTracking m_endTime;
		ctsStatsTracking m_bitsReceived;
		ctsStatsTracking m_successfulFrames;
		ctsStatsTracking m_droppedFrames;
		ctsStatsTracking m_duplicateFrames;
		ctsStatsTracking m_errorFrames;
		// unique connection identifier
		char m_connectionIdentifier[ctsStatistics::ConnectionIdLength]{};

		explicit ctsUdpStatistics(int64_t start_time = 0LL) noexcept :
			m_startTime(start_time)
		{
			m_connectionIdentifier[0] = '\0';
		}

		~ctsUdpStatistics() noexcept = default;

		ctsUdpStatistics(const ctsUdpStatistics&) noexcept = default;
		ctsUdpStatistics(ctsUdpStatistics&&) noexcept = default;

		ctsUdpStatistics& operator=(const ctsUdpStatistics&) = delete;
		ctsUdpStatistics& operator=(ctsUdpStatistics&&) = delete;

		// currently only called by the UDP client - only tracking the receives
		[[nodiscard]] int64_t GetBytesTransferred() const noexcept
		{
			return m_bitsReceived.GetValue() / 8;
		}

		//
		// snap-view will set the returned start time == last read time to capture the delta
		//
		ctsUdpStatistics SnapView(bool clear_settings) noexcept
		{
			const int64_t currentTime = ctl::ctTimer::snap_qpc_as_msec();
			const int64_t priorTimeRead = clear_settings ?
				m_startTime.SetPriorValue(currentTime) :
				m_startTime.GetPriorValue();

			ctsUdpStatistics returnStats(priorTimeRead);
			returnStats.m_endTime.SetValue(currentTime);

			if (clear_settings)
			{
				returnStats.m_bitsReceived.SetValue(m_bitsReceived.SnapValueDifference());
				returnStats.m_successfulFrames.SetValue(m_successfulFrames.SnapValueDifference());
				returnStats.m_droppedFrames.SetValue(m_droppedFrames.SnapValueDifference());
				returnStats.m_duplicateFrames.SetValue(m_duplicateFrames.SnapValueDifference());
				returnStats.m_errorFrames.SetValue(m_errorFrames.SnapValueDifference());
			}
			else
			{
				returnStats.m_bitsReceived.SetValue(m_bitsReceived.ReadValueDifference());
				returnStats.m_successfulFrames.SetValue(m_successfulFrames.ReadValueDifference());
				returnStats.m_droppedFrames.SetValue(m_droppedFrames.ReadValueDifference());
				returnStats.m_duplicateFrames.SetValue(m_duplicateFrames.ReadValueDifference());
				returnStats.m_errorFrames.SetValue(m_errorFrames.ReadValueDifference());
			}

			return returnStats;
		}
	};

	struct ctsTcpStatistics
	{
		ctsStatsTracking m_startTime;
		ctsStatsTracking m_endTime;
		ctsStatsTracking m_bytesSent;
		ctsStatsTracking m_bytesRecv;
		// unique connection identifier
		char m_connectionIdentifier[ctsStatistics::ConnectionIdLength]{};

		explicit ctsTcpStatistics(int64_t current_time = 0LL) noexcept :
			m_startTime(current_time)
		{
			static const auto* nullGuidString = "00000000-0000-0000-0000-000000000000";
			strcpy_s(
				m_connectionIdentifier,
				nullGuidString);
		}

		~ctsTcpStatistics() noexcept = default;

		ctsTcpStatistics(const ctsTcpStatistics&) noexcept = default;
		ctsTcpStatistics(ctsTcpStatistics&&) noexcept = default;

		ctsTcpStatistics operator=(const ctsTcpStatistics&) = delete;
		ctsTcpStatistics operator=(ctsTcpStatistics&&) = delete;

		[[nodiscard]] int64_t GetBytesTransferred() const noexcept
		{
			return m_bytesRecv.GetValue() + m_bytesSent.GetValue();
		}

		//
		// snap-view will set the returned start time == last read time to capture the delta
		// - and end time == current time
		//
		ctsTcpStatistics SnapView(bool clear_settings) noexcept
		{
			const int64_t currentTime = ctl::ctTimer::snap_qpc_as_msec();
			const int64_t priorTimeRead = clear_settings ?
				m_startTime.SetPriorValue(currentTime) :
				m_startTime.GetPriorValue();

			ctsTcpStatistics returnStats(priorTimeRead);
			returnStats.m_endTime.SetValue(currentTime);

			if (clear_settings)
			{
				returnStats.m_bytesSent.SetValue(m_bytesSent.SnapValueDifference());
				returnStats.m_bytesRecv.SetValue(m_bytesRecv.SnapValueDifference());
			}
			else
			{
				returnStats.m_bytesSent.SetValue(m_bytesSent.ReadValueDifference());
				returnStats.m_bytesRecv.SetValue(m_bytesRecv.ReadValueDifference());
			}

			return returnStats;
		}
	};
}
