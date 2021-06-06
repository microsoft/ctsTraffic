/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <array>
#include <string>
// os headers
#include <Windows.h>
#include <WinSock2.h>
// ctl headers
#include <ctTimer.hpp>
// local headers
#include "ctsConfig.h"
#include "ctsIOTask.hpp"
#include "ctsSafeInt.hpp"
#include "ctsStatistics.hpp"
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>

namespace ctsTraffic
{
    //
    // ctsMediaStreamMessage encapsulates requests sent from clients
    //
    // Grammar:
    //
    //   REQUEST_ID
    //   START
    //
    constexpr unsigned short c_udpDatagramProtocolHeaderFlagData = 0x0000;
    constexpr unsigned short c_udpDatagramProtocolHeaderFlagId = 0x1000;

    constexpr unsigned long c_udpDatagramProtocolHeaderFlagLength = 2;
    constexpr unsigned long c_udpDatagramConnectionIdHeaderLength = c_udpDatagramProtocolHeaderFlagLength + ctsStatistics::c_connectionIdLength;

    constexpr unsigned long c_udpDatagramSequenceNumberLength = 8; // 64-bit value
    constexpr unsigned long c_udpDatagramQpcLength = 8; // 64-bit value
    constexpr unsigned long c_udpDatagramQpfLength = 8; // 64-bit value
    constexpr unsigned long c_udpDatagramDataHeaderLength = c_udpDatagramProtocolHeaderFlagLength + c_udpDatagramSequenceNumberLength + c_udpDatagramQpcLength + c_udpDatagramQpfLength;

    constexpr unsigned long c_udpDatagramMaximumSizeBytes = 64000UL;

    static const char* g_udpDatagramStartString = "START";
    constexpr unsigned long c_udpDatagramStartStringLength = 5;

    enum class MediaStreamAction : char
    {
        // ReSharper disable once CppInconsistentNaming
        START
    };

    class ctsMediaStreamSendRequests
    {
    public:
        ~ctsMediaStreamSendRequests() = default;
        ctsMediaStreamSendRequests() = delete;
        ctsMediaStreamSendRequests(const ctsMediaStreamSendRequests&) = delete;
        ctsMediaStreamSendRequests& operator=(const ctsMediaStreamSendRequests&) = delete;
        ctsMediaStreamSendRequests(ctsMediaStreamSendRequests&&) = delete;
        ctsMediaStreamSendRequests& operator=(ctsMediaStreamSendRequests&&) = delete;


        static constexpr unsigned long c_bufferArraySize = 5;
        ///
        /// compose iteration across buffers to be sent per instantiated request
        ///
        // ReSharper disable once CppInconsistentNaming
        class iterator
        {
        public:
            ////////////////////////////////////////////////////////////////////////////////
            ///
            /// iterator_traits
            /// - allows <algorithm> functions to be used
            ///
            ////////////////////////////////////////////////////////////////////////////////
            // ReSharper disable once CppInconsistentNaming
            using iterator_category = std::forward_iterator_tag;
            // ReSharper disable once CppInconsistentNaming
            using value_type = std::array<WSABUF, c_bufferArraySize>;
            // ReSharper disable once CppInconsistentNaming
            using difference_type = size_t;
            // ReSharper disable once CppInconsistentNaming
            using pointer = std::array<WSABUF, c_bufferArraySize>*;
            // ReSharper disable once CppInconsistentNaming
            using reference = std::array<WSABUF, c_bufferArraySize>&;

            ~iterator() = default;
            iterator(const iterator&) = default;
            iterator& operator=(const iterator&) = default;
            iterator(iterator&&) = default;
            iterator& operator=(iterator&&) = default;

            ///
            /// Dereferencing operators
            /// - returning non-const array references as Winsock APIs don't take const WSABUF*
            ///
            std::array<WSABUF, c_bufferArraySize>* operator->() noexcept
            {
                FAIL_FAST_IF_MSG(
                    nullptr == m_qpcAddress,
                    "Invalid ctsMediaStreamSendRequests::iterator being dereferenced (%p)", this);

                // refresh the QPC value at the last possible moment before returning the array to the user
                _Analysis_assume_(m_qpcAddress != nullptr);
                QueryPerformanceCounter(m_qpcAddress);
                return &m_wsaBufArray;
            }

            std::array<WSABUF, c_bufferArraySize>& operator*() noexcept
            {
                FAIL_FAST_IF_MSG(
                    nullptr == m_qpcAddress,
                    "Invalid ctsMediaStreamSendRequests::iterator being dereferenced (%p)", this);

                // refresh the QPC value at the last possible moment before returning the array to the user
                _Analysis_assume_(m_qpcAddress != nullptr);
                QueryPerformanceCounter(m_qpcAddress);
                return m_wsaBufArray;
            }

            ///
            /// Equality operators
            ///
            bool operator ==(const iterator& comparand) const noexcept
            {
                return m_qpcAddress == comparand.m_qpcAddress &&
                    m_bytesToSend == comparand.m_bytesToSend;
            }
            bool operator !=(const iterator& comparand) const noexcept
            {
                return !(*this == comparand);
            }

            /// preincrement
            iterator& operator++() noexcept
            {
                FAIL_FAST_IF_MSG(
                    nullptr == m_qpcAddress,
                    "Invalid ctsMediaStreamSendRequests::iterator being dereferenced (%p)", this);

                // if bytes is zero, then increment to the end() iterator
                if (0 == m_bytesToSend)
                {
                    m_qpcAddress = nullptr;
                }
                else
                {
                    m_bytesToSend -= this->UpdateBufferLength();
                }

                return *this;
            }
            // postincrement
            iterator operator++(int) noexcept
            {
                iterator temp(*this);
                ++* this;
                return temp;
            }

        private:
            // c'tor is only available to the begin() and end() methods of ctsMediaStreamSendRequests
            friend class ctsMediaStreamSendRequests;
            iterator(_In_opt_ LARGE_INTEGER* qpcAddress, long long bytesToSend, const std::array<WSABUF, c_bufferArraySize>& wsaBufferArray) noexcept :
                m_qpcAddress(qpcAddress),
                m_bytesToSend(bytesToSend),
                m_wsaBufArray(wsaBufferArray)
            {
                // set the buffer length for the first iterator
                m_bytesToSend -= this->UpdateBufferLength();
            }

            unsigned long UpdateBufferLength() noexcept
            {
                // only update when not the end() iterator
                if (m_qpcAddress)
                {
                    if (m_bytesToSend > c_udpDatagramMaximumSizeBytes)
                    {
                        m_wsaBufArray[4].len = c_udpDatagramMaximumSizeBytes - c_udpDatagramDataHeaderLength;
                    }
                    else
                    {
                        m_wsaBufArray[4].len = static_cast<unsigned long>(m_bytesToSend - c_udpDatagramDataHeaderLength);
                    }

                    ctsUnsignedLong totalBytesToSend =
                        m_wsaBufArray[0].len + m_wsaBufArray[1].len + m_wsaBufArray[2].len + m_wsaBufArray[3].len + m_wsaBufArray[4].len;

                    // must guarantee that after we send this datagram we have enough bytes for the next send if there are bytes left over
                    const ctsSignedLongLong bytesRemaining = m_bytesToSend - static_cast<long long>(totalBytesToSend);

                    if (bytesRemaining > 0 && bytesRemaining <= c_udpDatagramDataHeaderLength)
                    {
                        // subtract out enough bytes so the next datagram will be large enough for the header and at least one byte of data
                        ctsUnsignedLong newLength = m_wsaBufArray[4].len;
                        const ctsUnsignedLong deltaToRemove = c_udpDatagramDataHeaderLength + 1 - static_cast<unsigned long>(bytesRemaining);
                        newLength -= deltaToRemove;

                        m_wsaBufArray[4].len = newLength;
                        totalBytesToSend -= deltaToRemove;
                    }

                    return totalBytesToSend;
                }

                return 0UL;
            }

            LARGE_INTEGER* m_qpcAddress;
            ctsSignedLongLong m_bytesToSend;
            std::array<WSABUF, c_bufferArraySize> m_wsaBufArray;
        };


        ///
        /// Constructor of the ctsMediaStreamSendRequests captures the properties of the next Send() request
        /// - the total # of bytes to send (across X number of send requests)
        /// - the sequence number to tag in every send request
        ///
        ctsMediaStreamSendRequests(long long bytesToSend, long long sequenceNumber, const char* sendBuffer) noexcept :
            m_qpf(ctl::ctTimer::SnapQpf()),
            m_bytesToSend(bytesToSend),
            m_sequenceNumber(sequenceNumber)
        {
            FAIL_FAST_IF_MSG(
                bytesToSend <= c_udpDatagramDataHeaderLength,
                "ctsMediaStreamSendRequests requires a buffer size to send larger than the ctsTraffic UDP header");

            // buffer layout: header#, seq. number, qpc, qpf, then the buffered data
            m_wsabuffer[0].buf = reinterpret_cast<char*>(const_cast<unsigned short*>(&c_udpDatagramProtocolHeaderFlagData));
            m_wsabuffer[0].len = c_udpDatagramProtocolHeaderFlagLength;

            m_wsabuffer[1].buf = reinterpret_cast<char*>(&m_sequenceNumber);
            m_wsabuffer[1].len = c_udpDatagramSequenceNumberLength;

            m_wsabuffer[2].buf = reinterpret_cast<char*>(&m_qpcValue.QuadPart);
            m_wsabuffer[2].len = c_udpDatagramQpcLength;

            m_wsabuffer[3].buf = reinterpret_cast<char*>(&m_qpf);
            m_wsabuffer[3].len = c_udpDatagramQpfLength;

            m_wsabuffer[4].buf = const_cast<char*>(sendBuffer);
            // the this->wsabuf[4].len field is dependent on bytes_to_send and can change by iterator()
        }

        // ReSharper disable once CppInconsistentNaming
        [[nodiscard]] iterator begin() noexcept
        {
            return { &m_qpcValue, m_bytesToSend, m_wsabuffer };
        }

        // ReSharper disable once CppInconsistentNaming
        [[nodiscard]] iterator end() const noexcept
        {
            // end == null qpc + 0 byte length
            return { nullptr, 0, m_wsabuffer };
        }


    private:
        std::array<WSABUF, c_bufferArraySize> m_wsabuffer{};
        LARGE_INTEGER m_qpcValue{};
        long long m_qpf{};
        long long m_bytesToSend{};
        long long m_sequenceNumber{};
    };


    struct ctsMediaStreamMessage
    {
        long long m_sequenceNumber = 0ll;
        MediaStreamAction m_action{};

        explicit ctsMediaStreamMessage(MediaStreamAction action) noexcept : m_action(action)
        {
        }
        ~ctsMediaStreamMessage() noexcept = default;
        ctsMediaStreamMessage(const ctsMediaStreamMessage&) = default;
        ctsMediaStreamMessage& operator=(const ctsMediaStreamMessage&) = default;
        ctsMediaStreamMessage(ctsMediaStreamMessage&&) = default;
        ctsMediaStreamMessage& operator=(ctsMediaStreamMessage&&) = default;

        static bool ValidateBufferLengthFromTask(const ctsTask& task, unsigned long completedBytes) noexcept
        {
            if (completedBytes < c_udpDatagramProtocolHeaderFlagLength)
            {
                ctsConfig::PrintErrorInfo(
                    L"ctsMediaStreamMessage::ValidateBufferLengthFromTask rejecting the datagram: the datagram size (%u) is less than UdpDatagramProtocolHeaderFlagLength (%u)",
                    completedBytes,
                    c_udpDatagramProtocolHeaderFlagLength);
                return false;
            }

            switch (GetProtocolHeaderFromTask(task))
            {
                case c_udpDatagramProtocolHeaderFlagData:
                    if (completedBytes < c_udpDatagramDataHeaderLength)
                    {
                        ctsConfig::PrintErrorInfo(
                            L"ctsMediaStreamMessage::ValidateBufferLengthFromTask rejecting the datagram type UdpDatagramProtocolHeaderFlagData: the datagram size (%u) is less than UdpDatagramDataHeaderLength (%u)",
                            completedBytes,
                            c_udpDatagramDataHeaderLength);
                        return false;
                    }
                    break;

                case c_udpDatagramProtocolHeaderFlagId:
                    if (completedBytes < c_udpDatagramConnectionIdHeaderLength)
                    {
                        ctsConfig::PrintErrorInfo(
                            L"ctsMediaStreamMessage::ValidateBufferLengthFromTask rejecting the datagram type UdpDatagramProtocolHeaderFlagId: the datagram size (%u) is less than UdpDatagramConnectionIdHeaderLength (%u)",
                            completedBytes,
                            c_udpDatagramConnectionIdHeaderLength);
                        return false;
                    }
                    break;

                default:
                    ctsConfig::PrintErrorInfo(
                        L"ctsMediaStreamMessage::ValidateBufferLengthFromTask rejecting the datagram of unknown frame type (%u) - expecting UdpDatagramProtocolHeaderFlagData (%u) or UdpDatagramProtocolHeaderFlagId (%u)",
                        GetProtocolHeaderFromTask(task),
                        c_udpDatagramProtocolHeaderFlagData,
                        c_udpDatagramProtocolHeaderFlagId);
                    return false;
            }

            return true;
        }

        static unsigned short GetProtocolHeaderFromTask(const ctsTask& task) noexcept
        {
            return *reinterpret_cast<unsigned short*>(task.m_buffer);
        }

        static void SetConnectionIdFromTask(_Inout_updates_(ctsStatistics::ConnectionIdLength) char* const connectionId, const ctsTask& task) noexcept
        {
            const auto copyError = memcpy_s(
                connectionId,
                ctsStatistics::c_connectionIdLength,
                task.m_buffer + task.m_bufferOffset + c_udpDatagramProtocolHeaderFlagLength,
                ctsStatistics::c_connectionIdLength);
            FAIL_FAST_IF_MSG(
                copyError != 0,
                "ctsMediaStreamMessage::GetConnectionIdFromTask : memcpy_s failed trying to copy the connection ID - target buffer (%p) ctsIOTask (%p) (error : %d)",
                connectionId, &task, copyError);
        }

        static long long GetSequenceNumberFromTask(const ctsTask& task) noexcept
        {
            long long returnValue;
            const auto copyError = memcpy_s(
                &returnValue,
                c_udpDatagramSequenceNumberLength,
                task.m_buffer + task.m_bufferOffset + c_udpDatagramProtocolHeaderFlagLength,
                c_udpDatagramSequenceNumberLength);
            FAIL_FAST_IF_MSG(
                copyError != 0,
                "ctsMediaStreamMessage::GetSequenceNumberFromTask : memcpy_s failed trying to copy the sequence number - ctsIOTask (%p) (error : %d)",
                &task, copyError);

            return returnValue;
        }

        static long long GetQueryPerfCounterFromTask(const ctsTask& task) noexcept
        {
            long long returnValue;
            const auto copyError = memcpy_s(&returnValue, c_udpDatagramSequenceNumberLength, task.m_buffer + task.m_bufferOffset + c_udpDatagramProtocolHeaderFlagLength, c_udpDatagramSequenceNumberLength);
            FAIL_FAST_IF_MSG(
                copyError != 0,
                "ctsMediaStreamMessage::GetSequenceNumberFromTask : memcpy_s failed trying to copy the sequence number - ctsIOTask (%p) (error : %d)",
                &task, copyError);

            return returnValue;
        }

        static long long GetQueryPerfFrequencyFromTask(const ctsTask& task) noexcept
        {
            long long returnValue;
            const auto copyError = memcpy_s(&returnValue, c_udpDatagramSequenceNumberLength, task.m_buffer + task.m_bufferOffset + c_udpDatagramProtocolHeaderFlagLength, c_udpDatagramSequenceNumberLength);
            FAIL_FAST_IF_MSG(
                copyError != 0,
                "ctsMediaStreamMessage::GetSequenceNumberFromTask : memcpy_s failed trying to copy the sequence number - target buffer (%p) ctsIOTask (%p) (error : %d)",
                &returnValue, &task, copyError);

            return returnValue;
        }

        static ctsTask MakeConnectionIdTask(const ctsTask& rawTask, _In_reads_(ctsStatistics::ConnectionIdLength) const char* const connectionId) noexcept
        {
            FAIL_FAST_IF_MSG(
                rawTask.m_bufferLength != ctsStatistics::c_connectionIdLength + c_udpDatagramProtocolHeaderFlagLength,
                "ctsMediaStreamMessage::GetConnectionIdFromTask : the buffer_length in the provided task (%u) is not the expected buffer length (%u)",
                rawTask.m_bufferLength, ctsStatistics::c_connectionIdLength + c_udpDatagramProtocolHeaderFlagLength);

            ctsTask returnTask{ rawTask };
            // populate the buffer with the connection Id and protocol field
            memcpy_s(returnTask.m_buffer, c_udpDatagramProtocolHeaderFlagLength, &c_udpDatagramProtocolHeaderFlagId, c_udpDatagramProtocolHeaderFlagLength);
            memcpy_s(returnTask.m_buffer + c_udpDatagramProtocolHeaderFlagLength, ctsStatistics::c_connectionIdLength, connectionId, ctsStatistics::c_connectionIdLength);

            returnTask.m_ioAction = ctsTaskAction::Send;
            returnTask.m_bufferType = ctsTask::BufferType::UdpConnectionId;
            returnTask.m_trackIo = false;
            return returnTask;
        }

        static ctsTask Construct(MediaStreamAction action) noexcept
        {
            ctsTask returnTask;
            returnTask.m_ioAction = ctsTaskAction::Send;
            returnTask.m_bufferType = ctsTask::BufferType::Static;
            returnTask.m_trackIo = false;

            // safe to const-cast as we are sending these buffers
            switch (action)
            {
                case MediaStreamAction::START:
                    returnTask.m_buffer = const_cast<char*>(g_udpDatagramStartString);
                    returnTask.m_bufferLength = c_udpDatagramStartStringLength;
                    break;

                default: FAIL_FAST();
            }

            return returnTask;
        }

        static ctsMediaStreamMessage Extract(_In_reads_bytes_(inputLength) const char* inputBuffer, unsigned inputLength)
        {
            if (inputLength == c_udpDatagramStartStringLength)
            {
                if (0 == memcmp(inputBuffer, g_udpDatagramStartString, c_udpDatagramStartStringLength))
                {
                    return ctsMediaStreamMessage(MediaStreamAction::START);
                }
            }

            THROW_HR_MSG(HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                "Invalid MediaStream message: %hs",
                std::string(inputBuffer, inputLength).c_str());
        }
    };
}

