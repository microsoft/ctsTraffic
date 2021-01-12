/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <sdkddkver.h>
#include "CppUnitTest.h"

#include <memory>

#include <ctString.hpp>

#include "ctsIOPatternState.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{
    template<> inline std::wstring ToString<ctsTraffic::ctsUnsignedLongLong>(const ctsTraffic::ctsUnsignedLongLong& value)
    {
        return std::to_wstring(static_cast<unsigned long long>(value));
    }
}

ctsTraffic::ctsUnsignedLongLong g_transferSize = 0ULL;
bool g_isListening = false;
///
/// Fakes
///
namespace ctsTraffic::ctsConfig
{
    ctsConfigSettings* g_configSettings;

    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long) noexcept
    {
    }
    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long, const ctsTcpStatistics&) noexcept
    {
    }
    void PrintConnectionResults(const ctl::ctSockaddr&, const ctl::ctSockaddr&, unsigned long, const ctsUdpStatistics&) noexcept
    {
    }
    void PrintDebug(_In_z_ _Printf_format_string_ PCWSTR, ...) noexcept
    {
    }
    void PrintException(const std::exception&) noexcept
    {
    }
    void PrintErrorInfo(_In_z_ _Printf_format_string_ PCWSTR, ...) noexcept
    {
    }

    bool IsListening() noexcept
    {
        return g_isListening;
    }

    ctsUnsignedLongLong GetTransferSize() noexcept
    {
        return g_transferSize;
    }
    bool ShutdownCalled() noexcept
    {
        return false;
    }
    unsigned long ConsoleVerbosity() noexcept
    {
        return 0;
    }
}

///
/// End of Fakes
///

using namespace ctsTraffic;
namespace ctsUnitTest
{
    TEST_CLASS(ctsStatisticsUnitTest)
    {
    private:
        //
        // The pattern state to use with each test
        //
        std::unique_ptr<ctsIoPatternState> pattern_state;

        enum Role
        {
            Client,
            Server
        };

    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            ctsConfig::g_configSettings = new ctsConfig::ctsConfigSettings;
            ctsConfig::g_configSettings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::g_configSettings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            delete ctsConfig::g_configSettings;
        }

        TEST_METHOD(Default)
        {
            g_isListening = true;

            ctsTcpStatistics tcp_stats;
            ctsUdpStatistics udp_stats;
            ctsConnectionStatistics conn_stats;
        }
    };
}