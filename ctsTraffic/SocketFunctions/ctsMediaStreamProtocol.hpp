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
#include <memory>
// OS headers
#include <windows.h>
#include <WinSock2.h>
// ctl headers
#include <ctString.hpp>
#include <ctTimer.hpp>
#include <ctException.hpp>
// local headers
#include "ctsIOTask.hpp"
#include "ctsSafeInt.hpp"


namespace ctsTraffic {
    ///
    /// ctsMediaStreamMessage encapsulates requests sent from clients
    ///
    ///
    /// Grammar:
    ///
    ///   START
    ///   RESEND.<sequence_number>
    ///   PAUSE
    ///   RESUME
    ///   DONE
    ///

    ///
    /// The maximum possible datagram to be sent or received
    /// The minimum possible datagram to be sent or received
    /// The header size of all datagrams sent or received (included in the above constants)
    ///
    static const unsigned long UdpDatagramMaximumSizeBytes = 64000UL;
    static const unsigned long UdpDatagramHeaderSizeBytes = 24UL;

    class ctsMediaStreamSendRequests {
    public:
        ctsMediaStreamSendRequests() = delete;
        ctsMediaStreamSendRequests(const ctsMediaStreamSendRequests&) = delete;
        ctsMediaStreamSendRequests& operator=(const ctsMediaStreamSendRequests&) = delete;


        static const unsigned long BufferArraySize = 4;
        ///
        /// compose iteration across buffers to be sent per instantiated request
        ///
        class iterator {
        public:
            ////////////////////////////////////////////////////////////////////////////////
            ///
            /// iterator_traits
            /// - allows <algorithm> functions to be used
            ///
            ////////////////////////////////////////////////////////////////////////////////
            typedef std::forward_iterator_tag              iterator_category;
            typedef std::array<WSABUF, BufferArraySize>    value_type;
            typedef size_t                                 difference_type;
            typedef std::array<WSABUF, BufferArraySize>*   pointer;
            typedef std::array<WSABUF, BufferArraySize>&   reference;

            /// no default c'tor
            iterator() = delete;

            ///
            /// Dereferencing operators
            /// - returning non-const array references as Winsock APIs don't take const WSABUF*
            ///
            std::array<WSABUF, BufferArraySize>* operator->() throw()
            {
                ctl::ctFatalCondition(
                    nullptr == this->qpc_address,
                    L"Invalid ctsMediaStreamSendRequests::iterator being dereferenced (%p)", this);

                // refresh the QPC value at the last possible moment before returning the array to the user
                _Analysis_assume_(this->qpc_address != nullptr);
                ::QueryPerformanceCounter(this->qpc_address);
                return &this->wsa_buf_array;
            }

            std::array<WSABUF, BufferArraySize>& operator*() throw()
            {
                ctl::ctFatalCondition(
                    nullptr == this->qpc_address,
                    L"Invalid ctsMediaStreamSendRequests::iterator being dereferenced (%p)", this);

                // refresh the QPC value at the last possible moment before returning the array to the user
                _Analysis_assume_(this->qpc_address != nullptr);
                ::QueryPerformanceCounter(this->qpc_address);
                return this->wsa_buf_array;
            }

            ///
            /// Equality operators
            ///
            bool operator ==(const iterator& _comparand) const throw()
            {
                return (this->qpc_address == _comparand.qpc_address &&
                        this->bytes_to_send == _comparand.bytes_to_send);
            }
            bool operator !=(const iterator& _comparand) const throw()
            {
                return !(*this == _comparand);
            }

            /// preincrement
            iterator& operator++() throw()
            {
                ctl::ctFatalCondition(
                    nullptr == this->qpc_address,
                    L"Invalid ctsMediaStreamSendRequests::iterator being dereferenced (%p)", this);

                // if bytes is zero, then increment to the end() iterator
                if (0 == this->bytes_to_send) {
                    this->qpc_address = nullptr;
                } else {
                    this->bytes_to_send -= this->update_buffer_length();
                }

                return *this;
            }
            // postincrement
            iterator operator++(int) throw()
            {
                iterator temp(*this);
                ++(*this);
                return temp;
            }

            iterator(const iterator& _in) throw() :
                qpc_address(_in.qpc_address),
                bytes_to_send(_in.bytes_to_send),
                wsa_buf_array(_in.wsa_buf_array)
            {
            }

            iterator& operator=(const iterator& _in) throw()
            {
                iterator temp(_in);

                using std::swap;
                swap(this->qpc_address, temp.qpc_address);
                swap(this->bytes_to_send, temp.bytes_to_send);
                swap(this->wsa_buf_array, temp.wsa_buf_array);
            }
        private:
            // c'tor is only available to the begin() and end() methods of ctsMediaStreamSendRequests
            friend class ctsMediaStreamSendRequests;
            iterator(_In_opt_ LARGE_INTEGER* _qpc_address, long long _bytes_to_send, const std::array<WSABUF, BufferArraySize>& _wsa_buf_array) :
                qpc_address(_qpc_address),
                bytes_to_send(_bytes_to_send),
                wsa_buf_array(_wsa_buf_array)
            {
                // set the buffer length for the first iterator
                this->bytes_to_send -= this->update_buffer_length();
            }

            unsigned long update_buffer_length() throw()
            {
                ctsUnsignedLong total_bytes_to_send = 0UL;
                // only update when not the end() iterator
                if (this->qpc_address) {
                    if (this->bytes_to_send > UdpDatagramMaximumSizeBytes) {
                        this->wsa_buf_array[3].len = UdpDatagramMaximumSizeBytes - UdpDatagramHeaderSizeBytes;
                    } else {
                        this->wsa_buf_array[3].len = static_cast<unsigned long>(this->bytes_to_send - UdpDatagramHeaderSizeBytes);
                    }

                    total_bytes_to_send = this->wsa_buf_array[0].len + this->wsa_buf_array[1].len + this->wsa_buf_array[2].len + this->wsa_buf_array[3].len;

                    // must guarantee that after we send this datagram we have enough bytes for the next send if there are bytes left over
                    ctsSignedLongLong bytes_remaining = this->bytes_to_send - static_cast<long long>(total_bytes_to_send);

                    if (bytes_remaining > 0 && bytes_remaining <= UdpDatagramHeaderSizeBytes) {
                        // subtract out enough bytes so the next datagram will be large enough for the header and at least one byte of data
                        ctsUnsignedLong new_length = this->wsa_buf_array[3].len;
                        ctsUnsignedLong delta_to_remove = UdpDatagramHeaderSizeBytes + 1 - static_cast<unsigned long>(bytes_remaining);
                        new_length -= delta_to_remove;

                        this->wsa_buf_array[3].len = new_length;
                        total_bytes_to_send -= delta_to_remove;
                    }

                    return total_bytes_to_send;
                } else {
                    return 0UL;
                }
            }

            LARGE_INTEGER* qpc_address;
            ctsSignedLongLong bytes_to_send;
            std::array<WSABUF, BufferArraySize> wsa_buf_array;
        };


        ///
        /// Constructor of the ctsMediaStreamSendRequests captures the properties of the next Send() request
        /// - the total # of bytes to send (across X number of send requests)
        /// - the sequence number to tag in every send request
        ///
        ctsMediaStreamSendRequests(long long _bytes_to_send, long long _sequence_number, _In_ char* _send_buffer) throw() :
            wsabuf(),
            qpc_value(),
            qpf(ctl::ctTimer::snap_qpf()),
            bytes_to_send(_bytes_to_send),
            sequence_number(_sequence_number)
        {
            ctl::ctFatalCondition(
                _bytes_to_send <= UdpDatagramHeaderSizeBytes,
                L"ctsMediaStreamSendRequests requires a buffer size to send larger than the ctsTraffic UDP header");

            // buffer layout: seq. number, qpc, qpf, then the buffered data
            this->wsabuf[0].buf = reinterpret_cast<char*>(&this->sequence_number);
            this->wsabuf[0].len = 8; // 64-bit number

            this->wsabuf[1].buf = reinterpret_cast<char*>(&this->qpc_value.QuadPart);
            this->wsabuf[1].len = 8; // 64-bit number

            this->wsabuf[2].buf = reinterpret_cast<char*>(&this->qpf);
            this->wsabuf[2].len = 8; // 64-bit number

            this->wsabuf[3].buf = _send_buffer;
            // the this->wsabuf[3].len field is dependent on bytes_to_send and can change by iterator()
        }

        iterator begin()
        {
            return iterator(&this->qpc_value, this->bytes_to_send, this->wsabuf);
        }

        iterator end()
        {
            // end == null qpc + 0 byte length
            return iterator(nullptr, 0, this->wsabuf);
        }


    private:
        std::array<WSABUF, BufferArraySize> wsabuf;
        LARGE_INTEGER qpc_value;
        long long qpf;
        long long bytes_to_send;
        long long sequence_number;
    };


    struct ctsMediaStreamMessage {

        unsigned long frame_rate;
        unsigned long frame_size;
        unsigned long stream_length;
        long long sequence_number;
        enum Action : char {
            START = 0x1,
            RESEND = 0x2,
            DONE = 0x3
        } action;

        ctsMediaStreamMessage(Action _action) throw()
        : frame_rate(0),
          frame_size(0),
          stream_length(0),
          sequence_number(0LL),
          action(_action)
        {
        }

        static ctsIOTask Construct(Action _action) throw()
        {
            ctsIOTask return_task;
            return_task.ioAction = ctsIOTask::IOAction::Send;
            return_task.tracked_io = false;

            switch (_action) {
                case START:
                    return_task.buffer = "START";
                    return_task.buffer_length = 5;
                    break;

                case RESEND:
                    return_task.buffer = "RESEND";
                    return_task.buffer_length = 6;
                    break;

                case DONE:
                    return_task.buffer = "DONE";
                    return_task.buffer_length = 4;
                    break;

                default:
                    ctl::ctAlwaysFatalCondition(L"Invalid Action specified : %d", _action);
            }
            // avoiding compiler warnings
            return return_task;
        }
        static std::unique_ptr<std::string> Construct(Action _action, unsigned long long _seq_number)
        {
            std::unique_ptr<std::string> constructed_string(new std::string);
            if (RESEND == _action) {
                constructed_string->assign("RESEND.");
                constructed_string->resize(constructed_string->size() + 8);
                ::memcpy(&(*constructed_string)[7], &_seq_number, 8);
            } else {
                ctl::ctAlwaysFatalCondition(L"Invalid Action specified : %d", _action);
            }

            return constructed_string;
        }

        static ctsMediaStreamMessage Extract(_In_reads_bytes_(_input_length) const char* _input, _In_ unsigned _input_length)
        {
            std::string buffer(_input, _input + _input_length);
            if (ctl::ctString::iordinal_equals("START", buffer)) {
                return ctsMediaStreamMessage(START);
            }

            if (ctl::ctString::iordinal_equals("DONE", buffer)) {
                return ctsMediaStreamMessage(DONE);
            }

            if (15 == _input_length && ctl::ctString::istarts_with(buffer, "RESEND.")) {
                ctsMediaStreamMessage resend(RESEND);
                resend.sequence_number = *reinterpret_cast<const long long*>(_input + 7);
                return resend;
            }

            throw ctl::ctException(
                ERROR_INVALID_DATA,
                ctl::ctString::format_string(
                    L"Invalid MediaStream message: %S",
                    buffer.c_str()).c_str(),
                L"ctsMediaStreamMessage",
                true);
        }
    };
}

