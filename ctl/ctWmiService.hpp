/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <exception>
// os headers
#include <windows.h>
#include <Wbemidl.h>
// local headers
#include "ctException.hpp"
#include "ctWmiException.hpp"
#include "ctComInitialize.hpp"
#include "ctVersionConversion.hpp"


namespace ctl
{
	////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////
	///
	/// class ctWmiService
	///
	/// Callers must instantiate a ctWmiService instance in order to use any ctWmi* class.  This class
	///   tracks the WMI initialization of the IWbemLocator and IWbemService interfaces - which
	///   maintain a connection to the specified WMI Service through which WMI calls are made.
	///
	////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////
	class ctWmiService
	{
	public:
		/////////////////////////////////////////////////////////////////////////////////////
		///
		/// Note: current only connecting to the local machine.
		///
		/// Note: CoInitializeSecurity is not called by the ctWmi* classes. This security
		///   policy should be defined by the code consuming these libraries, as these
		///   libraries cannot assume the security context to apply to the process.
		///
		/// Argument:
		/// LPCWSTR _path: this is the WMI namespace path to connect with
		///
		/////////////////////////////////////////////////////////////////////////////////////
		explicit ctWmiService(LPCWSTR _path)
		{
			this->wbemLocator = ctComPtr<IWbemLocator>::createInstance(CLSID_WbemLocator, IID_IWbemLocator);

			ctComBstr path(_path);
			auto hr = this->wbemLocator->ConnectServer(
				path.get(), // Object path of WMI namespace
				nullptr, // User name. NULL = current user
				nullptr, // User password. NULL = current
				nullptr, // Locale. NULL indicates current
				0, // Security flags.
				nullptr, // Authority (e.g. Kerberos)
				nullptr, // Context object 
				this->wbemServices.get_addr_of()); // receive pointer to IWbemServices proxy
			if (FAILED(hr)) {
				throw ctException(hr, L"IWbemLocator::ConnectServer", L"ctWmiService::connect", false);
			}

			hr = CoSetProxyBlanket(
				this->wbemServices.get_IUnknown(), // Indicates the proxy to set
				RPC_C_AUTHN_WINNT, // RPC_C_AUTHN_xxx
				RPC_C_AUTHZ_NONE, // RPC_C_AUTHZ_xxx
				nullptr, // Server principal name 
				RPC_C_AUTHN_LEVEL_CALL, // RPC_C_AUTHN_LEVEL_xxx 
				RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
				nullptr, // client identity
				EOAC_NONE // proxy capabilities 
			);
			if (FAILED(hr)) {
				throw ctException(hr, L"CoSetProxyBlanket", L"ctWmiService::connect", false);
			}
		}

		~ctWmiService() = default;

		ctWmiService(const ctWmiService& _service) NOEXCEPT
		: wbemLocator(_service.wbemLocator),
		  wbemServices(_service.wbemServices)
		{
			// empty
		}

		ctWmiService& operator=(const ctWmiService& _service) NOEXCEPT
		{
			ctWmiService temp(_service);
			using std::swap;
			swap(this->wbemLocator, temp.wbemLocator);
			swap(this->wbemServices, temp.wbemServices);
			return *this;
		}

		ctWmiService(ctWmiService&& rhs) NOEXCEPT
		: wbemLocator(std::move(rhs.wbemLocator)),
		  wbemServices(std::move(rhs.wbemServices))
		{
		}
		ctWmiService& operator=(ctWmiService&& rhs) NOEXCEPT
		{
			wbemLocator = std::move(rhs.wbemLocator);
			wbemServices = std::move(rhs.wbemServices);
			return *this;
		}

		////////////////////////////////////////////////////////////////////////////////
		///
		/// operator ->
		/// - exposes the underlying IWbemServices* 
		///
		/// A no-fail/no-throw operation
		////////////////////////////////////////////////////////////////////////////////
		IWbemServices* operator->() NOEXCEPT
		{
			return this->wbemServices.get();
		}

		const IWbemServices* operator ->() const NOEXCEPT
		{
			return this->wbemServices.get();
		}

		bool operator ==(const ctWmiService& _service) const NOEXCEPT
		{
			return this->wbemLocator == _service.wbemLocator &&
				   this->wbemServices == _service.wbemServices;
		}

		bool operator !=(const ctWmiService& _service) const NOEXCEPT
		{
			return !(*this == _service);
		}

		IWbemServices* get() NOEXCEPT
		{
			return this->wbemServices.get();
		}

		const IWbemServices* get() const NOEXCEPT
		{
			return this->wbemServices.get();
		}

		void delete_path(LPCWSTR _objPath, const ctComPtr<IWbemContext>& _context)
		{
			ctComBstr bstrObjectPath(_objPath);
			ctComPtr<IWbemCallResult> result;
			auto hr = this->wbemServices->DeleteInstance(
				bstrObjectPath.get(),
				WBEM_FLAG_RETURN_IMMEDIATELY,
				const_cast<IWbemContext*>(_context.get()),
				result.get_addr_of());
			if (FAILED(hr))
			{
				throw ctWmiException(hr, L"IWbemServices::DeleteInstance", L"ctWmiService::delete_path", false);
			}

			// wait for the call to complete
			HRESULT status;
			hr = result->GetCallStatus(WBEM_INFINITE, &status);
			if (FAILED(hr))
			{
				throw ctWmiException(hr, L"IWbemCallResult::GetCallStatus", L"ctWmiService::delete_path", false);
			}
			if (FAILED(status))
			{
				throw ctWmiException(status, L"IWbemServices::DeleteInstance", L"ctWmiService::delete_path", false);
			}
		}

		////////////////////////////////////////////////////////////////////////////////
		///
		/// void delete_path(LPCWSTR)
		/// - Deletes the WMI object based off the object path specified in the input
		/// - Throws ctWmiException on failures
		///
		/// The object path takes the form of:
		///    MyClass.MyProperty1='33',MyProperty2='value'
		///
		////////////////////////////////////////////////////////////////////////////////
		void delete_path(LPCWSTR _objPath)
		{
			const ctComPtr<IWbemContext> null_context;
			delete_path(_objPath, null_context);
		}

	private:
		//
		// TODO: tracking CoInitialize should not be occuring within these objects
		// - as this is a per-thread call the caller should be enforcing
		//
		ctComInitialize coinit{};
		ctComPtr<IWbemLocator> wbemLocator{};
		ctComPtr<IWbemServices> wbemServices{};
	};
} // namespace ctl
