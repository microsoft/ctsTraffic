/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <algorithm>
#include <string>
#include <vector>
// os headers
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
// wil headers
#include <wil/stl.h>
#include <wil/resource.h>

#pragma prefast(push)
// ignore prefast IPv4 warnings
#pragma prefast(disable: 24002)
// ignore IDN warnings when explicitly asking to resolve a short-string
#pragma prefast(disable: 38026)


// ReSharper disable once CppInconsistentNaming
namespace ctl
{
enum class ByteOrder
{
    HostOrder,
    NetworkOrder
};

constexpr DWORD SockAddrMaxStringLength = INET6_ADDRSTRLEN;

class ctSockaddr final
{
public:
    static std::vector<ctSockaddr> ResolveName(PCWSTR name)
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
                returnAddrs.emplace_back(pAddrInfo->ai_addr, static_cast<int>(pAddrInfo->ai_addrlen));
            }
        }
        else
        {
            THROW_WIN32_MSG(::WSAGetLastError(), "GetAddrInfoW");
        }

        return returnAddrs;
    }

    // for dual-mode sockets, when needing to explicitly connect to the target v4 address,
    // - one must map the v4 address to its mapped v6 address
    static ctSockaddr MapDualMode4To6(const ctSockaddr& inV4) noexcept
    {
        constexpr IN6_ADDR v4MappedPrefix{{IN6ADDR_V4MAPPEDPREFIX_INIT}};

        ctSockaddr outV6(AF_INET6);
        auto* const pIn6Addr = outV6.in6_addr();
        const auto* const pIn4Addr = inV4.in_addr();

        *pIn6Addr = v4MappedPrefix;
        pIn6Addr->u.Byte[12] = pIn4Addr->S_un.S_un_b.s_b1;
        pIn6Addr->u.Byte[13] = pIn4Addr->S_un.S_un_b.s_b2;
        pIn6Addr->u.Byte[14] = pIn4Addr->S_un.S_un_b.s_b3;
        pIn6Addr->u.Byte[15] = pIn4Addr->S_un.S_un_b.s_b4;

        outV6.SetPort(inV4.port());
        return outV6;
    }

    enum class AddressType
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

    ~ctSockaddr() = default;

    ctSockaddr(const ctSockaddr&) noexcept;
    ctSockaddr& operator=(const ctSockaddr&) noexcept;

    ctSockaddr(ctSockaddr&&) noexcept;
    ctSockaddr& operator=(ctSockaddr&&) noexcept;

    bool operator==(const ctSockaddr&) const noexcept;
    bool operator!=(const ctSockaddr&) const noexcept;
    bool operator<(const ctSockaddr&) const noexcept;

    void swap(_Inout_ ctSockaddr&) noexcept;

    void set(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept;
    void set(const SOCKADDR_IN*) noexcept;
    void set(const SOCKADDR_IN6*) noexcept;
    void set(const SOCKADDR_INET*) noexcept;
    void set(const SOCKET_ADDRESS*) noexcept;
    void set(ADDRESS_FAMILY, AddressType) noexcept;

    // setting by string returns a bool if was able to convert to an address
    bool SetAddress(_In_ PCWSTR) noexcept;
    bool SetAddress(_In_ PCSTR) noexcept;
    void SetAddress(const IN_ADDR*) noexcept;
    void SetAddress(const IN6_ADDR*) noexcept;
    [[nodiscard]] bool SetAddress(SOCKET) const noexcept;

    void SetPort(unsigned short, ByteOrder = ByteOrder::HostOrder) noexcept;
    void SetScopeId(unsigned long) noexcept;
    void SetFlowInfo(unsigned long) noexcept;

    [[nodiscard]] bool IsAddressAny() const noexcept;
    [[nodiscard]] bool IsAddressLoopback() const noexcept;

    // the odd syntax below is necessary to pass a reference to an array
    // necessary as the [] operators take precedence over the ref & operator
    // writeAddress only prints the IP address, not the scope or port
    [[nodiscard]] std::wstring WriteAddress() const;
    bool WriteAddress(WCHAR (&address)[SockAddrMaxStringLength]) const noexcept; // NOLINT(google-runtime-references)
    bool WriteAddress(CHAR (&address)[SockAddrMaxStringLength]) const noexcept; // NOLINT(google-runtime-references)

    // writeCompleteAddress prints the IP address, scope, and port
    [[nodiscard]] std::wstring WriteCompleteAddress(bool trimScope = false) const;
    bool WriteCompleteAddress(WCHAR (&address)[SockAddrMaxStringLength], bool trimScope = false) const noexcept; // NOLINT(google-runtime-references)
    bool WriteCompleteAddress(CHAR (&address)[SockAddrMaxStringLength], bool trimScope = false) const noexcept; // NOLINT(google-runtime-references)

    //
    // Accessors
    //
    [[nodiscard]] static int length() noexcept;
    [[nodiscard]] unsigned short port() const noexcept;
    [[nodiscard]] ADDRESS_FAMILY family() const noexcept;
    [[nodiscard]] unsigned long flowinfo() const noexcept;
    [[nodiscard]] unsigned long scope_id() const noexcept;
    // returning non-const from const methods, for API compatibility
    [[nodiscard]] SOCKADDR* sockaddr() const noexcept;
    [[nodiscard]] SOCKADDR_IN* sockaddr_in() const noexcept;
    [[nodiscard]] SOCKADDR_IN6* sockaddr_in6() const noexcept;
    [[nodiscard]] SOCKADDR_INET* sockaddr_inet() const noexcept;
    [[nodiscard]] IN_ADDR* in_addr() const noexcept;
    [[nodiscard]] IN6_ADDR* in6_addr() const noexcept;

private:
    static constexpr size_t c_saddrSize = sizeof(SOCKADDR_INET);
    SOCKADDR_INET m_saddr{};
};

//
// non-member swap
//
// ReSharper disable once CppInconsistentNaming
inline void swap(_Inout_ ctSockaddr& left, _Inout_ ctSockaddr& right) noexcept
{
    left.swap(right);
}


inline ctSockaddr::ctSockaddr(ADDRESS_FAMILY family, AddressType type) noexcept
{
    ::ZeroMemory(&m_saddr, c_saddrSize);
    m_saddr.si_family = family;

    if (type == AddressType::Loopback)
    {
        if (AF_INET == family)
        {
            auto* const pSockaddrIn = reinterpret_cast<PSOCKADDR_IN>(&m_saddr);
            const auto ipv4Port = pSockaddrIn->sin_port;
            ::ZeroMemory(&m_saddr, c_saddrSize);
            pSockaddrIn->sin_family = AF_INET;
            pSockaddrIn->sin_port = ipv4Port;
            pSockaddrIn->sin_addr.s_addr = 0x0100007f; // htons(INADDR_LOOPBACK);
        }
        else if (AF_INET6 == family)
        {
            auto* const pSockaddrIn6 = reinterpret_cast<PSOCKADDR_IN6>(&m_saddr);
            const auto ipv6Port = pSockaddrIn6->sin6_port;
            ::ZeroMemory(&m_saddr, c_saddrSize);
            pSockaddrIn6->sin6_family = AF_INET6;
            pSockaddrIn6->sin6_port = ipv6Port;
            pSockaddrIn6->sin6_addr.s6_bytes[15] = 1; // IN6ADDR_LOOPBACK_INIT;
        }
        else
        {
            FAIL_FAST_MSG("ctSockaddr: unknown family creating a loopback sockaddr");
        }
    }
}

inline ctSockaddr::ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept
{
    const auto length = static_cast<size_t>(inLength) <= c_saddrSize ? inLength : c_saddrSize;

    ::ZeroMemory(&m_saddr, c_saddrSize);
    ::CopyMemory(&m_saddr, inAddr, length);
}

inline ctSockaddr::ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, size_t inLength) noexcept
{
    const auto length = inLength <= c_saddrSize ? inLength : c_saddrSize;

    ::ZeroMemory(&m_saddr, c_saddrSize);
    ::CopyMemory(&m_saddr, inAddr, length);
}

inline ctSockaddr::ctSockaddr(const SOCKADDR_IN* inAddr) noexcept
{
    ::ZeroMemory(&m_saddr, c_saddrSize);
    ::CopyMemory(&m_saddr, inAddr, sizeof(SOCKADDR_IN));
}

inline ctSockaddr::ctSockaddr(const SOCKADDR_IN6* inAddr) noexcept
{
    ::ZeroMemory(&m_saddr, c_saddrSize);
    ::CopyMemory(&m_saddr, inAddr, sizeof(SOCKADDR_IN6));
}

inline ctSockaddr::ctSockaddr(const SOCKADDR_INET* inAddr) noexcept
{
    ::ZeroMemory(&m_saddr, c_saddrSize);
    if (AF_INET == inAddr->si_family)
    {
        ::CopyMemory(&m_saddr, inAddr, sizeof(SOCKADDR_IN));
    }
    else
    {
        ::CopyMemory(&m_saddr, inAddr, sizeof(SOCKADDR_IN6));
    }
}

inline ctSockaddr::ctSockaddr(const SOCKET_ADDRESS* inAddr) noexcept
{
    const auto length = static_cast<size_t>(inAddr->iSockaddrLength) <= c_saddrSize
                        ? inAddr->iSockaddrLength
                        : c_saddrSize;

    ::ZeroMemory(&m_saddr, c_saddrSize);
    ::CopyMemory(&m_saddr, inAddr->lpSockaddr, length);
}

inline ctSockaddr::ctSockaddr(const ctSockaddr& inAddr) noexcept
{
    ::CopyMemory(&m_saddr, &inAddr.m_saddr, c_saddrSize);
}

inline ctSockaddr& ctSockaddr::operator=(const ctSockaddr& inAddr) noexcept
{
    ctSockaddr temp(inAddr);
    swap(temp);
    return *this;
}

inline ctSockaddr::ctSockaddr(ctSockaddr&& inAddr) noexcept
{
    ::CopyMemory(&m_saddr, &inAddr.m_saddr, c_saddrSize);
}

inline ctSockaddr& ctSockaddr::operator=(ctSockaddr&& inAddr) noexcept
{
    ::CopyMemory(&m_saddr, &inAddr.m_saddr, c_saddrSize);
    ::ZeroMemory(&inAddr.m_saddr, c_saddrSize);
    return *this;
}

inline bool ctSockaddr::operator==(const ctSockaddr& inAddr) const noexcept
{
    return 0 == memcmp(&m_saddr, &inAddr.m_saddr, c_saddrSize);
}

inline bool ctSockaddr::operator!=(const ctSockaddr& inAddr) const noexcept
{
    return !(*this == inAddr);
}

inline bool ctSockaddr::operator<(const ctSockaddr& rhs) const noexcept
{
    // Follows the same documented comparison logic as 
    //   GetTcpTable2 and GetTcp6Table2
    const auto& lhs = *this;
    if (lhs.family() != rhs.family())
    {
        return false;
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
    }
    else
    {
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
    }
    // else they are all equal
    return false;
}

inline void ctSockaddr::swap(_Inout_ ctSockaddr& inAddr) noexcept
{
    using std::swap;
    swap(m_saddr, inAddr.m_saddr);
}

inline bool ctSockaddr::SetAddress(SOCKET s) const noexcept
{
    auto namelen = length();
    return 0 == getsockname(s, sockaddr(), &namelen);
}

inline void ctSockaddr::set(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept
{
    const auto length = static_cast<size_t>(inLength) <= c_saddrSize ? inLength : c_saddrSize;

    ::ZeroMemory(&m_saddr, c_saddrSize);
    ::CopyMemory(&m_saddr, inAddr, length);
}

inline void ctSockaddr::set(const SOCKADDR_IN* inAddr) noexcept
{
    ::ZeroMemory(&m_saddr, c_saddrSize);
    ::CopyMemory(&m_saddr, inAddr, sizeof(SOCKADDR_IN));
}

inline void ctSockaddr::set(const SOCKADDR_IN6* inAddr) noexcept
{
    ::ZeroMemory(&m_saddr, c_saddrSize);
    ::CopyMemory(&m_saddr, inAddr, sizeof(SOCKADDR_IN6));
}

inline void ctSockaddr::set(const SOCKADDR_INET* inAddr) noexcept
{
    ::ZeroMemory(&m_saddr, c_saddrSize);
    if (AF_INET == inAddr->si_family)
    {
        ::CopyMemory(&m_saddr, inAddr, sizeof(SOCKADDR_IN));
    }
    else
    {
        ::CopyMemory(&m_saddr, inAddr, sizeof(SOCKADDR_IN6));
    }
}

inline void ctSockaddr::set(const SOCKET_ADDRESS* inAddr) noexcept
{
    const auto length = static_cast<size_t>(inAddr->iSockaddrLength) <= c_saddrSize
                        ? static_cast<size_t>(inAddr->iSockaddrLength)
                        : c_saddrSize;

    ::ZeroMemory(&m_saddr, c_saddrSize);
    ::CopyMemory(&m_saddr, inAddr->lpSockaddr, length);
}

inline void ctSockaddr::set(ADDRESS_FAMILY family, AddressType type) noexcept
{
    ctSockaddr temp(family, type);
    swap(temp);
}

inline bool ctSockaddr::IsAddressAny() const noexcept
{
    const ctSockaddr anyAddr(m_saddr.si_family, AddressType::Any);
    return 0 == memcmp(&anyAddr.m_saddr, &m_saddr, c_saddrSize);
}

inline bool ctSockaddr::IsAddressLoopback() const noexcept
{
    const ctSockaddr anyAddr(m_saddr.si_family, AddressType::Loopback);
    return 0 == memcmp(&anyAddr.m_saddr, &m_saddr, c_saddrSize);
}

inline bool ctSockaddr::SetAddress(_In_ PCWSTR wszAddr) noexcept
{
    ADDRINFOW hints;
    ::ZeroMemory(&hints, sizeof hints);
    hints.ai_flags = AI_NUMERICHOST;

    ADDRINFOW* pResult = nullptr;
    if (0 == GetAddrInfoW(wszAddr, nullptr, &hints, &pResult))
    {
        set(pResult->ai_addr, static_cast<int>(pResult->ai_addrlen));
        FreeAddrInfoW(pResult);
        return true;
    }
    return false;
}

inline bool ctSockaddr::SetAddress(_In_ PCSTR szAddr) noexcept
{
    ADDRINFOA hints;
    ::ZeroMemory(&hints, sizeof hints);
    hints.ai_flags = AI_NUMERICHOST;

    ADDRINFOA* pResult = nullptr;
#pragma prefast(suppress:38026, "The explicit use of AI_NUMERICHOST makes GetAddrInfoA's lack of IDN support irrelevant here - they wouldn't be accepted even if we used GetAddrInfoW")
    if (0 == ::GetAddrInfoA(szAddr, nullptr, &hints, &pResult))
    {
        set(pResult->ai_addr, static_cast<int>(pResult->ai_addrlen));
        ::FreeAddrInfoA(pResult);
        return true;
    }
    return false;
}

inline void ctSockaddr::SetAddress(const IN_ADDR* inAddr) noexcept
{
    m_saddr.si_family = AF_INET;
    auto* const pSockaddrIn = reinterpret_cast<PSOCKADDR_IN>(&m_saddr);
    pSockaddrIn->sin_addr.S_un.S_addr = inAddr->S_un.S_addr;
}

inline void ctSockaddr::SetAddress(const IN6_ADDR* inAddr) noexcept
{
    m_saddr.si_family = AF_INET6;
    auto* const pSockaddrIn6 = reinterpret_cast<PSOCKADDR_IN6>(&m_saddr);
    pSockaddrIn6->sin6_addr = *inAddr;
}

inline void ctSockaddr::SetPort(unsigned short port, ByteOrder byteOrder) noexcept
{
    auto* const pSockaddrIn = reinterpret_cast<PSOCKADDR_IN>(&m_saddr);
    pSockaddrIn->sin_port = byteOrder == ByteOrder::HostOrder ? htons(port) : port;
}

inline void ctSockaddr::SetScopeId(unsigned long scopeid) noexcept
{
    if (AF_INET6 == m_saddr.si_family)
    {
        auto* const pSockaddrIn6 = reinterpret_cast<PSOCKADDR_IN6>(&m_saddr);
        pSockaddrIn6->sin6_scope_id = scopeid;
    }
}

inline void ctSockaddr::SetFlowInfo(unsigned long flowinfo) noexcept
{
    if (AF_INET6 == m_saddr.si_family)
    {
        auto* const pSockaddrIn6 = reinterpret_cast<PSOCKADDR_IN6>(&m_saddr);
        pSockaddrIn6->sin6_flowinfo = flowinfo;
    }
}

inline std::wstring ctSockaddr::WriteAddress() const
{
    WCHAR returnString[SockAddrMaxStringLength]{};
    WriteAddress(returnString);
    returnString[SockAddrMaxStringLength - 1] = L'\0';
    return returnString;
}

inline bool ctSockaddr::WriteAddress(WCHAR (&address)[SockAddrMaxStringLength]) const noexcept
{
    ::ZeroMemory(address, SockAddrMaxStringLength * sizeof(WCHAR));

    const auto* const pAddr = AF_INET == m_saddr.si_family
                              ? reinterpret_cast<PVOID>(in_addr())
                              : reinterpret_cast<PVOID>(in6_addr());
    return nullptr != InetNtopW(m_saddr.si_family, pAddr, address, SockAddrMaxStringLength);
}

inline bool ctSockaddr::WriteAddress(CHAR (&address)[SockAddrMaxStringLength]) const noexcept
{
    ::ZeroMemory(address, SockAddrMaxStringLength * sizeof(CHAR));

    const auto* const pAddr = AF_INET == m_saddr.si_family
                              ? reinterpret_cast<PVOID>(in_addr())
                              : reinterpret_cast<PVOID>(in6_addr());
    return nullptr != ::InetNtopA(m_saddr.si_family, pAddr, address, SockAddrMaxStringLength);
}

inline std::wstring ctSockaddr::WriteCompleteAddress(bool trimScope) const
{
    WCHAR returnString[SockAddrMaxStringLength]{};
    WriteCompleteAddress(returnString, trimScope);
    returnString[SockAddrMaxStringLength - 1] = L'\0';
    return returnString;
}

inline bool ctSockaddr::WriteCompleteAddress(WCHAR (&address)[SockAddrMaxStringLength], bool trimScope) const noexcept
{
    ::ZeroMemory(address, SockAddrMaxStringLength * sizeof(WCHAR));

    DWORD addressLength = SockAddrMaxStringLength;
    if (0 == WSAAddressToStringW(sockaddr(), c_saddrSize, nullptr, address, &addressLength))
    {
        if (family() == AF_INET6 && trimScope)
        {
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
inline bool ctSockaddr::WriteCompleteAddress(CHAR (&address)[SockAddrMaxStringLength], bool trimScope) const noexcept
{
    ::ZeroMemory(address, SockAddrMaxStringLength * sizeof(CHAR));

    DWORD addressLength = SockAddrMaxStringLength;
    if (0 == WSAAddressToStringA(sockaddr(), c_saddrSize, nullptr, address, &addressLength))
    {
        if (family() == AF_INET6 && trimScope)
        {
            auto* const end = address + addressLength;
            if (auto* pScopePtr = std::find(address, end, '%'); pScopePtr != end)
            {
                if (auto* pMovePtr = std::find(address, end, ']'); pMovePtr != end)
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
inline int ctSockaddr::length() noexcept
{
    return static_cast<int>(c_saddrSize);
}

inline ADDRESS_FAMILY ctSockaddr::family() const noexcept
{
    return m_saddr.si_family;
}

inline unsigned short ctSockaddr::port() const noexcept
{
    const auto* const pSockaddrIn = reinterpret_cast<const SOCKADDR_IN*>(&m_saddr);
    return ntohs(pSockaddrIn->sin_port);
}

inline unsigned long ctSockaddr::flowinfo() const noexcept
{
    if (AF_INET6 == m_saddr.si_family)
    {
        const auto* const pSockaddrIn6 = reinterpret_cast<const SOCKADDR_IN6*>(&m_saddr);
        return pSockaddrIn6->sin6_flowinfo;
    }
    return 0;
}

inline unsigned long ctSockaddr::scope_id() const noexcept
{
    if (AF_INET6 == m_saddr.si_family)
    {
        const auto* const pSockaddrIn6 = reinterpret_cast<const SOCKADDR_IN6*>(&m_saddr);
        return pSockaddrIn6->sin6_scope_id;
    }
    return 0;
}

inline SOCKADDR* ctSockaddr::sockaddr() const noexcept
{
    return const_cast<SOCKADDR*>(
        reinterpret_cast<const SOCKADDR*>(&m_saddr));
}

inline SOCKADDR_IN* ctSockaddr::sockaddr_in() const noexcept
{
    return const_cast<SOCKADDR_IN*>(
        reinterpret_cast<const SOCKADDR_IN*>(&m_saddr));
}

inline SOCKADDR_IN6* ctSockaddr::sockaddr_in6() const noexcept
{
    return const_cast<SOCKADDR_IN6*>(
        reinterpret_cast<const SOCKADDR_IN6*>(&m_saddr));
}

inline SOCKADDR_INET* ctSockaddr::sockaddr_inet() const noexcept
{
    return const_cast<SOCKADDR_INET*>(&m_saddr);
}

inline IN_ADDR* ctSockaddr::in_addr() const noexcept
{
    const auto* const pSockaddrIn = reinterpret_cast<const SOCKADDR_IN*>(&m_saddr);
    return const_cast<IN_ADDR*>(&pSockaddrIn->sin_addr);
}

inline IN6_ADDR* ctSockaddr::in6_addr() const noexcept
{
    const auto* const pSockaddrIn6 = reinterpret_cast<const SOCKADDR_IN6*>(&m_saddr);
    return const_cast<IN6_ADDR*>(&pSockaddrIn6->sin6_addr);
}
} // namespace ctl

#pragma prefast(pop)
