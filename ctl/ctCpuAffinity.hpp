/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

/* ctCpuAffinity - helper to detect CPU affinity facilities and map shard -> affinity
	This header provides a small, testable interface for the sharded receive
	implementation to detect kernel capabilities (SIO_CPU_AFFINITY) and compute
	per-shard affinity mappings. Implementations can be platform-optimized in
	a corresponding .cpp file.
*/

#pragma once

#include <cstdint>
#include <cwctype>
#include <vector>
#include <optional>
#include <string>

// wil headers always included last; wil/stl.h before wil/network.h; wil/network.h owns all networking headers
#include <wil/stl.h>
#include <wil/network.h>
#include <wil/resource.h>

namespace ctl
{
	namespace details {
		// Helper: get per-group processor counts
		inline std::vector<uint32_t> GetProcessorCountsPerGroup() noexcept
		{
			std::vector<uint32_t> counts;
			const auto groupCount = GetActiveProcessorGroupCount();
			counts.reserve(groupCount);
			for (WORD g = 0; g < groupCount; ++g)
			{
				const auto proc_count = GetActiveProcessorCount(g);
				WI_ASSERT(proc_count > 0);
				counts.push_back(proc_count);
			}
			return counts;
		}

		// Convert a global cpu index to group and local index
		inline void GlobalCpuIndexToGroupAndIndex(uint32_t globalIndex, const std::vector<uint32_t>& perGroupCounts, _Out_ WORD* outGroup, _Out_ uint32_t* outIndex) noexcept
		{
			uint32_t acc = 0;
			for (WORD g = 0; g < perGroupCounts.size(); ++g)
			{
				const uint32_t c = perGroupCounts[g];
				if (globalIndex < acc + c)
				{
					*outGroup = g;
					*outIndex = globalIndex - acc;
					return;
				}
				acc += c;
			}
			// fallback to last group
			if (!perGroupCounts.empty())
			{
				*outGroup = static_cast<WORD>(perGroupCounts.size() - 1);
				*outIndex = perGroupCounts.back() ? (perGroupCounts.back() - 1) : 0;
			}
			else
			{
				*outGroup = 0;
				*outIndex = 0;
			}
		}
	}

	enum class CpuAffinityPolicy : uint8_t
	{
		PerCpu,
		PerGroup,
		RssAligned,
		Manual
	};

	struct CpuAffinityInfo
	{
		uint32_t LogicalProcessorCount = 1;    // total logical processors
		uint32_t ProcessorGroupCount = 1;     // number of processor groups
		bool SupportsCpuAffinityIoctl = false; // WSAIoctl(SIO_CPU_AFFINITY) support
	};

	// Query runtime support for CPU affinity features. Non-throwing.
	inline CpuAffinityInfo QueryCpuAffinitySupport() noexcept
	{
		CpuAffinityInfo info{};

		// Processor group and counts
		info.ProcessorGroupCount = static_cast<uint32_t>(GetActiveProcessorGroupCount());
		// total logical processors across groups
		uint64_t total = 0;
		for (WORD g = 0; g < info.ProcessorGroupCount; ++g)
		{
			total += GetActiveProcessorCount(g);
		}
		info.LogicalProcessorCount = static_cast<uint32_t>(total);

		// Probe SIO_CPU_AFFINITY support using a temporary UDP socket, if available
		const wil::unique_socket s{ socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) };
		if (s)
		{
			DWORD bytes = 0;
			// Per MSDN, SIO_CPU_AFFINITY expects a USHORT processor index (0-based)
			// as its input buffer. Probe support by passing a small USHORT buffer.
			USHORT probeProcessorIndex = 0;
#if !defined(SIO_CPU_AFFINITY)
#define SIO_CPU_AFFINITY                    _WSAIOW(IOC_VENDOR,21)
#endif
			const int rc = WSAIoctl(s.get(), SIO_CPU_AFFINITY, &probeProcessorIndex, sizeof(probeProcessorIndex), nullptr, 0, &bytes, nullptr, nullptr);
			info.SupportsCpuAffinityIoctl = (rc == 0);
		}
		else
		{
			info.SupportsCpuAffinityIoctl = false;
		}

		return info;
	}

	// Represents a group + affinity mask for thread/socket affinity operations.
	struct GroupAffinity
	{
		WORD Group = 0;
		KAFFINITY Mask = 0;
	};

	// Compute per-shard affinity mapping given a shard count and policy.
	// Returns an optional vector of length == shardCount when mapping is possible.
	inline std::optional<std::vector<GroupAffinity>> ComputeShardAffinities(uint32_t shardCount, CpuAffinityPolicy policy)
	{
		if (shardCount == 0)
		{
			return std::nullopt;
		}

		const auto perGroup = details::GetProcessorCountsPerGroup();
		uint32_t totalProcessors = 0;
		for (const auto c : perGroup)
		{
			totalProcessors += c;
		}
		if (totalProcessors == 0)
		{
			return std::nullopt;
		}

		std::vector<GroupAffinity> result;
		result.resize(shardCount);

		switch (policy)
		{


		case CpuAffinityPolicy::Manual:
			// Manual policy requires external mapping; indicate failure by returning nullopt
			return std::nullopt;

		case CpuAffinityPolicy::PerCpu:
		case CpuAffinityPolicy::RssAligned:
		{
			// Distribute shards across individual logical processors round-robin
			for (uint32_t i = 0; i < shardCount; ++i)
			{
				const uint32_t cpuIndex = i % totalProcessors;
				GroupAffinity ga{};
				uint32_t localIndex = 0;
				details::GlobalCpuIndexToGroupAndIndex(cpuIndex, perGroup, &ga.Group, &localIndex);
				// Limit mask to 64 bits (KAFFINITY is 64-bit on x64)
				if (localIndex < (sizeof(KAFFINITY) * 8))
				{
					ga.Mask = static_cast<KAFFINITY>(1ULL << localIndex);
				}
				else
				{
					// fallback to first bit if index too large
					ga.Mask = static_cast<KAFFINITY>(1ULL);
				}
				result[i] = ga;
			}
			return result;
		}

		case CpuAffinityPolicy::PerGroup:
		default:
		{
			// Assign shards to groups (full group mask) round-robin across available groups
			const size_t groupCount = perGroup.size();
			std::vector<KAFFINITY> groupMasks(groupCount, 0);
			for (size_t g = 0; g < groupCount; ++g)
			{
				KAFFINITY mask = 0;
				const uint32_t count = perGroup[g];
				for (uint32_t bi = 0; bi < count && bi < (sizeof(KAFFINITY) * 8); ++bi)
				{
					mask |= (static_cast<KAFFINITY>(1ULL) << bi);
				}
				groupMasks[g] = mask;
			}

			for (uint32_t i = 0; i < shardCount; ++i)
			{
				const size_t g = i % groupCount;
				result[i].Group = static_cast<WORD>(g);
				result[i].Mask = groupMasks[g];
			}
			return result;
		}
		}
	}

	// Parse a policy name (case-insensitive) to CpuAffinityPolicy.
	inline std::optional<CpuAffinityPolicy> ParsePolicyName(const std::wstring& name) noexcept
	{
		if (name.empty())
		{
			return std::nullopt;
		}

		std::wstring s = name;
		for (auto& c : s)
		{
			// towupper requires c to be in the ASCII range
			FAIL_FAST_IF(iswascii(c) == 0);
			c = std::towupper(c);
		}
		if (s == L"NONE") return std::nullopt;
		if (s == L"PERCPU" || s == L"PER_CPU") return CpuAffinityPolicy::PerCpu;
		if (s == L"PERGROUP" || s == L"PER_GROUP") return CpuAffinityPolicy::PerGroup;
		if (s == L"RSSALIGNED" || s == L"RSS_ALIGNED") return CpuAffinityPolicy::RssAligned;
		if (s == L"MANUAL") return CpuAffinityPolicy::Manual;

		return std::nullopt;
	}

	// Human-readable formatting helper for logging.
	inline std::wstring FormatGroupAffinity(const GroupAffinity& g)
	{
		wchar_t buf[128]{};
		FAIL_FAST_IF(-1 == swprintf_s(buf, _countof(buf), L"Group=%u Mask=0x%llx", g.Group, static_cast<unsigned long long>(g.Mask)));
		return buf;
	}
}
