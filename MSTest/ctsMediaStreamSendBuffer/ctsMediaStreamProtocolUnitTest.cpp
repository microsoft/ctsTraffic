/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <sdkddkver.h>
#include "CppUnitTest.h"

#include <string>
#include <ctString.hpp>
#include "ctsMediaStreamProtocol.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace ctsTraffic;

template <>
std::wstring __cdecl Microsoft::VisualStudio::CppUnitTestFramework::ToString<unsigned short>(const unsigned short& _value)
{
    return std::to_wstring(_value);
}
template <>
std::wstring __cdecl Microsoft::VisualStudio::CppUnitTestFramework::ToString<ctsTraffic::MediaStreamAction>(const ctsTraffic::MediaStreamAction& _message)
{
    switch (_message) {
        case ctsTraffic::MediaStreamAction::START:
            return L"START";
    }
    return wil::str_printf<std::wstring>(L"Unknown Message (0x%x)", _message);
}

namespace ctsUnitTest
{		
    TEST_CLASS(ctsMediaStreamProtocolUnitTest)
    {
        const long long SequenceNumber = 1LL;
        char* BufferPtr = nullptr;

    public:
        
        TEST_METHOD(IteratorSingleBufferValidationTest) 
        { 
            ctsMediaStreamSendRequests testbuffer(c_udpDatagramDataHeaderLength + 1, SequenceNumber, BufferPtr);

            Assert::IsFalse(
                testbuffer.begin() == testbuffer.end(),
                L"begin(iterator) cannot equal end(iterator)");

            auto starting = std::begin(testbuffer);
            const auto ending = std::end(testbuffer);
            while (starting != ending) {
                // verify operator->
                Assert::AreEqual(static_cast<size_t>(5), starting->size());
                // verify operator*
                auto deref = *starting;
                Assert::AreEqual(static_cast<size_t>(5), deref.size());
                ++starting;
            }
        }

        TEST_METHOD(IteratorMultipleBufferValidationTest)
        {
            ctsMediaStreamSendRequests testbuffer(c_udpDatagramMaximumSizeBytes + 1, SequenceNumber, BufferPtr);
            Assert::IsFalse(
                testbuffer.begin() == testbuffer.end(),
                L"begin(iterator) cannot equal end(iterator)");

            auto starting = std::begin(testbuffer);
            const auto ending = std::end(testbuffer);
            while (starting != ending) {
                // verify operator->
                Assert::AreEqual(static_cast<size_t>(5), starting->size());
                // verify operator*
                auto deref = *starting;
                Assert::AreEqual(static_cast<size_t>(5), deref.size());
                ++starting;
            }
        }

        TEST_METHOD(TinySendRequest)
        {
            static const unsigned long buffer_size = c_udpDatagramDataHeaderLength + 1;

            ctsMediaStreamSendRequests testbuffer(buffer_size, SequenceNumber, BufferPtr);
            this->verify_protocol_header(testbuffer);
            const auto dgrams_returned = this->verify_byte_count(testbuffer, buffer_size);

            static const unsigned long expected_datagram_count = 1;
            Assert::AreEqual(expected_datagram_count, dgrams_returned);
        }
        
        TEST_METHOD(OneDatagramSendRequest)
        {
            static const unsigned long buffer_size = c_udpDatagramMaximumSizeBytes;

            ctsMediaStreamSendRequests testbuffer(buffer_size, SequenceNumber, BufferPtr);
            this->verify_protocol_header(testbuffer);
            const auto dgrams_returned = this->verify_byte_count(testbuffer, buffer_size);

            static const unsigned long expected_datagram_count = 1;
            Assert::AreEqual(expected_datagram_count, dgrams_returned);
        }

        TEST_METHOD(OneDatagramMinusOneSendRequest)
        {
            static const unsigned long buffer_size = c_udpDatagramMaximumSizeBytes - 1;

            ctsMediaStreamSendRequests testbuffer(buffer_size, 1, nullptr);
            this->verify_protocol_header(testbuffer);
            const auto dgrams_returned = this->verify_byte_count(testbuffer, buffer_size);

            static const unsigned long expected_datagram_count = 1;
            Assert::AreEqual(expected_datagram_count, dgrams_returned);
        }

        TEST_METHOD(OneDatagramPlusOneSendRequest)
        {
            static const unsigned long buffer_size = c_udpDatagramMaximumSizeBytes + 1;

            ctsMediaStreamSendRequests testbuffer(buffer_size, SequenceNumber, BufferPtr);
            this->verify_protocol_header(testbuffer);
            const auto dgrams_returned = this->verify_byte_count(testbuffer, buffer_size);

            static const unsigned long expected_datagram_count = 2;
            Assert::AreEqual(expected_datagram_count, dgrams_returned);
        }

        TEST_METHOD(ExactlyTwoDatagramSendRequest)
        {
            static const unsigned long buffer_size = 2 * c_udpDatagramMaximumSizeBytes;

            ctsMediaStreamSendRequests testbuffer(buffer_size, SequenceNumber, BufferPtr);
            this->verify_protocol_header(testbuffer);
            const auto dgrams_returned = this->verify_byte_count(testbuffer, buffer_size);

            static const unsigned long expected_datagram_count = 2;
            Assert::AreEqual(expected_datagram_count, dgrams_returned);
        }
        TEST_METHOD(LargeSendRequest)
        {
            static const unsigned long buffer_size = 123456789;

            ctsMediaStreamSendRequests testbuffer(buffer_size, SequenceNumber, BufferPtr);
            this->verify_protocol_header(testbuffer);
            const auto dgrams_returned = this->verify_byte_count(testbuffer, buffer_size);

            static const unsigned long expected_datagram_count = 1930;
            Assert::AreEqual(expected_datagram_count, dgrams_returned);
        }

        TEST_METHOD(ConstructStart)
        {
            Assert::AreEqual(c_udpDatagramStartStringLength, static_cast<unsigned long>(::strlen(g_udpDatagramStartString)));

            const ctsTask test_task(ctsMediaStreamMessage::Construct(MediaStreamAction::START));
            Assert::AreEqual(c_udpDatagramStartStringLength, test_task.m_bufferLength);

            const ctsMediaStreamMessage round_trip(ctsMediaStreamMessage::Extract(test_task.m_buffer, test_task.m_bufferLength));
            Assert::AreEqual(MediaStreamAction::START, round_trip.m_action);
        }

    private:
        void verify_protocol_header(ctsMediaStreamSendRequests& _testbuffer) const
        {
            for (auto& buffer_array : _testbuffer) {
                Assert::AreEqual(c_udpDatagramProtocolHeaderFlagLength, buffer_array[0].len);
                Assert::AreEqual(c_udpDatagramProtocolHeaderFlagData, *reinterpret_cast<unsigned short*>(buffer_array[0].buf));
            }
        }
        unsigned long verify_byte_count(ctsMediaStreamSendRequests& _testbuffer, unsigned long _buffer_size) const
        {
            Logger::WriteMessage(
                wil::str_printf<std::wstring>(L"Buffer size %u\n", _buffer_size).c_str());

            unsigned long datagram_count = 0;
            unsigned long total_bytes = 0;
            for (auto& buffer_array : _testbuffer) {
                for (auto& wsa_buf : buffer_array) {
                    Logger::WriteMessage(
                        wil::str_printf<std::wstring>(L"Buffer length %u  :  ", wsa_buf.len).c_str());
                    total_bytes += wsa_buf.len;
                }
                Logger::WriteMessage(L"\n");
                ++datagram_count;
            }

            Assert::AreEqual(_buffer_size, total_bytes);

            return datagram_count;
        }
    };
}