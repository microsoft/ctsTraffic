/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <stdexcept>
#include <utility>
#include <vector>
#include <memory>
// os headers
// ReSharper disable once CppUnusedIncludeDirective
#include <WinSock2.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <ws2ipdef.h>
#include <iphlpapi.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include "ctSockaddr.hpp"

namespace ctl
{
    class ctNetAdapterAddresses
    {
    public:
        // ReSharper disable once CppInconsistentNaming
        class iterator
        {
        public:
            iterator() = default;

            explicit iterator(std::shared_ptr<std::vector<BYTE>> ipAdapter) noexcept
                : m_buffer(std::move(ipAdapter))
            {
                if (m_buffer && !m_buffer->empty())
                {
                    m_current = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&this->m_buffer->at(0));
                }
            }

            void swap(_Inout_ iterator& rhs) noexcept
            {
                using std::swap;
                swap(this->m_buffer, rhs.m_buffer);
                swap(this->m_current, rhs.m_current);
            }

            IP_ADAPTER_ADDRESSES& operator*() const
            {
                if (!this->m_current)
                {
                    throw std::out_of_range("out_of_range: ctNetAdapterAddresses::iterator::operator*");
                }
                return *this->m_current;
            }

            IP_ADAPTER_ADDRESSES* operator->() const
            {
                if (!this->m_current)
                {
                    throw std::out_of_range("out_of_range: ctNetAdapterAddresses::iterator::operator->");
                }
                return this->m_current;
            }

            bool operator==(const iterator& iter) const noexcept
            {
                // for comparison of 'end' iterators, just look at current
                if (!this->m_current)
                {
                    return this->m_current == iter.m_current;
                }

                return this->m_buffer == iter.m_buffer &&
                    this->m_current == iter.m_current;
            }

            bool operator!=(const iterator& iter) const noexcept
            {
                return !(*this == iter);
            }

            iterator& operator++()
            {
                if (!this->m_current)
                {
                    throw std::out_of_range("out_of_range: ctNetAdapterAddresses::iterator::operator++");
                }
                // increment
                m_current = m_current->Next;
                return *this;
            }

            iterator operator++(int)
            {
                // ReSharper disable once CppUseAuto
                iterator temp(*this);
                ++* this;
                return temp;
            }

            iterator& operator+=(DWORD _inc)
            {
                for (unsigned loop = 0; loop < _inc && this->m_current != nullptr; ++loop)
                {
                    m_current = m_current->Next;
                }
                if (!this->m_current)
                {
                    throw std::out_of_range("out_of_range: ctNetAdapterAddresses::iterator::operator+=");
                }
                return *this;
            }

            ////////////////////////////////////////////////////////////////////////////////
            ///
            /// iterator_traits
            /// - allows <algorithm> functions to be used
            ///
            ////////////////////////////////////////////////////////////////////////////////
            typedef std::forward_iterator_tag iterator_category;
            typedef IP_ADAPTER_ADDRESSES value_type;
            typedef int difference_type;
            typedef IP_ADAPTER_ADDRESSES* pointer;
            typedef IP_ADAPTER_ADDRESSES& reference;

        private:
            std::shared_ptr<std::vector<BYTE>> m_buffer;
            PIP_ADAPTER_ADDRESSES m_current = nullptr;
        };

    public:

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// c'tor
        ///
        /// - default d'tor, copy c'tor, and copy assignment
        /// - Takes an optional _gaaFlags argument which is passed through directly to
        ///   GetAdapterAddresses internally - use standard GAA_FLAG_* constants
        ///
        ////////////////////////////////////////////////////////////////////////////////
        explicit ctNetAdapterAddresses(unsigned _family = AF_UNSPEC, DWORD _gaaFlags = 0) :
            m_buffer(std::make_shared<std::vector<BYTE>>(16384))
        {
            this->refresh(_family, _gaaFlags);
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// refresh
        ///
        /// - retrieves the current set of adapter address information
        /// - Takes an optional _gaaFlags argument which is passed through directly to
        ///   GetAdapterAddresses internally - use standard GAA_FLAG_* constants
        ///
        /// NOTE: this will invalidate any iterators from this instance
        /// NOTE: this only implements the Basic exception guarantee
        ///       if this fails, an exception is thrown, and any prior
        ///       information is lost. This is still safe to call after errors.
        ///
        ////////////////////////////////////////////////////////////////////////////////
        void refresh(unsigned family = AF_UNSPEC, DWORD gaaFlags = 0) const
        {
            // get both v4 and v6 adapter info
            auto byteSize = static_cast<ULONG>(this->m_buffer->size());
            auto err = GetAdaptersAddresses(
                family,   // Family
                gaaFlags, // Flags
                nullptr,   // Reserved
                reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&this->m_buffer->at(0)),
                &byteSize
            );
            if (err == ERROR_BUFFER_OVERFLOW)
            {
                this->m_buffer->resize(byteSize);
                err = GetAdaptersAddresses(
                    family,   // Family
                    gaaFlags, // Flags
                    nullptr,   // Reserved
                    reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&this->m_buffer->at(0)),
                    &byteSize
                );
            }
            if (err != NO_ERROR)
            {
                THROW_WIN32_MSG(err, "GetAdaptersAddresses");
            }
        }

        [[nodiscard]] iterator begin() const noexcept
        {
            return iterator(this->m_buffer);
        }

        // ReSharper disable once CppMemberFunctionMayBeStatic
        [[nodiscard]] iterator end() const noexcept
        {
            return iterator();
        }

    private:
        ///
        /// private members
        ///
        std::shared_ptr<std::vector<BYTE>> m_buffer;
    };

    /// functor ctNetAdapterMatchingAddrPredicate
    ///
    /// Created to leverage STL algorigthms to parse a ctNetAdapterAddresses set of iterators
    /// - to find the first interface that has the specified address assigned
    struct ctNetAdapterMatchingAddrPredicate
    {
        explicit ctNetAdapterMatchingAddrPredicate(ctSockaddr addr) noexcept :
            m_targetAddr(std::move(addr))
        {
        }

        bool operator ()(const IP_ADAPTER_ADDRESSES& ipAddress) const noexcept
        {
            for (auto* unicastAddress = ipAddress.FirstUnicastAddress;
                 unicastAddress != nullptr;
                 unicastAddress = unicastAddress->Next)
            {
                const ctSockaddr unicastSockaddr(&unicastAddress->Address);
                if (unicastSockaddr == m_targetAddr)
                {
                    return true;
                }
            }
            return false;
        }

    private:
        const ctSockaddr m_targetAddr;
    };
} // namespace ctl
