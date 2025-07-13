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
#include <algorithm>
#include <string>
#include <vector>
// os headers
#include <Windows.h>
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <WS2tcpip.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>

#pragma prefast(push)
// ignore prefast IPv4 warnings
#pragma prefast(disable: 24002)
// ignore IDN warnings when explicitly asking to resolve a short-string
#pragma prefast(disable: 38026)

namespace ctl
{
enum class ByteOrder : std::uint8_t
{
    HostOrder,
    NetworkOrder
};

class ctSockaddr final
{
public:
    static constexpr DWORD FixedStringLength = INET6_ADDRSTRLEN;
    
    static std::vector<ctSockaddr> ResolveName(_In_ PCWSTR name)
    {
        ADDRINFOW* addrResult = nullptr;
        auto freeAddrOnExit = wil::scope_exit([&]() noexcept {
            if (addrResult)
            {
                FreeAddrInfoW(addrResult);
            }
        });

        std::vector<ctSockaddr> returnAddrs;
        if (0 == GetAddrInfoW(name, nullptr, nullptr, &addrResult))
        {
            for (auto* pAddrInfo = addrResult; pAddrInfo != nullptr; pAddrInfo = pAddrInfo->ai_next)
            {
                returnAddrs.emplace_back(pAddrInfo->ai_addr, pAddrInfo->ai_addrlen);
            }
        }
        else
        {
            THROW_WIN32_MSG(WSAGetLastError(), "GetAddrInfoW");
        }

        return returnAddrs;
    }

    // for dual-mode sockets, when needing to explicitly connect to the target v4 address,
    // - one must map the v4 address to its mapped v6 address
    static ctSockaddr MapDualMode4To6(const ctSockaddr& inV4) noexcept
    {
        constexpr IN6_ADDR v4MappedPrefix{{IN6ADDR_V4MAPPEDPREFIX_INIT}};

        ctSockaddr outV6(&v4MappedPrefix, inV4.port());
        auto* const pIn6Addr = outV6.in6_addr();
        const auto* const pIn4Addr = inV4.in_addr();

        pIn6Addr->u.Byte[12] = pIn4Addr->S_un.S_un_b.s_b1;
        pIn6Addr->u.Byte[13] = pIn4Addr->S_un.S_un_b.s_b2;
        pIn6Addr->u.Byte[14] = pIn4Addr->S_un.S_un_b.s_b3;
        pIn6Addr->u.Byte[15] = pIn4Addr->S_un.S_un_b.s_b4;

        return outV6;
    }

    enum class AddressType : std::uint8_t
    {
        Loopback,
        Any
    };

    explicit ctSockaddr(ADDRESS_FAMILY family = AF_UNSPEC, AddressType type = AddressType::Any) noexcept;
    explicit ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept;
    explicit ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, size_t inLength) noexcept;
    explicit ctSockaddr(const SOCKADDR_IN*) noexcept;
    explicit ctSockaddr(const SOCKADDR_IN6*) noexcept;
    explicit ctSockaddr(const SOCKADDR_INET*) noexcept;
    explicit ctSockaddr(const SOCKET_ADDRESS*) noexcept;
    explicit ctSockaddr(const IN_ADDR*, uint16_t port = 0, ByteOrder order = ByteOrder::HostOrder) noexcept;
    explicit ctSockaddr(const IN6_ADDR*, uint16_t port = 0, ByteOrder order = ByteOrder::HostOrder) noexcept;

    ~ctSockaddr() = default;

    ctSockaddr(const ctSockaddr&) noexcept;
    ctSockaddr& operator=(const ctSockaddr&) noexcept;
    ctSockaddr(ctSockaddr&&) noexcept;
    ctSockaddr& operator=(ctSockaddr&&) noexcept;

    bool operator==(const ctSockaddr&) const noexcept;
    bool operator!=(const ctSockaddr&) const noexcept;
    bool operator<(const ctSockaddr&) const noexcept;
    bool operator>(const ctSockaddr&) const noexcept;

    void reset(ADDRESS_FAMILY family = AF_UNSPEC) noexcept;
    void reset(ADDRESS_FAMILY, AddressType) noexcept;
    void swap(_Inout_ ctSockaddr&) noexcept;

    void setSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept;
    void setSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, size_t inLength) noexcept;
    void setSockaddr(const SOCKADDR_IN*) noexcept;
    void setSockaddr(const SOCKADDR_IN6*) noexcept;
    void setSockaddr(const SOCKADDR_INET*) noexcept;
    void setSockaddr(const SOCKET_ADDRESS*) noexcept;

    // setting by string returns a bool if was able to convert to an address
    bool setAddress(_In_ PCWSTR) noexcept;
    bool setAddress(_In_ PCSTR) noexcept;
    void setAddress(const IN_ADDR*) noexcept;
    void setAddress(const IN6_ADDR*) noexcept;
    [[nodiscard]] bool setAddress(SOCKET) noexcept;

    void setPort(uint16_t, ByteOrder = ByteOrder::HostOrder) noexcept;
    void setScopeId(uint32_t) noexcept;
    void setFlowInfo(uint32_t) noexcept;

    [[nodiscard]] bool isAddressLinkLocal() const noexcept;

    void setAddressAny() noexcept;
    [[nodiscard]] bool isAddressAny() const noexcept;

    void setAddressLoopback() noexcept;
    [[nodiscard]] bool isAddressLoopback() const noexcept;

    // writeAddress prints the IP address portion, not the scope id or port
    [[nodiscard]] std::wstring writeAddress() const;
    bool writeAddress(WCHAR (&address)[FixedStringLength]) const noexcept;
    bool writeAddress(CHAR (&address)[FixedStringLength]) const noexcept;

    // writeCompleteAddress prints the IP address, scope id, and port
    [[nodiscard]] std::wstring writeCompleteAddress(bool trimScope = false) const;
    bool writeCompleteAddress(WCHAR (&address)[FixedStringLength], bool trimScope = false) const noexcept;
    bool writeCompleteAddress(CHAR (&address)[FixedStringLength], bool trimScope = false) const noexcept;

    // Accessors
    [[nodiscard]] int length() const noexcept;
    [[nodiscard]] uint16_t port() const noexcept;
    [[nodiscard]] ADDRESS_FAMILY family() const noexcept;
    [[nodiscard]] uint32_t flowinfo() const noexcept;
    [[nodiscard]] uint32_t scope_id() const noexcept;

    [[nodiscard]] SOCKADDR* sockaddr() noexcept;
    [[nodiscard]] SOCKADDR_IN* sockaddr_in() noexcept;
    [[nodiscard]] SOCKADDR_IN6* sockaddr_in6() noexcept;
    [[nodiscard]] SOCKADDR_INET* sockaddr_inet() noexcept;
    [[nodiscard]] IN_ADDR* in_addr() noexcept;
    [[nodiscard]] IN6_ADDR* in6_addr() noexcept;

    [[nodiscard]] const SOCKADDR* sockaddr() const noexcept;
    [[nodiscard]] const SOCKADDR_IN* sockaddr_in() const noexcept;
    [[nodiscard]] const SOCKADDR_IN6* sockaddr_in6() const noexcept;
    [[nodiscard]] const SOCKADDR_INET* sockaddr_inet() const noexcept;
    [[nodiscard]] const IN_ADDR* in_addr() const noexcept;
    [[nodiscard]] const IN6_ADDR* in6_addr() const noexcept;

private:
    static constexpr size_t c_sockaddrSize = sizeof(SOCKADDR_INET);
    SOCKADDR_INET m_sockaddr{};
};

//
// non-member swap
//
inline void swap(_Inout_ ctSockaddr& left, _Inout_ ctSockaddr& right) noexcept
{
    left.swap(right);
}


inline ctSockaddr::ctSockaddr(ADDRESS_FAMILY family, AddressType type) noexcept
{
    reset(family, type);
}

inline ctSockaddr::ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept
{
    setSockaddr(inAddr, inLength);
}

inline ctSockaddr::ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, size_t inLength) noexcept
{
    setSockaddr(inAddr, inLength);
}

inline ctSockaddr::ctSockaddr(const SOCKADDR_IN* inAddr) noexcept
{
    setSockaddr(inAddr);
}

inline ctSockaddr::ctSockaddr(const SOCKADDR_IN6* inAddr) noexcept
{
    setSockaddr(inAddr);
}

inline ctSockaddr::ctSockaddr(const SOCKADDR_INET* inAddr) noexcept
{
    setSockaddr(inAddr);
}

inline ctSockaddr::ctSockaddr(const SOCKET_ADDRESS* inAddr) noexcept
{
    setSockaddr(inAddr->lpSockaddr, inAddr->iSockaddrLength);
}

inline ctSockaddr::ctSockaddr(const IN_ADDR* inAddr, unsigned short port, ByteOrder order) noexcept
{
    setAddress(inAddr);
    setPort(port, order);
}

inline ctSockaddr::ctSockaddr(const IN6_ADDR* inAddr, unsigned short port, ByteOrder order) noexcept
{
    setAddress(inAddr);
    setPort(port, order);
}

inline ctSockaddr::ctSockaddr(const ctSockaddr& inAddr) noexcept
{
    CopyMemory(&m_sockaddr, &inAddr.m_sockaddr, c_sockaddrSize);
}

inline ctSockaddr& ctSockaddr::operator=(const ctSockaddr& inAddr) noexcept
{
    ctSockaddr temp(inAddr);
    swap(temp);
    return *this;
}

inline ctSockaddr::ctSockaddr(ctSockaddr&& inAddr) noexcept
{
    CopyMemory(&m_sockaddr, &inAddr.m_sockaddr, c_sockaddrSize);
}

inline ctSockaddr& ctSockaddr::operator=(ctSockaddr&& inAddr) noexcept
{
    CopyMemory(&m_sockaddr, &inAddr.m_sockaddr, c_sockaddrSize);
    ZeroMemory(&inAddr.m_sockaddr, c_sockaddrSize);
    return *this;
}

inline bool ctSockaddr::operator==(const ctSockaddr& inAddr) const noexcept
{
	switch (m_sockaddr.si_family)
	{
	case AF_INET:
        // only require the v4 slice of the union to match
		return 0 == memcmp(&m_sockaddr.Ipv4, &inAddr.m_sockaddr.Ipv4, sizeof(SOCKADDR_IN));
	case AF_INET6:
        // only require the v6 slice of the union to match
		return 0 == memcmp(&m_sockaddr.Ipv6, &inAddr.m_sockaddr.Ipv6, sizeof(SOCKADDR_IN6));
	default:
		return 0 == memcmp(&m_sockaddr, &inAddr.m_sockaddr, c_sockaddrSize);
	}
}

inline bool ctSockaddr::operator!=(const ctSockaddr& inAddr) const noexcept
{
    return !(*this == inAddr);
}

inline bool ctSockaddr::operator<(const ctSockaddr& rhs) const noexcept
{
    // Follows the same documented comparison logic as GetTcpTable2 and GetTcp6Table2
    const auto& lhs = *this;
    if (lhs.family() != rhs.family())
    {
        return lhs.family() < rhs.family();
    }

    if (lhs.family() == AF_INET)
    {
        if (lhs.in_addr()->S_un.S_addr < rhs.in_addr()->S_un.S_addr)
        {
            return true;
        }
        if (lhs.in_addr()->S_un.S_addr > rhs.in_addr()->S_un.S_addr)
        {
            return false;
        }
        if (lhs.port() < rhs.port())
        {
            return true;
        }
        if (lhs.port() > rhs.port())
        {
            return false;
        }
        // else they are equal
        return false;
    }

    // AF_INET6
    if (lhs.in6_addr()->u.Word[0] < rhs.in6_addr()->u.Word[0])
    {
        return true;
    }
    if (lhs.in6_addr()->u.Word[1] < rhs.in6_addr()->u.Word[1])
    {
        return true;
    }
    if (lhs.in6_addr()->u.Word[2] < rhs.in6_addr()->u.Word[2])
    {
        return true;
    }
    if (lhs.in6_addr()->u.Word[3] < rhs.in6_addr()->u.Word[3])
    {
        return true;
    }
    if (lhs.in6_addr()->u.Word[4] < rhs.in6_addr()->u.Word[4])
    {
        return true;
    }
    if (lhs.in6_addr()->u.Word[5] < rhs.in6_addr()->u.Word[5])
    {
        return true;
    }
    if (lhs.in6_addr()->u.Word[6] < rhs.in6_addr()->u.Word[6])
    {
        return true;
    }
    if (lhs.in6_addr()->u.Word[7] < rhs.in6_addr()->u.Word[7])
    {
        return true;
    }

    if (lhs.in6_addr()->u.Word[0] > rhs.in6_addr()->u.Word[0])
    {
        return false;
    }
    if (lhs.in6_addr()->u.Word[1] > rhs.in6_addr()->u.Word[1])
    {
        return false;
    }
    if (lhs.in6_addr()->u.Word[2] > rhs.in6_addr()->u.Word[2])
    {
        return false;
    }
    if (lhs.in6_addr()->u.Word[3] > rhs.in6_addr()->u.Word[3])
    {
        return false;
    }
    if (lhs.in6_addr()->u.Word[4] > rhs.in6_addr()->u.Word[4])
    {
        return false;
    }
    if (lhs.in6_addr()->u.Word[5] > rhs.in6_addr()->u.Word[5])
    {
        return false;
    }
    if (lhs.in6_addr()->u.Word[6] > rhs.in6_addr()->u.Word[6])
    {
        return false;
    }
    if (lhs.in6_addr()->u.Word[7] > rhs.in6_addr()->u.Word[7])
    {
        return false;
    }

    if (lhs.scope_id() < rhs.scope_id())
    {
        return true;
    }
    if (lhs.scope_id() > rhs.scope_id())
    {
        return false;
    }

    if (lhs.port() < rhs.port())
    {
        return true;
    }
    if (lhs.port() > rhs.port())
    {
        return false;
    }

    // else they are all equal
    return false;
}

inline bool ctSockaddr::operator>(const ctSockaddr& rhs) const noexcept
{
    return !(*this < rhs);
}

inline void ctSockaddr::reset(ADDRESS_FAMILY family) noexcept
{
    ZeroMemory(&m_sockaddr, c_sockaddrSize);
    m_sockaddr.si_family = family;
}

inline void ctSockaddr::reset(ADDRESS_FAMILY family, AddressType type) noexcept
{
    ZeroMemory(&m_sockaddr, c_sockaddrSize);
    m_sockaddr.si_family = family;

    if (type == AddressType::Loopback)
    {
        if (AF_INET == family)
        {
            m_sockaddr.Ipv4.sin_addr = in4addr_loopback; // from ws2ipdef.h
        }
        else if (AF_INET6 == family)
        {
            m_sockaddr.Ipv6.sin6_addr = in6addr_loopback; // from ws2ipdef.h
        }
        else
        {
            FAIL_FAST_MSG("ctSockaddr: unknown family creating a loopback sockaddr");
        }
    }
}

inline void ctSockaddr::swap(_Inout_ ctSockaddr& inAddr) noexcept
{
    SOCKADDR_INET tempAddr{};
    CopyMemory(&tempAddr, &inAddr.m_sockaddr, c_sockaddrSize);
    CopyMemory(&inAddr.m_sockaddr, &m_sockaddr, c_sockaddrSize);
    CopyMemory(&m_sockaddr, &tempAddr, c_sockaddrSize);
}

inline bool ctSockaddr::setAddress(SOCKET s) noexcept
{
    auto namelen = length();
    return 0 == getsockname(s, sockaddr(), &namelen);
}

inline void ctSockaddr::setSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept
{
    const auto length = static_cast<size_t>(inLength) < c_sockaddrSize ? inLength : c_sockaddrSize;
    ZeroMemory(&m_sockaddr, c_sockaddrSize);
    CopyMemory(&m_sockaddr, inAddr, length);
}

inline void ctSockaddr::setSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, size_t inLength) noexcept
{
    const auto length = inLength < c_sockaddrSize ? inLength : c_sockaddrSize;
    ZeroMemory(&m_sockaddr, c_sockaddrSize);
    CopyMemory(&m_sockaddr, inAddr, length);
}

inline void ctSockaddr::setSockaddr(const SOCKADDR_IN* inAddr) noexcept
{
    ZeroMemory(&m_sockaddr, c_sockaddrSize);
    CopyMemory(&m_sockaddr, inAddr, sizeof(SOCKADDR_IN));
}

inline void ctSockaddr::setSockaddr(const SOCKADDR_IN6* inAddr) noexcept
{
    ZeroMemory(&m_sockaddr, c_sockaddrSize);
    CopyMemory(&m_sockaddr, inAddr, sizeof(SOCKADDR_IN6));
}

inline void ctSockaddr::setSockaddr(const SOCKADDR_INET* inAddr) noexcept
{
    ZeroMemory(&m_sockaddr, c_sockaddrSize);
    CopyMemory(&m_sockaddr, inAddr, sizeof(SOCKADDR_INET));
}

inline void ctSockaddr::setSockaddr(const SOCKET_ADDRESS* inAddr) noexcept
{
    const auto length = static_cast<size_t>(inAddr->iSockaddrLength) < c_sockaddrSize
                        ? static_cast<size_t>(inAddr->iSockaddrLength) : c_sockaddrSize;

    ZeroMemory(&m_sockaddr, c_sockaddrSize);
    CopyMemory(&m_sockaddr, inAddr->lpSockaddr, length);
}

inline bool ctSockaddr::isAddressLinkLocal() const noexcept
{
    if (m_sockaddr.si_family == 0)
    {
        WI_ASSERT(false);
        return false;
    }

    if (m_sockaddr.si_family == AF_INET6)
    {
        return IN6_IS_ADDR_LINKLOCAL(&m_sockaddr.Ipv6.sin6_addr);
    }

    return (m_sockaddr.Ipv4.sin_addr.S_un.S_addr & 0xffff) == 0xfea9; // 169.254/16
}

inline void ctSockaddr::setAddressAny() noexcept
{
    reset(m_sockaddr.si_family);
}

inline bool ctSockaddr::isAddressAny() const noexcept
{
    if (m_sockaddr.si_family == 0)
    {
        WI_ASSERT(false);
        return false;
    }

    if (m_sockaddr.si_family == AF_INET6)
    {
        return 0 == memcmp(&m_sockaddr.Ipv6.sin6_addr, &in6addr_any, sizeof IN6_ADDR);
    }
    return 0 == memcmp(&m_sockaddr.Ipv4.sin_addr, &in4addr_any, sizeof IN_ADDR);
}

inline void ctSockaddr::setAddressLoopback() noexcept
{
    const ctSockaddr loopbackAddr{m_sockaddr.si_family, AddressType::Loopback};
    CopyMemory(&m_sockaddr, &loopbackAddr.m_sockaddr, c_sockaddrSize);
}

inline bool ctSockaddr::isAddressLoopback() const noexcept
{
    if (m_sockaddr.si_family == 0)
    {
        WI_ASSERT(false);
        return false;
    }

    if (m_sockaddr.si_family == AF_INET6)
    {
        return 0 == memcmp(&m_sockaddr.Ipv6.sin6_addr, &in6addr_loopback, sizeof IN6_ADDR);
    }
    return 0 == memcmp(&m_sockaddr.Ipv4.sin_addr, &in4addr_loopback, sizeof IN_ADDR);
}

inline bool ctSockaddr::setAddress(_In_ PCWSTR wszAddr) noexcept
{
    ADDRINFOW hints;
    ZeroMemory(&hints, sizeof hints);
    hints.ai_flags = AI_NUMERICHOST;

    ADDRINFOW* pResult = nullptr;
    if (0 == GetAddrInfoW(wszAddr, nullptr, &hints, &pResult))
    {
        setSockaddr(pResult->ai_addr, pResult->ai_addrlen);
        FreeAddrInfoW(pResult);
        return true;
    }
    return false;
}

#ifdef _WINSOCK_DEPRECATED_NO_WARNINGS
inline bool ctSockaddr::setAddress(_In_ PCSTR szAddr) noexcept
{
    ADDRINFOA hints;
    ZeroMemory(&hints, sizeof hints);
    hints.ai_flags = AI_NUMERICHOST;

    ADDRINFOA* pResult = nullptr;
    if (0 == GetAddrInfoA(szAddr, nullptr, &hints, &pResult))
    {
        setSockaddr(pResult->ai_addr, pResult->ai_addrlen);
        FreeAddrInfoA(pResult);
        return true;
    }
    return false;
}
#endif

inline void ctSockaddr::setAddress(const IN_ADDR* inAddr) noexcept
{
    reset(AF_INET);
    m_sockaddr.Ipv4.sin_addr.S_un.S_addr = inAddr->S_un.S_addr;
}

inline void ctSockaddr::setAddress(const IN6_ADDR* inAddr) noexcept
{
    reset(AF_INET6);
    m_sockaddr.Ipv6.sin6_addr = *inAddr;
}

inline void ctSockaddr::setPort(uint16_t port, ByteOrder byteOrder) noexcept
{
    m_sockaddr.Ipv4.sin_port = byteOrder == ByteOrder::HostOrder ? htons(port) : port;
}

inline void ctSockaddr::setScopeId(uint32_t scopeid) noexcept
{
    if (AF_INET6 == m_sockaddr.si_family)
    {
        m_sockaddr.Ipv6.sin6_scope_id = scopeid;
    }
}

inline void ctSockaddr::setFlowInfo(uint32_t flowinfo) noexcept
{
    if (AF_INET6 == m_sockaddr.si_family)
    {
        m_sockaddr.Ipv6.sin6_flowinfo = flowinfo;
    }
}

inline std::wstring ctSockaddr::writeAddress() const
{
    WCHAR returnString[FixedStringLength]{};
    writeAddress(returnString);
    returnString[FixedStringLength - 1] = L'\0';
    return returnString;
}

inline bool ctSockaddr::writeAddress(WCHAR (&address)[FixedStringLength]) const noexcept
{
    ZeroMemory(address, FixedStringLength * sizeof(WCHAR));

    const void* const pAddr = AF_INET == m_sockaddr.si_family
                              ? static_cast<const void*>(&m_sockaddr.Ipv4.sin_addr) : static_cast<const void*>(&m_sockaddr.Ipv6.sin6_addr);
    return nullptr != InetNtopW(m_sockaddr.si_family, pAddr, address, FixedStringLength);
}

inline bool ctSockaddr::writeAddress(CHAR (&address)[FixedStringLength]) const noexcept
{
    ZeroMemory(address, FixedStringLength * sizeof(CHAR));

    const void* const pAddr = AF_INET == m_sockaddr.si_family
                              ? static_cast<const void*>(&m_sockaddr.Ipv4.sin_addr) : static_cast<const void*>(&m_sockaddr.Ipv6.sin6_addr);
    return nullptr != InetNtopA(m_sockaddr.si_family, pAddr, address, FixedStringLength);
}

inline std::wstring ctSockaddr::writeCompleteAddress(bool trimScope) const
{
    WCHAR returnString[FixedStringLength]{};
    writeCompleteAddress(returnString, trimScope);
    returnString[FixedStringLength - 1] = L'\0';
    return returnString;
}

inline bool ctSockaddr::writeCompleteAddress(WCHAR (&address)[FixedStringLength], bool trimScope) const noexcept
{
    ZeroMemory(address, FixedStringLength * sizeof(WCHAR));

    DWORD addressLength = FixedStringLength;
    if (0 == WSAAddressToStringW(const_cast<SOCKADDR*>(sockaddr()), c_sockaddrSize, nullptr, address, &addressLength))
    {
        if (m_sockaddr.si_family == AF_INET6 && trimScope)
        {
            // ReSharper disable once CppLocalVariableMayBeConst
            auto* const end = address + addressLength;
            if (auto* pScopePtr = std::find(address, end, L'%'); pScopePtr != end)
            {
                if (const auto* pMovePtr = std::find(address, end, L']'); pMovePtr != end)
                {
                    while (pMovePtr != end)
                    {
                        *pScopePtr = *pMovePtr;
                        ++pScopePtr;
                        ++pMovePtr;
                    }
                }
                else
                {
                    // no port was appended
                    while (pScopePtr != end)
                    {
                        *pScopePtr = L'\0';
                        ++pScopePtr;
                    }
                }
            }
        }
        return true;
    }
    return false;
}

#ifdef _WINSOCK_DEPRECATED_NO_WARNINGS
inline bool ctSockaddr::writeCompleteAddress(CHAR (&address)[FixedStringLength], bool trimScope) const noexcept
{
    ZeroMemory(address, FixedStringLength * sizeof(CHAR));

    DWORD addressLength = FixedStringLength;
    if (0 == WSAAddressToStringA(const_cast<SOCKADDR*>(sockaddr()), c_sockaddrSize, nullptr, address, &addressLength))
    {
        if (m_sockaddr.si_family == AF_INET6 && trimScope)
        {
            // ReSharper disable once CppLocalVariableMayBeConst
            auto* const end = address + addressLength;
            if (auto* pScopePtr = std::find(address, end, '%'); pScopePtr != end)
            {
                if (const auto* pMovePtr = std::find(address, end, ']'); pMovePtr != end)
                {
                    while (pMovePtr != end)
                    {
                        *pScopePtr = *pMovePtr;
                        ++pScopePtr;
                        ++pMovePtr;
                    }
                }
                else
                {
                    // no port was appended
                    while (pScopePtr != end)
                    {
                        *pScopePtr = '\0';
                        ++pScopePtr;
                    }
                }
            }
        }
        return true;
    }
    return false;
}
#endif

// ReSharper disable once CppMemberFunctionMayBeStatic
inline int ctSockaddr::length() const noexcept // NOLINT(readability-convert-member-functions-to-static)
{
    return c_sockaddrSize;
}

inline ADDRESS_FAMILY ctSockaddr::family() const noexcept
{
    return m_sockaddr.si_family;
}

inline uint16_t ctSockaddr::port() const noexcept
{
    return ntohs(m_sockaddr.Ipv4.sin_port);
}

inline uint32_t ctSockaddr::flowinfo() const noexcept
{
    if (AF_INET6 == m_sockaddr.si_family)
    {
        return m_sockaddr.Ipv6.sin6_flowinfo;
    }
    return 0;
}

inline uint32_t ctSockaddr::scope_id() const noexcept
{
    if (AF_INET6 == m_sockaddr.si_family)
    {
        return m_sockaddr.Ipv6.sin6_scope_id;
    }
    return 0;
}

inline SOCKADDR* ctSockaddr::sockaddr() noexcept
{
    return reinterpret_cast<SOCKADDR*>(&m_sockaddr);
}

inline SOCKADDR_IN* ctSockaddr::sockaddr_in() noexcept
{
    return &m_sockaddr.Ipv4;
}

inline SOCKADDR_IN6* ctSockaddr::sockaddr_in6() noexcept
{
    return &m_sockaddr.Ipv6;
}

inline SOCKADDR_INET* ctSockaddr::sockaddr_inet() noexcept
{
    return &m_sockaddr;
}

inline IN_ADDR* ctSockaddr::in_addr() noexcept
{
    return &m_sockaddr.Ipv4.sin_addr;
}

inline IN6_ADDR* ctSockaddr::in6_addr() noexcept
{
    return &m_sockaddr.Ipv6.sin6_addr;
}

inline const SOCKADDR* ctSockaddr::sockaddr() const noexcept
{
    return reinterpret_cast<const SOCKADDR*>(&m_sockaddr);
}

inline const SOCKADDR_IN* ctSockaddr::sockaddr_in() const noexcept
{
    return &m_sockaddr.Ipv4;
}

inline const SOCKADDR_IN6* ctSockaddr::sockaddr_in6() const noexcept
{
    return &m_sockaddr.Ipv6;
}

inline const SOCKADDR_INET* ctSockaddr::sockaddr_inet() const noexcept
{
    return &m_sockaddr;
}

inline const IN_ADDR* ctSockaddr::in_addr() const noexcept
{
    return &m_sockaddr.Ipv4.sin_addr;
}

inline const IN6_ADDR* ctSockaddr::in6_addr() const noexcept
{
    return &m_sockaddr.Ipv6.sin6_addr;
}
} // namespace ctl

#pragma prefast(pop)
