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
#include <Winsock2.h>
#include <Ws2tcpip.h>
// wil headers
#include <wil/resource.h>
// ctl headers
#include "ctException.hpp"

#pragma prefast(push)
// ignore prefast IPv4 warnings
#pragma prefast(disable: 24002)
// ignore IDN warnings when explicitly asking to resolve a short-string
#pragma prefast(disable: 38026)

namespace ctl
{
	enum class ByteOrder
	{
		HostOrder,
		NetworkOrder
	};

	const DWORD IP_STRING_MAX_LENGTH = 65;

	class ctSockaddr final
    {
	public:

		static
		std::vector<ctSockaddr> ResolveName(LPCWSTR _name)
		{
			ADDRINFOW* addr_result = nullptr;
			auto freeAddrOnExit = wil::scope_exit([&]() { if (addr_result) ::FreeAddrInfoW(addr_result); });

			std::vector<ctSockaddr> return_addrs;
			if (0 == ::GetAddrInfoW(_name, nullptr, nullptr, &addr_result)) {
				for (auto addrinfo = addr_result; addrinfo != nullptr; addrinfo = addrinfo->ai_next) {
					return_addrs.emplace_back(addrinfo->ai_addr, static_cast<int>(addrinfo->ai_addrlen));
				}
			} else {
				throw ctException(::WSAGetLastError(), L"GetAddrInfoW", L"ctl::ctSockaddr::ResolveName", false);
			}

			return return_addrs;
		}

		explicit ctSockaddr(short family = AF_UNSPEC) noexcept;
		explicit ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept;
		explicit ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, size_t inLength) noexcept;
		explicit ctSockaddr(const SOCKADDR_IN*) noexcept;
		explicit ctSockaddr(const SOCKADDR_IN6*) noexcept;
		explicit ctSockaddr(const SOCKADDR_INET*) noexcept;
		explicit ctSockaddr(const SOCKADDR_STORAGE*) noexcept;
		explicit ctSockaddr(const SOCKET_ADDRESS*) noexcept;

		ctSockaddr(const ctSockaddr&) noexcept;
		ctSockaddr& operator=(const ctSockaddr&) noexcept;

		ctSockaddr(ctSockaddr&&) noexcept;
		ctSockaddr& operator=(ctSockaddr&&) noexcept;

		bool operator==(const ctSockaddr&) const noexcept;
		bool operator!=(const ctSockaddr&) const noexcept;
		bool operator<(const ctSockaddr&) const noexcept;

        ~ctSockaddr() = default;

		void reset(short family = AF_UNSPEC) noexcept;

		void swap(_Inout_ ctSockaddr&) noexcept;

		bool setSocketAddress(SOCKET) const noexcept;
	
		void setSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept;
		void setSockaddr(const SOCKADDR_IN*) noexcept;
		void setSockaddr(const SOCKADDR_IN6*) noexcept;
		void setSockaddr(const SOCKADDR_INET*) noexcept;
		void setSockaddr(const SOCKADDR_STORAGE*) noexcept;
		void setSockaddr(const SOCKET_ADDRESS*) noexcept;

		void setPort(unsigned short, ByteOrder = ByteOrder::HostOrder) noexcept;

		// for dual-mode sockets, when needing to explicitly connect to the target v4 address,
		// - one must map the v4 address to its mapped v6 address
		void mapDualMode4To6() noexcept;

		// setting by string returns a bool if was able to convert to an address
		bool setAddress(_In_ PCWSTR) noexcept;
		bool setAddress(_In_ LPCSTR) noexcept;

		void setAddress(const IN_ADDR*) noexcept;
		void setAddress(const IN6_ADDR*) noexcept;

		void setFlowInfo(unsigned long) noexcept;
		void setScopeId(unsigned long) noexcept;

		void setAddressLoopback() noexcept;
		void setAddressAny() noexcept;

		bool isAddressLoopback() const noexcept;
		bool isAddressAny() const noexcept;

		// the odd syntax below is necessary to pass a reference to an array
		// necessary as the [] operators take precedence over the ref & operator
		// writeAddress only prints the IP address, not the scope or port
		std::wstring writeAddress() const;
		bool writeAddress(WCHAR (&address)[IP_STRING_MAX_LENGTH]) const noexcept;
		bool writeAddress(CHAR (&address)[IP_STRING_MAX_LENGTH]) const noexcept;
		// writeCompleteAddress prints the IP address, scope, and port
		std::wstring writeCompleteAddress(bool trim_scope = false) const;
		bool writeCompleteAddress(WCHAR (&address)[IP_STRING_MAX_LENGTH], bool trim_scope = false) const noexcept;
		bool writeCompleteAddress(CHAR (&address)[IP_STRING_MAX_LENGTH], bool trim_scope = false) const noexcept;

		//
		// Accessors
		//
		int               length() const noexcept;
		unsigned short    port() const noexcept;
		short             family() const noexcept;
		unsigned long     flowinfo() const noexcept;
		unsigned long     scopeId() const noexcept;
		// returning non-const from const methods, for API compatibility
		SOCKADDR*         sockaddr() const noexcept;
		SOCKADDR_IN*      sockaddr_in() const noexcept;
		SOCKADDR_IN6*     sockaddr_in6() const noexcept;
		SOCKADDR_INET*    sockaddr_inet() const noexcept;
		SOCKADDR_STORAGE* sockaddr_storage() const noexcept;
		IN_ADDR*          in_addr() const noexcept;
		IN6_ADDR*         in6_addr() const noexcept;

	private:
		const size_t SADDR_SIZE = sizeof(SOCKADDR_STORAGE);
		SOCKADDR_STORAGE saddr{};
	};

	//
	// non-member swap
	//
	inline
	void swap(_Inout_ ctSockaddr& left_, _Inout_ ctSockaddr& right_) noexcept
	{
		left_.swap(right_);
	}


	inline ctSockaddr::ctSockaddr(short family) noexcept
	{
		::ZeroMemory(&saddr, SADDR_SIZE);
		saddr.ss_family = family;
	}

	inline ctSockaddr::ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept
	{
		const auto length = static_cast<size_t>(inLength) <= SADDR_SIZE ? inLength : SADDR_SIZE;

		::ZeroMemory(&saddr, SADDR_SIZE);
		::CopyMemory(&saddr, inAddr, length);
	}

	inline ctSockaddr::ctSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, size_t inLength) noexcept
	{
		const auto length = inLength <= SADDR_SIZE ? inLength : SADDR_SIZE;

		::ZeroMemory(&saddr, SADDR_SIZE);
		::CopyMemory(&saddr, inAddr, length);
	}

	inline ctSockaddr::ctSockaddr(const SOCKADDR_IN* inAddr) noexcept
	{
		::ZeroMemory(&saddr, SADDR_SIZE);
		::CopyMemory(&saddr, inAddr, sizeof(SOCKADDR_IN));
	}

	inline ctSockaddr::ctSockaddr(const SOCKADDR_IN6* inAddr) noexcept
	{
		::ZeroMemory(&saddr, SADDR_SIZE);
		::CopyMemory(&saddr, inAddr, sizeof(SOCKADDR_IN6));
	}

	inline ctSockaddr::ctSockaddr(const SOCKADDR_INET* inAddr) noexcept
	{
		::ZeroMemory(&saddr, SADDR_SIZE);
		if (AF_INET == inAddr->si_family) {
			::CopyMemory(&saddr, inAddr, sizeof(SOCKADDR_IN));
		} else {
			::CopyMemory(&saddr, inAddr, sizeof(SOCKADDR_IN6));
		}
	}

	inline ctSockaddr::ctSockaddr(const SOCKADDR_STORAGE* inAddr) noexcept
	{
		::CopyMemory(&saddr, inAddr, SADDR_SIZE);
	}

	inline ctSockaddr::ctSockaddr(const SOCKET_ADDRESS* inAddr) noexcept
	{
		const auto length = static_cast<size_t>(inAddr->iSockaddrLength) <= SADDR_SIZE
		                     ? inAddr->iSockaddrLength
		                     : SADDR_SIZE;

		::ZeroMemory(&saddr, SADDR_SIZE);
		::CopyMemory(&saddr, inAddr->lpSockaddr, length);
	}

	inline ctSockaddr::ctSockaddr(const ctSockaddr& inAddr) noexcept
	{
		::CopyMemory(&saddr, &inAddr.saddr, SADDR_SIZE);
	}

	inline ctSockaddr& ctSockaddr::operator=(const ctSockaddr& inAddr) noexcept
	{
		// copy and swap
		// ReSharper disable once CppUseAuto
		ctSockaddr temp(inAddr);
		this->swap(temp);
		return *this;
	}

	inline ctSockaddr::ctSockaddr(ctSockaddr&& inAddr) noexcept
	{
		::CopyMemory(&saddr, &inAddr.saddr, SADDR_SIZE);
	}

	inline ctSockaddr& ctSockaddr::operator=(ctSockaddr&& inAddr) noexcept
	{
		::CopyMemory(&saddr, &inAddr.saddr, SADDR_SIZE);
		::ZeroMemory(&inAddr.saddr, SADDR_SIZE);
		return *this;
	}

	inline bool ctSockaddr::operator==(const ctSockaddr& _inAddr) const noexcept
	{
		return 0 == memcmp(&this->saddr, &_inAddr.saddr, SADDR_SIZE);
	}

	inline bool ctSockaddr::operator!=(const ctSockaddr& _inAddr) const noexcept
	{
		return !(*this == _inAddr);
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

			if (lhs.scopeId() < rhs.scopeId())
			{
				return true;
			}
			if (lhs.scopeId() > rhs.scopeId())
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

	inline void ctSockaddr::reset(short family) noexcept
	{
		::ZeroMemory(&saddr, SADDR_SIZE);
		saddr.ss_family = family;
	}

	inline void ctSockaddr::swap(_Inout_ ctSockaddr& inAddr) noexcept
	{
		using std::swap;
		swap(saddr, inAddr.saddr);
	}

	inline bool ctSockaddr::setSocketAddress(SOCKET s) const noexcept
	{
		auto namelen = this->length();
		return 0 == ::getsockname(s, this->sockaddr(), &namelen);
	}

	inline void ctSockaddr::setSockaddr(_In_reads_bytes_(inLength) const SOCKADDR* inAddr, int inLength) noexcept
	{
		const auto length = static_cast<size_t>(inLength) <= SADDR_SIZE ? inLength : SADDR_SIZE;

		::ZeroMemory(&saddr, SADDR_SIZE);
		::CopyMemory(&saddr, inAddr, length);
	}

	inline void ctSockaddr::setSockaddr(const SOCKADDR_IN* inAddr) noexcept
	{
		::ZeroMemory(&saddr, SADDR_SIZE);
		::CopyMemory(&saddr, inAddr, sizeof(SOCKADDR_IN));
	}

	inline void ctSockaddr::setSockaddr(const SOCKADDR_IN6* inAddr) noexcept
	{
		::ZeroMemory(&saddr, SADDR_SIZE);
		::CopyMemory(&saddr, inAddr, sizeof(SOCKADDR_IN6));
	}

	inline void ctSockaddr::setSockaddr(const SOCKADDR_INET* inAddr) noexcept
	{
		::ZeroMemory(&saddr, SADDR_SIZE);
		if (AF_INET == inAddr->si_family) {
			::CopyMemory(&saddr, inAddr, sizeof(SOCKADDR_IN));
		} else {
			::CopyMemory(&saddr, inAddr, sizeof(SOCKADDR_IN6));
		}
	}

	inline void ctSockaddr::setSockaddr(const SOCKADDR_STORAGE* inAddr) noexcept
	{
		::CopyMemory(&saddr, inAddr, SADDR_SIZE);
	}

	inline void ctSockaddr::setSockaddr(const SOCKET_ADDRESS* inAddr) noexcept
	{
		const auto length = static_cast<size_t>(inAddr->iSockaddrLength) <= SADDR_SIZE
			                 ? static_cast<size_t>(inAddr->iSockaddrLength)
			                 : SADDR_SIZE;

		::ZeroMemory(&saddr, SADDR_SIZE);
		::CopyMemory(&saddr, inAddr->lpSockaddr, length);
	}

	inline void ctSockaddr::setAddressLoopback() noexcept
	{
		if (AF_INET == saddr.ss_family) {
			const auto in4 = reinterpret_cast<PSOCKADDR_IN>(&saddr);
			const auto in4_port = in4->sin_port;
			::ZeroMemory(&saddr, SADDR_SIZE);
			in4->sin_family = AF_INET;
			in4->sin_port = in4_port;
			in4->sin_addr.s_addr = 0x0100007f; // htons(INADDR_LOOPBACK);
		} else if (AF_INET6 == saddr.ss_family) {
			const auto in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
			const auto in6_port = in6->sin6_port;
			::ZeroMemory(&saddr, SADDR_SIZE);
			in6->sin6_family = AF_INET6;
			in6->sin6_port = in6_port;
			in6->sin6_addr.s6_bytes[15] = 1; // IN6ADDR_LOOPBACK_INIT;
		} else {
			ctAlwaysFatalCondition(
				L"ctSockaddr: unknown family in the SOCKADDR_STORAGE (this %p)", this);
		}
	}

	inline void ctSockaddr::setAddressAny() noexcept
	{
		if (AF_INET == saddr.ss_family) {
			const auto in4 = reinterpret_cast<PSOCKADDR_IN>(&saddr);
			const auto in4_port = in4->sin_port;
			::ZeroMemory(&saddr, SADDR_SIZE);
			in4->sin_family = AF_INET;
			in4->sin_port = in4_port;
		} else if (AF_INET6 == saddr.ss_family) {
			const auto in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
			const auto in6_port = in6->sin6_port;
			::ZeroMemory(&saddr, SADDR_SIZE);
			in6->sin6_family = AF_INET6;
			in6->sin6_port = in6_port;
		}
	}

	inline bool ctSockaddr::isAddressLoopback() const noexcept
	{
		// ReSharper disable once CppUseAuto
		ctSockaddr loopback(*this);
		loopback.setAddressLoopback();
		return 0 == memcmp(&loopback.saddr, &this->saddr, sizeof(SOCKADDR_STORAGE));
	}

	inline bool ctSockaddr::isAddressAny() const noexcept
	{
		// ReSharper disable once CppUseAuto
		ctSockaddr any_addr(*this);
		any_addr.setAddressAny();
		return 0 == memcmp(&any_addr.saddr, &this->saddr, sizeof(SOCKADDR_STORAGE));
	}

	inline void ctSockaddr::setPort(unsigned short port, ByteOrder byteOrder) noexcept
	{
		const auto addr_in = reinterpret_cast<PSOCKADDR_IN>(&saddr);
		addr_in->sin_port = byteOrder == ByteOrder::HostOrder ? ::htons(port) : port;
	}

	inline void ctSockaddr::mapDualMode4To6() noexcept
	{
        const IN6_ADDR v4MappedPrefix = { {IN6ADDR_V4MAPPEDPREFIX_INIT} };

		ctSockaddr tempV6(AF_INET6);
		const auto a6 = tempV6.in6_addr();
		const auto a4 = this->in_addr();

		*a6 = v4MappedPrefix;
		a6->u.Byte[12] = a4->S_un.S_un_b.s_b1;
		a6->u.Byte[13] = a4->S_un.S_un_b.s_b2;
		a6->u.Byte[14] = a4->S_un.S_un_b.s_b3;
		a6->u.Byte[15] = a4->S_un.S_un_b.s_b4;

		tempV6.setPort(this->port());
		this->swap(tempV6);
	}

	inline bool ctSockaddr::setAddress(_In_ LPCWSTR wszAddr) noexcept
	{
		ADDRINFOW addr_hints;
		::ZeroMemory(&addr_hints, sizeof addr_hints);
		addr_hints.ai_flags = AI_NUMERICHOST;

		ADDRINFOW* addr_result = nullptr;
		if (0 == ::GetAddrInfoW(wszAddr, nullptr, &addr_hints, &addr_result)) {
			this->setSockaddr(addr_result->ai_addr, static_cast<int>(addr_result->ai_addrlen));
			::FreeAddrInfoW(addr_result);
			return true;
		}
		return false;
	}

	inline bool ctSockaddr::setAddress(_In_ LPCSTR szAddr) noexcept
	{
		ADDRINFOA addr_hints;
		::ZeroMemory(&addr_hints, sizeof addr_hints);
		addr_hints.ai_flags = AI_NUMERICHOST;

		ADDRINFOA* addr_result = nullptr;
#pragma prefast(suppress:38026, "The explicit use of AI_NUMERICHOST makes GetAddrInfoA's lack of IDN support irrelevant here - they wouldn't be accepted even if we used GetAddrInfoW")
		if (0 == ::GetAddrInfoA(szAddr, nullptr, &addr_hints, &addr_result)) {
			this->setSockaddr(addr_result->ai_addr, static_cast<int>(addr_result->ai_addrlen));
			::FreeAddrInfoA(addr_result);
			return true;
		}
		return false;
	}

	inline void ctSockaddr::setAddress(const IN_ADDR* inAddr) noexcept
	{
		saddr.ss_family = AF_INET;
		const auto addr_in = reinterpret_cast<PSOCKADDR_IN>(&saddr);
		addr_in->sin_addr.S_un.S_addr = inAddr->S_un.S_addr;
	}

	inline void ctSockaddr::setAddress(const IN6_ADDR* inAddr) noexcept
	{
		saddr.ss_family = AF_INET6;
		const auto addr_in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
		addr_in6->sin6_addr = *inAddr;
	}

	inline void ctSockaddr::setFlowInfo(unsigned long flowinfo) noexcept
	{
		if (AF_INET6 == saddr.ss_family) {
			const auto addr_in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
			addr_in6->sin6_flowinfo = flowinfo;
		}
	}

	inline void ctSockaddr::setScopeId(unsigned long scopeid) noexcept
	{
		if (AF_INET6 == saddr.ss_family) {
			const auto addr_in6 = reinterpret_cast<PSOCKADDR_IN6>(&saddr);
			addr_in6->sin6_scope_id = scopeid;
		}
	}

	inline std::wstring ctSockaddr::writeAddress() const
	{
		WCHAR return_string[IP_STRING_MAX_LENGTH];
		(void)this->writeAddress(return_string);
		return_string[IP_STRING_MAX_LENGTH - 1] = L'\0';
		return return_string;
	}

	inline bool ctSockaddr::writeAddress(WCHAR (&address)[IP_STRING_MAX_LENGTH]) const noexcept
	{
		::ZeroMemory(address, IP_STRING_MAX_LENGTH * sizeof(WCHAR));

		const auto pAddr = AF_INET == saddr.ss_family
			                ? reinterpret_cast<PVOID>(this->in_addr())
			                : reinterpret_cast<PVOID>(this->in6_addr());
		return nullptr != ::InetNtopW(saddr.ss_family, pAddr, address, IP_STRING_MAX_LENGTH);
	}

	inline bool ctSockaddr::writeAddress(CHAR (&address)[IP_STRING_MAX_LENGTH]) const noexcept
	{
		::ZeroMemory(address, IP_STRING_MAX_LENGTH * sizeof(CHAR));

		const auto pAddr = AF_INET == saddr.ss_family
			                ? reinterpret_cast<PVOID>(this->in_addr())
			                : reinterpret_cast<PVOID>(this->in6_addr());
		return nullptr != ::InetNtopA(saddr.ss_family, pAddr, address, IP_STRING_MAX_LENGTH);
	}

	inline std::wstring ctSockaddr::writeCompleteAddress(bool trim_scope) const
	{
		WCHAR return_string[IP_STRING_MAX_LENGTH];
		(void)this->writeCompleteAddress(return_string, trim_scope);
		return_string[IP_STRING_MAX_LENGTH - 1] = L'\0';
		return return_string;
	}

	inline bool ctSockaddr::writeCompleteAddress(WCHAR (&address)[IP_STRING_MAX_LENGTH], bool trim_scope) const noexcept
	{
		::ZeroMemory(address, IP_STRING_MAX_LENGTH * sizeof(WCHAR));

		DWORD addressLength = IP_STRING_MAX_LENGTH;
		if (0 == ::WSAAddressToStringW(this->sockaddr(), static_cast<DWORD>(SADDR_SIZE), nullptr, address, &addressLength)) {
			if (this->family() == AF_INET6 && trim_scope) {
				const auto end = address + addressLength;
				auto scope_ptr = std::find(address, end, L'%');
				if (scope_ptr != end) {
					const WCHAR* move_ptr = std::find(address, end, L']');
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
			return true;
		}
		return false;
	}

	inline bool ctSockaddr::writeCompleteAddress(CHAR (&address)[IP_STRING_MAX_LENGTH], bool trim_scope) const noexcept
	{
		::ZeroMemory(address, IP_STRING_MAX_LENGTH * sizeof(CHAR));

		DWORD addressLength = IP_STRING_MAX_LENGTH;
        if (0 == WSAAddressToStringA(this->sockaddr(), static_cast<DWORD>(SADDR_SIZE), nullptr, address, &addressLength)) {
            if (this->family() == AF_INET6 && trim_scope) {
				const auto end = address + addressLength;
				auto scope_ptr = std::find(address, end, '%');
				if (scope_ptr != end) {
					auto move_ptr = std::find(address, end, ']');
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
			return true;
		}
		return false;
	}

	inline int ctSockaddr::length() const noexcept
	{
		return static_cast<int>(SADDR_SIZE);
	}

	inline short ctSockaddr::family() const noexcept
	{
		return saddr.ss_family;
	}

	inline unsigned short ctSockaddr::port() const noexcept
	{
		const auto addr_in = reinterpret_cast<const SOCKADDR_IN*>(&saddr);
		return ::ntohs(addr_in->sin_port);
	}

	inline unsigned long ctSockaddr::flowinfo() const noexcept
	{
		if (AF_INET6 == saddr.ss_family) {
			const auto addr_in6 = reinterpret_cast<const SOCKADDR_IN6*>(&saddr);
			return addr_in6->sin6_flowinfo;
		}
		return 0;
	}

	inline unsigned long ctSockaddr::scopeId() const noexcept
	{
		if (AF_INET6 == saddr.ss_family) {
			const auto addr_in6 = reinterpret_cast<const SOCKADDR_IN6*>(&saddr);
			return addr_in6->sin6_scope_id;
		}
		return 0;
	}

	inline SOCKADDR* ctSockaddr::sockaddr() const noexcept
	{
		return const_cast<SOCKADDR*>(
			reinterpret_cast<const SOCKADDR*>(&saddr));
	}

	inline SOCKADDR_IN* ctSockaddr::sockaddr_in() const noexcept
	{
		return const_cast<SOCKADDR_IN*>(
			reinterpret_cast<const SOCKADDR_IN*>(&saddr));
	}

	inline SOCKADDR_IN6* ctSockaddr::sockaddr_in6() const noexcept
	{
		return const_cast<SOCKADDR_IN6*>(
			reinterpret_cast<const SOCKADDR_IN6*>(&saddr));
	}

	inline SOCKADDR_INET* ctSockaddr::sockaddr_inet() const noexcept
	{
		return const_cast<SOCKADDR_INET*>(
			reinterpret_cast<const SOCKADDR_INET*>(&saddr));
	}

	inline SOCKADDR_STORAGE* ctSockaddr::sockaddr_storage() const noexcept
	{
		return const_cast<SOCKADDR_STORAGE*>(&saddr);
	}

	inline IN_ADDR* ctSockaddr::in_addr() const noexcept
	{
		const auto addr_in = reinterpret_cast<const SOCKADDR_IN*>(&saddr);
		return const_cast<IN_ADDR*>(&addr_in->sin_addr);
	}

	inline IN6_ADDR* ctSockaddr::in6_addr() const noexcept
	{
		const auto addr_in6 = reinterpret_cast<const SOCKADDR_IN6*>(&saddr);
		return const_cast<IN6_ADDR*>(&addr_in6->sin6_addr);
	}
} // namespace ctl

#pragma prefast(pop)
