/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

/*
    Unit tests for ctl::ctCpuAffinity helpers
*/

#include <sdkddkver.h>
#include "CppUnitTest.h"

#include "../../ctl/ctCpuAffinity.hpp"

// wil headers always included last
#include <wil/stl.h>
#include <wil/network.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;

namespace ctsUnitTest
{
    TEST_CLASS(ctsCpuAffinityUnitTest)
    {
    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            WSADATA wsa;
            const int startup = WSAStartup(MAKEWORD(2,2), &wsa);
            Assert::AreEqual(0, startup);
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            WSACleanup();
        }

        TEST_METHOD(QueryCpuAffinitySupport_Basic)
        {
            const auto info = ctl::QueryCpuAffinitySupport();
            Assert::IsTrue(info.ProcessorGroupCount >= 1);
            Assert::IsTrue(info.LogicalProcessorCount >= 1);
        }

        TEST_METHOD(ParsePolicyName_Varieties)
        {
            auto p = ctl::ParsePolicyName(L"none");
            Assert::IsFalse(p.has_value());

            p = ctl::ParsePolicyName(L"percpu");
            Assert::IsTrue(p.has_value());
            Assert::AreEqual(static_cast<int>(*p), static_cast<int>(ctl::CpuAffinityPolicy::PerCpu));

            p = ctl::ParsePolicyName(L"per_group");
            Assert::IsTrue(p.has_value());
            Assert::AreEqual(static_cast<int>(*p), static_cast<int>(ctl::CpuAffinityPolicy::PerGroup));

            p = ctl::ParsePolicyName(L"rss_aligned");
            Assert::IsTrue(p.has_value());
            Assert::AreEqual(static_cast<int>(*p), static_cast<int>(ctl::CpuAffinityPolicy::RssAligned));

            p = ctl::ParsePolicyName(L"manual");
            Assert::IsTrue(p.has_value());
            Assert::AreEqual(static_cast<int>(*p), static_cast<int>(ctl::CpuAffinityPolicy::Manual));
        }

        TEST_METHOD(ComputeShardAffinities_Sanity)
        {
	        constexpr uint32_t shardCount = 4;
            auto mapping = ctl::ComputeShardAffinities(shardCount, ctl::CpuAffinityPolicy::PerCpu);
            Assert::IsTrue(mapping.has_value());
            Assert::AreEqual(shardCount, static_cast<uint32_t>(mapping->size()));

            // PerCpu policy should return per-shard mappings
            auto perCpuMap = ctl::ComputeShardAffinities(shardCount, ctl::CpuAffinityPolicy::PerCpu);
            Assert::IsTrue(perCpuMap.has_value());
            Assert::AreEqual(shardCount, static_cast<uint32_t>(perCpuMap->size()));

            // Manual should indicate uncomputable mapping
            auto manualMap = ctl::ComputeShardAffinities(shardCount, ctl::CpuAffinityPolicy::Manual);
            Assert::IsFalse(manualMap.has_value());
        }
    };
}
