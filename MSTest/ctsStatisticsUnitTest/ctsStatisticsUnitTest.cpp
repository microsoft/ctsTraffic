/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <SDKDDKVer.h>
#include "CppUnitTest.h"

#include <memory>

#include <ctString.hpp>

#include "ctsIOPatternState.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft {
    namespace VisualStudio {
        namespace CppUnitTestFramework {
            template<> static std::wstring ToString<ctsTraffic::ctsUnsignedLongLong>(const ctsTraffic::ctsUnsignedLongLong& _value)
            {
                return std::to_wstring(static_cast<unsigned long long>(_value));
            }

        }
    }
}

ctsTraffic::ctsUnsignedLongLong s_TransferSize = 0ULL;
bool s_Listening = false;
///
/// Fakes
///
namespace ctsTraffic {
    namespace ctsConfig {
        ctsConfigSettings* Settings;

        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error) noexcept
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsTcpStatistics& _stats) noexcept
        {
        }
        void PrintConnectionResults(const ctl::ctSockaddr& _local_addr, const ctl::ctSockaddr& _remote_addr, unsigned long _error, const ctsUdpStatistics& _stats) noexcept
        {
        }
        void PrintDebug(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) noexcept
        {
        }
        void PrintException(const std::exception& e) noexcept
        {
        }
        void PrintJitterUpdate(long long _sequence_number, long long _sender_qpc, long long _sender_qpf, long long _recevier_qpc, long long _receiver_qpf) noexcept
        {
        }
        void PrintErrorInfo(_In_z_ _Printf_format_string_ LPCWSTR _text, ...) noexcept
        {
        }

        bool IsListening() noexcept
        {
            return s_Listening;
        }

        ctsUnsignedLongLong GetTransferSize() noexcept
        {
            return s_TransferSize;
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
}
///
/// End of Fakes
///
    
using namespace ctsTraffic;
namespace ctsUnitTest {
    TEST_CLASS(ctsStatisticsUnitTest)
    {
    private:
        //
        // The pattern state to use with each test
        //
        std::unique_ptr<ctsIOPatternState> pattern_state;

        enum Role  {
            Client,
            Server
        };

    public:
        TEST_CLASS_INITIALIZE(Setup)
        {
            ctsConfig::Settings = new ctsConfig::ctsConfigSettings;
            ctsConfig::Settings->Protocol = ctsConfig::ProtocolType::TCP;
            ctsConfig::Settings->TcpShutdown = ctsConfig::TcpShutdownType::GracefulShutdown;
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            delete ctsConfig::Settings;
        }

        TEST_METHOD(Default)
        {
            s_Listening = true;

            ctsTcpStatistics tcp_stats;
            ctsUdpStatistics udp_stats;
            ctsConnectionStatistics conn_stats;
        }
    };
}