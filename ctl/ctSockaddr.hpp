/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// CPP headers
#include <cassert>
#include <algorithm>
#include <string>
#include <vector>
// OS headers
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>
// CTL headers
#include "ctScopeGuard.hpp"
#include "ctException.hpp"

#pragma prefast(push)
// ignore prefast IPv4 warnings
#pragma prefast(disable: 24002)
// ignore IDN warnings when explicitly asking to resolve a short-string
#pragma prefast(disable: 38026)

namespace ctl {


    class ctSockaddr {
    public:

        static
        std::vector<ctSockaddr> ResolveName(__in LPCWSTR _name)
        {
            ADDRINFOW* addr_result = nullptr;
            ctlScopeGuard(freeAddrOnExit, { if (addr_result) ::FreeAddrInfoW(addr_result); });

            std::vector<ctSockaddr> return_addrs;
            if (0 == ::GetAddrInfoW(_name, nullptr, nullptr, &addr_result)) {
                for (ADDRINFOW* addrinfo = addr_result; addrinfo != nullptr; addrinfo = addrinfo->ai_next) {
                    return_addrs.push_back(ctSockaddr(addrinfo->ai_addr, static_cast<int>(addrinfo->ai_addrlen)));
                }
            } else {
                throw ctException(::WSAGetLastError(), L"GetAddrInfoW", L"ctl::ctSockaddr::ResolveName", false);
            }

            return return_addrs;
        }

        enum ByteOrder {
            HostOrder,
            NetworkOrder
        };

        //
        // constructors can throw if WSAStartup fails under low-resources
        // - copy c'tor and copy assignment can't fail
        //
        ctSockaddr(__in short family = AF_UNSPEC);

        explicit ctSockaddr(__in_bcount(inLength) const SOCKADDR* inAddr, __in int inLength) throw();
        explicit ctSockaddr(__in_bcount(inLength) const SOCKADDR* inAddr, __in size_t inLength) throw();
        explicit ctSockaddr(__in const SOCKADDR_IN*) throw();
        explicit ctSockaddr(__in const SOCKADDR_IN6*) throw();
        explicit ctSockaddr(__in const SOCKADDR_INET*) throw();
        explicit ctSockaddr(__in const SOCKADDR_STORAGE*) throw();
        explicit ctSockaddr(__in const SOCKET_ADDRESS*) throw();

        ctSockaddr(__in const ctSockaddr&) throw();
        ctSockaddr& operator=(__in const ctSockaddr&) throw();

        bool operator==(__in const ctSockaddr&) const throw();
        bool operator!=(__in const ctSockaddr&) const throw();

        virtual ~ctSockaddr() throw();

        void reset(__in short family = AF_UNSPEC) throw();

        void swap(__inout ctSockaddr&) throw();

        bool setSocketAddress(__in SOCKET) throw();

        void setSockaddr(__in_bcount(length) const SOCKADDR*, __in int length) throw();
        void setSockaddr(__in const SOCKADDR_IN*) throw();
        void setSockaddr(__in const SOCKADDR_IN6*) throw();
        void setSockaddr(__in const SOCKADDR_INET*) throw();
        void setSockaddr(__in const SOCKADDR_STORAGE*) throw();
        void setSockaddr(__in const SOCKET_ADDRESS*) throw();

        void setPort(__in unsigned short, __in ByteOrder = HostOrder) throw();

        // for dual-mode sockets, when needing to explicitly connect to the target v4 address,
        // - one must map the v4 address to its mapped v6 address
        void mapDualMode4To6() throw();

        // setting by string returns a bool if was able to convert to an address
        bool setAddress(__in PCWSTR wszAddr) throw();
        bool setAddress(__in LPCSTR szAddr) throw();

        void setAddress(__in const IN_ADDR*) throw();
        void setAddress(__in const IN6_ADDR*) throw();

        void setFlowInfo(__in unsigned long) throw();
        void setScopeId(__in unsigned long) throw();

        void setAddressLoopback() throw();
        void setAddressAny() throw();

        bool isAddressLoopback() const throw();
        bool isAddressAny() const throw();

        std::wstring writeAddress(__in bool trim_scope = false) const;
        bool writeAddress(__out std::wstring& wsReturn, __in bool trim_scope = false) const;
        bool writeAddress(__out std::string& sReturn, __in bool trim_scope = false) const;
        std::wstring writeCompleteAddress(__in bool trim_scope = false) const;
        bool writeCompleteAddress(__out std::wstring& wsReturn, __in bool trim_scope = false) const;
        bool writeCompleteAddress(__out std::string& sReturn, __in bool trim_scope = false) const;

        //
        // Accessors
        //
        int               length() const throw();
        unsigned short    port() const throw();
        short             family() const throw();
        unsigned long     flowinfo() const throw();
        unsigned long     scopeId() const throw();

        // returning non-const from const methods, for API compatibility
        SOCKADDR*         sockaddr() const throw();
        SOCKADDR_IN*      sockaddr_in() const throw();
        SOCKADDR_IN6*     sockaddr_in6() const throw();
        SOCKADDR_INET*    sockaddr_inet() const throw();
        SOCKADDR_STORAGE* sockaddr_storage() const throw();
        IN_ADDR*          in_addr() const throw();
        IN6_ADDR*         in6_addr() const throw();

    private:
        SOCKADDR_STORAGE saddr;

        static int saddr_size()
        {
            return static_cast<int>(sizeof(SOCKADDR_STORAGE));
        }
    };

    //
    // non-member swap
    //
    inline
    void swap(__inout ctSockaddr& left_, __inout ctSockaddr& right_) throw()
    {
        left_.swap(right_);
    }


    inline
    ctSockaddr::ctSockaddr(__in short family) throw()
    {
        ::ZeroMemory(&saddr, saddr_size());
        saddr.ss_family = family;
    }

    inline
    ctSockaddr::ctSockaddr(__in_bcount(inLength) const SOCKADDR* inAddr, __in int inLength) throw()
    {
        assert(inLength <= saddr_size());

        size_t length = (inLength <= saddr_size()) ?
        inLength :
            saddr_size();

        ::ZeroMemory(&saddr, saddr_size());
        ::CopyMemory(
            &saddr,
            inAddr,
            length
            );
    }
    inline
    ctSockaddr::ctSockaddr(__in_bcount(inLength) const SOCKADDR* inAddr, __in size_t inLength) throw()
    {
        assert(static_cast<int>(inLength) <= saddr_size());

        size_t length = (static_cast<int>(inLength) <= saddr_size()) ?
        inLength :
            saddr_size();

        ::ZeroMemory(&saddr, saddr_size());
        ::CopyMemory(
            &saddr,
            inAddr,
            length
            );
    }

    inline
    ctSockaddr::ctSockaddr(__in const SOCKADDR_IN* inAddr) throw()
    {
        ::ZeroMemory(&saddr, saddr_size());
        ::CopyMemory(
            &saddr,
            inAddr,
            sizeof(SOCKADDR_IN)
            );
    }
    inline
    ctSockaddr::ctSockaddr(__in const SOCKADDR_IN6* inAddr) throw()
    {
        ::ZeroMemory(&saddr, saddr_size());
        ::CopyMemory(
            &saddr,
            inAddr,
            sizeof(SOCKADDR_IN6)
            );
    }
    inline
    ctSockaddr::ctSockaddr(__in const SOCKADDR_INET* inAddr) throw()
    {
        ::ZeroMemory(&saddr, saddr_size());
        if (AF_INET == inAddr->si_family) {
            ::CopyMemory(
                &saddr,
                inAddr,
                sizeof(SOCKADDR_IN)
                );
        } else {
            ::CopyMemory(
                &saddr,
                inAddr,
                sizeof(SOCKADDR_IN6)
                );
        }
    }
    inline
    ctSockaddr::ctSockaddr(__in const SOCKADDR_STORAGE* inAddr) throw()
    {
        ::CopyMemory(
            &saddr,
            inAddr,
            saddr_size()
            );
    }
    inline
    ctSockaddr::ctSockaddr(__in const SOCKET_ADDRESS* inAddr) throw()
    {
        assert(inAddr->iSockaddrLength <= saddr_size());

        size_t length = (inAddr->iSockaddrLength <= saddr_size()) ?
            inAddr->iSockaddrLength :
            saddr_size();

        ::ZeroMemory(&saddr, saddr_size());
        ::CopyMemory(
            &saddr,
            inAddr->lpSockaddr,
            length
            );
    }

    inline
    ctSockaddr::ctSockaddr(__in const ctSockaddr& inAddr) throw()
    {
        ::CopyMemory(
            &saddr,
            &(inAddr.saddr),
            saddr_size()
            );
    }
    inline
    ctSockaddr& ctSockaddr::operator=(__in const ctSockaddr& inAddr) throw()
    {
        // copy and swap
        ctSockaddr temp(inAddr);
        this->swap(temp);
        return *this;
    }

    inline
    bool ctSockaddr::operator==(__in const ctSockaddr& _inAddr) const throw()
    {
        return (0 == ::memcmp(&this->saddr, &_inAddr.saddr, this->saddr_size()));
    }
    inline
    bool ctSockaddr::operator!=(__in const ctSockaddr& _inAddr) const throw()
    {
        return !(*this == _inAddr);
    }

    inline
    ctSockaddr::~ctSockaddr() throw()
    {
        // empty
    }

    inline
    void ctSockaddr::reset(__in short family) throw()
    {
        ::ZeroMemory(&saddr, saddr_size());
        saddr.ss_family = family;
    }

    inline
    void ctSockaddr::swap(__inout ctSockaddr& inAddr) throw()
    {
        using std::swap;
        swap(saddr, inAddr.saddr);
    }

    inline
    bool ctSockaddr::setSocketAddress(__in SOCKET s) throw()
    {
        int namelen = this->length();
        return (0 == ::getsockname(s, this->sockaddr(), &namelen));
    }

    inline
    void ctSockaddr::setSockaddr(__in_bcount(inLength) const SOCKADDR* inAddr, __in int inLength) throw()
    {
        assert(inLength <= saddr_size());

        size_t length = (inLength <= saddr_size()) ?
        inLength :
            saddr_size();

        ::ZeroMemory(&saddr, saddr_size());
        ::CopyMemory(
            &saddr,
            inAddr,
            length
            );
    }
    inline
    void ctSockaddr::setSockaddr(__in const SOCKADDR_IN* inAddr) throw()
    {
        ::ZeroMemory(&saddr, saddr_size());
        ::CopyMemory(
            &saddr,
            inAddr,
            sizeof(SOCKADDR_IN)
            );
    }
    inline
    void ctSockaddr::setSockaddr(__in const SOCKADDR_IN6* inAddr) throw()
    {
        ::ZeroMemory(&saddr, saddr_size());
        ::CopyMemory(
            &saddr,
            inAddr,
            sizeof(SOCKADDR_IN6)
            );
    }
    inline
    void ctSockaddr::setSockaddr(__in const SOCKADDR_INET* inAddr) throw()
    {
        ::ZeroMemory(&saddr, saddr_size());
        if (AF_INET == inAddr->si_family) {
            ::CopyMemory(
                &saddr,
                inAddr,
                sizeof(SOCKADDR_IN)
                );
        } else {
            ::CopyMemory(
                &saddr,
                inAddr,
                sizeof(SOCKADDR_IN6)
                );
        }
    }
    inline
    void ctSockaddr::setSockaddr(__in const SOCKADDR_STORAGE* inAddr) throw()
    {
        ::CopyMemory(
            &saddr,
            inAddr,
            saddr_size()
            );
    }
    inline
    void ctSockaddr::setSockaddr(__in const SOCKET_ADDRESS* inAddr) throw()
    {
        assert(inAddr->iSockaddrLength <= saddr_size());

        size_t length = (inAddr->iSockaddrLength <= saddr_size()) ?
            inAddr->iSockaddrLength :
            saddr_size();

        ::ZeroMemory(&saddr, saddr_size());
        ::CopyMemory(
            &saddr,
            inAddr->lpSockaddr,
            length
            );
    }

    inline
    void ctSockaddr::setAddressLoopback() throw()
    {
        if (AF_INET == saddr.ss_family) {
            PSOCKADDR_IN in4 = reinterpret_cast<PSOCKADDR_IN>(&saddr);
            unsigned short in4_port = in4->sin_port;
            ::ZeroMemory(&saddr, saddr_size());

            in4->sin_family = AF_INET;
            in4->sin_port = in4_port;
            in4->sin_addr.s_addr = 0x0100007f; // htons(INADDR_LOOPBACK);
        } else if (AF_INET6 == saddr.ss_family) {
            PSOCKADDR_IN6 in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
            unsigned short in6_port = in6->sin6_port;
            ::ZeroMemory(&saddr, saddr_size());

            in6->sin6_family = AF_INET6;
            in6->sin6_port = in6_port;
            in6->sin6_addr.s6_bytes[15] = 1; // IN6ADDR_LOOPBACK_INIT;
        } else {
            assert(!"ctSockaddr::setAddressLoopback - Unknown family");
        }
    }
    inline
    void ctSockaddr::setAddressAny() throw()
    {
        if (AF_INET == saddr.ss_family) {
            PSOCKADDR_IN in4 = reinterpret_cast<PSOCKADDR_IN>(&saddr);
            unsigned short in4_port = in4->sin_port;
            ::ZeroMemory(&saddr, saddr_size());

            in4->sin_family = AF_INET;
            in4->sin_port = in4_port;
        } else if (AF_INET6 == saddr.ss_family) {
            PSOCKADDR_IN6 in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
            unsigned short in6_port = in6->sin6_port;
            ::ZeroMemory(&saddr, saddr_size());

            in6->sin6_family = AF_INET6;
            in6->sin6_port = in6_port;
        }
    }
    inline
    bool ctSockaddr::isAddressLoopback() const throw()
    {
        ctSockaddr loopback(*this);
        loopback.setAddressLoopback();
        return (0 == ::memcmp(&(loopback.saddr), &(this->saddr), sizeof(SOCKADDR_STORAGE)));
    }
    inline
    bool ctSockaddr::isAddressAny() const throw()
    {
        ctSockaddr any_addr(*this);
        any_addr.setAddressAny();
        return (0 == ::memcmp(&(any_addr.saddr), &(this->saddr), sizeof(SOCKADDR_STORAGE)));
    }

    inline
    void ctSockaddr::setPort(__in unsigned short port, __in ByteOrder order) throw()
    {
        PSOCKADDR_IN addr_in = reinterpret_cast<PSOCKADDR_IN>(&saddr);
        addr_in->sin_port = (order == HostOrder) ? ::htons(port) : port;
    }

    inline
    void ctSockaddr::mapDualMode4To6() throw()
    {
        static const IN6_ADDR v4MappedPrefix = IN6ADDR_V4MAPPEDPREFIX_INIT;

        ctSockaddr tempV6(AF_INET6);
        IN6_ADDR* a6 = tempV6.in6_addr();
        IN_ADDR*  a4 = this->in_addr();

        *a6 = v4MappedPrefix;
        a6->u.Byte[12] = a4->S_un.S_un_b.s_b1;
        a6->u.Byte[13] = a4->S_un.S_un_b.s_b2;
        a6->u.Byte[14] = a4->S_un.S_un_b.s_b3;
        a6->u.Byte[15] = a4->S_un.S_un_b.s_b4;

        tempV6.setPort(this->port());
        this->swap(tempV6);
    }

    inline
    bool ctSockaddr::setAddress(__in LPCWSTR wszAddr) throw()
    {
        ADDRINFOW addr_hints;
        ::ZeroMemory(&addr_hints, sizeof(addr_hints));
        addr_hints.ai_flags = AI_NUMERICHOST;

        ADDRINFOW* addr_result = nullptr;
        if (0 == ::GetAddrInfoW(wszAddr, nullptr, &addr_hints, &addr_result)) {
            this->setSockaddr(addr_result->ai_addr, static_cast<int>(addr_result->ai_addrlen));
            ::FreeAddrInfoW(addr_result);
            return true;
        } else {
            return false;
        }
    }

    inline
    bool ctSockaddr::setAddress(__in LPCSTR szAddr) throw()
    {
        ADDRINFOA addr_hints;
        ::ZeroMemory(&addr_hints, sizeof(addr_hints));
        addr_hints.ai_flags = AI_NUMERICHOST;

        ADDRINFOA* addr_result = nullptr;
#pragma prefast(suppress:38026, "The explicit use of AI_NUMERICHOST makes GetAddrInfoA's lack of IDN support irrelevant here - they wouldn't be accepted even if we used GetAddrInfoW")
        if (0 == ::GetAddrInfoA(szAddr, nullptr, &addr_hints, &addr_result)) {
            this->setSockaddr(addr_result->ai_addr, static_cast<int>(addr_result->ai_addrlen));
            FreeAddrInfoA(addr_result);
            return true;
        } else {
            return false;
        }
    }

    inline
    void ctSockaddr::setAddress(__in const IN_ADDR* inAddr) throw()
    {
        saddr.ss_family = AF_INET;
        PSOCKADDR_IN addr_in = reinterpret_cast<PSOCKADDR_IN>(&saddr);
        addr_in->sin_addr = *inAddr;
    }
    inline
    void ctSockaddr::setAddress(__in const IN6_ADDR* inAddr) throw()
    {
        saddr.ss_family = AF_INET6;
        PSOCKADDR_IN6 addr_in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
        addr_in6->sin6_addr = *inAddr;
    }

    inline
    void ctSockaddr::setFlowInfo(__in unsigned long flowinfo) throw()
    {
        assert(AF_INET6 == saddr.ss_family);
        if (AF_INET6 == saddr.ss_family) {
            PSOCKADDR_IN6 addr_in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
            addr_in6->sin6_flowinfo = flowinfo;
        }
    }
    inline
    void ctSockaddr::setScopeId(__in unsigned long scopeid) throw()
    {
        assert(AF_INET6 == saddr.ss_family);
        if (AF_INET6 == saddr.ss_family) {
            PSOCKADDR_IN6 addr_in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
            addr_in6->sin6_scope_id = scopeid;
        }
    }

    inline
    std::wstring ctSockaddr::writeAddress(__in bool trim_scope) const
    {
        std::wstring return_string;
        this->writeAddress(return_string, trim_scope);
        return return_string;
    }
    inline
    bool ctSockaddr::writeAddress(__out std::wstring& wsReturn, __in bool trim_scope) const
    {
        WCHAR address[64];
        size_t addressLength = 64;

        if (nullptr != ::InetNtopW(
            saddr.ss_family,
            (AF_INET == saddr.ss_family) ? reinterpret_cast<PVOID>(this->in_addr()) : reinterpret_cast<PVOID>(this->in6_addr()),
            address,
            addressLength
            )) {
            if ((this->family() == AF_INET6) && trim_scope) {
                WCHAR* end = address + addressLength;
                WCHAR* scope_ptr = std::find(address, end, L'%');
                if (scope_ptr != end) {
                    WCHAR* move_ptr = std::find(address, end, L']');
                    if (move_ptr != end) {
                        while (move_ptr != end) {
                            *scope_ptr = *move_ptr;
                            ++scope_ptr;
                            ++move_ptr;
                        }
                    } else {
                        // no port was appended
                        while (scope_ptr != end) {
                            *scope_ptr = L'\0';
                            ++scope_ptr;
                        }
                    }
                }
            }
            wsReturn.assign(address, std::find(address, address + addressLength, 0x00));
            return true;
        }
        wsReturn.clear();
        return false;
    }

    inline
    bool ctSockaddr::writeAddress(__out std::string& sReturn, __in bool trim_scope) const
    {
        CHAR address[64];
        DWORD addressLength = 64;

        if (NULL != ::InetNtopA(
            saddr.ss_family,
            (AF_INET == saddr.ss_family) ? reinterpret_cast<PVOID>(this->in_addr()) : reinterpret_cast<PVOID>(this->in6_addr()),
            address,
            addressLength
            )) {
            if ((this->family() == AF_INET6) && trim_scope) {
                CHAR* end = address + addressLength;
                CHAR* scope_ptr = std::find(address, end, '%');
                if (scope_ptr != end) {
                    CHAR* move_ptr = std::find(address, end, ']');
                    if (move_ptr != end) {
                        while (move_ptr != end) {
                            *scope_ptr = *move_ptr;
                            ++scope_ptr;
                            ++move_ptr;
                        }
                    } else {
                        // no port was appended
                        while (scope_ptr != end) {
                            *scope_ptr = '\0';
                            ++scope_ptr;
                        }
                    }
                }
            }
            sReturn.assign(address, std::find(address, address + addressLength, 0x0));
            return true;
        }
        sReturn.clear();
        return false;
    }

    inline
    std::wstring ctSockaddr::writeCompleteAddress(__in bool trim_scope) const
    {
        std::wstring return_string;
        this->writeCompleteAddress(return_string, trim_scope);
        return return_string;
    }
    inline
    bool ctSockaddr::writeCompleteAddress(__out std::wstring& wsReturn, __in bool trim_scope) const
    {
        WCHAR address[64];
        DWORD addressLength = 64;

        if (0 == ::WSAAddressToStringW(
            this->sockaddr(),
            saddr_size(),
            nullptr,
            address,
            &addressLength
            )) {
            if ((this->family() == AF_INET6) && trim_scope) {
                WCHAR* end = address + addressLength;
                WCHAR* scope_ptr = std::find(address, end, L'%');
                if (scope_ptr != end) {
                    WCHAR* move_ptr = std::find(address, end, L']');
                    if (move_ptr != end) {
                        while (move_ptr != end) {
                            *scope_ptr = *move_ptr;
                            ++scope_ptr;
                            ++move_ptr;
                        }
                    } else {
                        // no port was appended
                        while (scope_ptr != end) {
                            *scope_ptr = L'\0';
                            ++scope_ptr;
                        }
                    }
                }
            }
            wsReturn.assign(address, std::find(address, address + addressLength, 0x00));
            return true;
        }
        wsReturn.clear();
        return false;
    }

    inline
    bool ctSockaddr::writeCompleteAddress(__out std::string& sReturn, __in bool trim_scope) const
    {
        CHAR address[64];
        DWORD addressLength = 64;

        if (0 == ::WSAAddressToStringA(
            this->sockaddr(),
            saddr_size(),
            nullptr,
            address,
            &addressLength
            )) {
            if ((this->family() == AF_INET6) && trim_scope) {
                CHAR* end = address + addressLength;
                CHAR* scope_ptr = std::find(address, end, '%');
                if (scope_ptr != end) {
                    CHAR* move_ptr = std::find(address, end, ']');
                    if (move_ptr != end) {
                        while (move_ptr != end) {
                            *scope_ptr = *move_ptr;
                            ++scope_ptr;
                            ++move_ptr;
                        }
                    } else {
                        // no port was appended
                        while (scope_ptr != end) {
                            *scope_ptr = '\0';
                            ++scope_ptr;
                        }
                    }
                }
            }
            sReturn.assign(address, std::find(address, address + addressLength, 0x0));
            return true;
        }
        sReturn.clear();
        return false;
    }

    inline
    int ctSockaddr::length() const throw()
    {
        return static_cast<int>(saddr_size());
    }

    inline
    short ctSockaddr::family() const throw()
    {
        return saddr.ss_family;
    }
    inline
    unsigned short ctSockaddr::port() const throw()
    {
        const SOCKADDR_IN* addr_in = reinterpret_cast<const SOCKADDR_IN*>(&saddr);
        return ::ntohs(addr_in->sin_port);
    }
    inline
    unsigned long ctSockaddr::flowinfo() const throw()
    {
        if (AF_INET6 == saddr.ss_family) {
            const SOCKADDR_IN6* addr_in6 = reinterpret_cast<const SOCKADDR_IN6*>(&saddr);
            return addr_in6->sin6_flowinfo;
        } else {
            return 0;
        }
    }
    inline
    unsigned long ctSockaddr::scopeId() const throw()
    {
        if (AF_INET6 == saddr.ss_family) {
            const SOCKADDR_IN6* addr_in6 = reinterpret_cast<const SOCKADDR_IN6*>(&saddr);
            return addr_in6->sin6_scope_id;
        } else {
            return 0;
        }
    }

    inline
    SOCKADDR* ctSockaddr::sockaddr() const throw()
    {
        return const_cast<SOCKADDR*>(
            reinterpret_cast<const SOCKADDR*>(&saddr));
    }
    inline
    SOCKADDR_IN* ctSockaddr::sockaddr_in() const throw()
    {
        return const_cast<SOCKADDR_IN*>(
            reinterpret_cast<const SOCKADDR_IN*>(&saddr));
    }
    inline
    SOCKADDR_IN6* ctSockaddr::sockaddr_in6() const throw()
    {
        return const_cast<SOCKADDR_IN6*>(
            reinterpret_cast<const SOCKADDR_IN6*>(&saddr));
    }
    inline
    SOCKADDR_INET* ctSockaddr::sockaddr_inet() const throw()
    {
        return const_cast<SOCKADDR_INET*>(
            reinterpret_cast<const SOCKADDR_INET*>(&saddr));
    }
    inline
    SOCKADDR_STORAGE* ctSockaddr::sockaddr_storage() const throw()
    {
        return const_cast<SOCKADDR_STORAGE*>(&saddr);
    }
    inline
    IN_ADDR* ctSockaddr::in_addr() const throw()
    {
        const SOCKADDR_IN* addr_in = reinterpret_cast<const SOCKADDR_IN*>(&saddr);
        return const_cast<IN_ADDR*>(&(addr_in->sin_addr));
    }
    inline
    IN6_ADDR* ctSockaddr::in6_addr() const throw()
    {
        const SOCKADDR_IN6* addr_in6 = reinterpret_cast<const SOCKADDR_IN6*>(&saddr);
        return const_cast<IN6_ADDR*>(&(addr_in6->sin6_addr));
    }

}; // namespace ctl

#pragma prefast(pop)
