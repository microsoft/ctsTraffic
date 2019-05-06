/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include <Windows.h>
#include <winsock2.h>
// project headers
#include "ctscopedt.hpp"


namespace ctl
{
	///////////////////////////////////////////////////////////////////////////////////
	///
	///  typedef of ctScopedT<T, Fn> using:
	///  - a general Win32 HANDLE resource type
	///  - a CloseHandle() function to close the HANDLE
	///
	///////////////////////////////////////////////////////////////////////////////////
	struct ctHandleDeleter
	{
		void operator()(const HANDLE h) const noexcept  // NOLINT
		{
			// ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
			if ((h != NULL) && (h != INVALID_HANDLE_VALUE)) {
				::CloseHandle(h);
			}
		}
	};

	typedef ctScopedT<HANDLE, NULL, ctHandleDeleter> ctScopedHandle;


	///////////////////////////////////////////////////////////////////////////////////
	///
	///  typedef of ctScopedT<T, Fn> using:
	///  - a HKEY resource type (registry handles)
	///  - a RegCloseKey() function to close the HANDLE
	///
	///////////////////////////////////////////////////////////////////////////////////
	struct ctHKeyDeleter
	{
		void operator()(const HKEY h) const noexcept  // NOLINT
		{
			// ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
			if ((h != NULL) &&
				(h != HKEY_CLASSES_ROOT) &&
				(h != HKEY_CURRENT_CONFIG) &&
				(h != HKEY_CURRENT_USER) &&
				(h != HKEY_LOCAL_MACHINE) &&
				(h != HKEY_USERS))
			{
				// ignore the pre-defined HKEY values
				::RegCloseKey(h);
			}
		}
	};

	typedef ctScopedT<HKEY, NULL, ctHKeyDeleter> ctScopedHKey;


	///////////////////////////////////////////////////////////////////////////////////
	///
	///  typedef of ctSharedT<T, Fn> using:
	///  - a HANDLE created with the FindFirst* APIs
	///  - a FindClose() function to close the HANDLE
	///
	///////////////////////////////////////////////////////////////////////////////////
	struct ctFindHandleDeleter
	{
		void operator()(const HANDLE h) const noexcept  // NOLINT
		{
			// ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
			if ((h != NULL) && (h != INVALID_HANDLE_VALUE)) {
				::FindClose(h);
			}
		}
	};

	typedef ctScopedT<HANDLE, NULL, ctFindHandleDeleter> ctScopedFindHandle;


	///////////////////////////////////////////////////////////////////////////////////
	///
	///  typedef of ctSharedT<T, Fn> using:
	///  - a HANDLE created with the OpenEventLog API
	///  - a CloseEventLog() function to close the HANDLE
	///
	///////////////////////////////////////////////////////////////////////////////////
	struct ctEventLogHandleDeleter
	{
		void operator()(const HANDLE h) const noexcept  // NOLINT
		{
			// ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
			if ((h != NULL) && (h != INVALID_HANDLE_VALUE)) {
				::CloseEventLog(h);
			}
		}
	};

	typedef ctScopedT<HANDLE, NULL, ctEventLogHandleDeleter> ctScopedEventLogHandle;


	///////////////////////////////////////////////////////////////////////////////////
	//
	///  typedef of ctSharedT<T, Fn> using:
	///   - a HMODULE resource type
	///   - a FreeLibrary() function to close the HMODULE
	///
	///////////////////////////////////////////////////////////////////////////////////
	struct ctLibraryHandleDeleter
	{
		void operator()(const HMODULE h) const noexcept
		{
			// ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
			if (h != NULL) {
				::FreeLibrary(h);
			}
		}
	};

	typedef ctScopedT<HMODULE, NULL, ctLibraryHandleDeleter> ctScopedLibraryHandle;


	///////////////////////////////////////////////////////////////////////////////////
	///
	///  typedef of ctSharedT<T, Fn> using:
	///  - a SC_HANDLE resource type
	///  - a CloseServiceHandle() function to close the SC_HANDLE
	///
	///////////////////////////////////////////////////////////////////////////////////
	struct ctServiceHandleDeleter
	{
		void operator()(const SC_HANDLE h) const noexcept  // NOLINT
		{
			// ReSharper disable once CppZeroConstantCanBeReplacedWithNullptr
			if (h != NULL) {
				::CloseServiceHandle(h);
			}
		}
	};

	typedef ctScopedT<SC_HANDLE, NULL, ctServiceHandleDeleter> ctScopedServiceHandle;


	///////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////
	///
	///  typedef of ctSharedT<T, Fn> using:
	///  - a SOCKET resource type
	///  - a closesocket() function to close the HMODULE
	///
	///////////////////////////////////////////////////////////////////////////////////
	struct ctSocketHandleDeleter
	{
		void operator()(const SOCKET s) const noexcept
		{
			if (s != INVALID_SOCKET) {
				::closesocket(s);
			}
		}
	};

	typedef ctScopedT<SOCKET, INVALID_SOCKET, ctSocketHandleDeleter> ctScopedSocket;
} // namespace ctl
