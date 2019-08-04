/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <cerrno>
// os headers
#include <Windows.h>
#include <Objbase.h>
// wil headers
#include <wil/resource.h>
// local headers
#include "ctException.hpp"

namespace ctl
{
	class ctComInitialize
	{
	public:
		///////////////////////////////////////////////////////////////////////////////////////////////
		///
		/// ctl classes have no requirement to be explicitly COINIT_APARTMENTTHREADED
		/// - thus defaulting to COINIT_MULTITHREADED as they can be used with either
		///
		///////////////////////////////////////////////////////////////////////////////////////////////
		explicit ctComInitialize(DWORD _threading_model = COINIT_MULTITHREADED)
		{
			const auto hr = ::CoInitializeEx(nullptr, _threading_model);
			switch (hr) {
				case S_OK:
				case S_FALSE:
					m_uninitRequired = true;
					break;
				case RPC_E_CHANGED_MODE:
					m_uninitRequired = false;
					break;
				default:
					throw ctException(hr, L"CoInitializeEx", L"ctComInitialize::ctComInitialize", false);
			}
		}

		~ctComInitialize() noexcept
		{
			if (m_uninitRequired) {
				::CoUninitialize();
			}
		}

		ctComInitialize(const ctComInitialize&) = delete;
		ctComInitialize& operator =(const ctComInitialize&) = delete;
		ctComInitialize(ctComInitialize&&) = delete;
		ctComInitialize& operator =(ctComInitialize&&) = delete;

	private:
		bool m_uninitRequired = false;
	};

    inline bool operator ==(const wil::unique_variant& rhs, const wil::unique_variant& lhs) noexcept
    {
        if (rhs.vt == VT_NULL)
        {
            return lhs.vt == VT_NULL;
        }

        if (rhs.vt == VT_EMPTY)
        {
            return lhs.vt == VT_EMPTY;
        }

        if (rhs.vt == VT_BSTR)
        {
            if (lhs.vt == VT_BSTR)
            {
                return 0 == _wcsicmp(rhs.bstrVal, lhs.bstrVal);
            }
            return false;
        }

        if (rhs.vt == VT_DATE)
        {
            if (lhs.vt == VT_DATE)
            {
                return rhs.date == lhs.date;
            }
            return false;
        }

        //
        // intentionally not supporting comparing floating-point types
        // - it's not going to provide a correct value
        // - the proper comparison should be < or  >
        //
        if (rhs.vt == VT_R4 || lhs.vt == VT_R4 ||
            rhs.vt == VT_R8 || lhs.vt == VT_R8)
        {
            ctAlwaysFatalCondition(L"Not making equality comparisons on floating-point numbers");
        }
        //
        // Comparing integer types - not tightly enforcing type by default
        // - except for VT_BOOL
        // - maintaining that logical BOOLEAN comparison
        //
        // integer values to compare
        // - left hand side ('this' value)
        // - right hand side ('_in' value)
        //
        unsigned rhs_integer;
        switch (rhs.vt)
        {
            case VT_BOOL:
                rhs_integer = static_cast<unsigned>(rhs.boolVal);
                break;
            case VT_I1:
                rhs_integer = static_cast<unsigned>(rhs.cVal);
                break;
            case VT_UI1:
                rhs_integer = static_cast<unsigned>(rhs.bVal);
                break;
            case VT_I2:
                rhs_integer = static_cast<unsigned>(rhs.iVal);
                break;
            case VT_UI2:
                rhs_integer = static_cast<unsigned>(rhs.uiVal);
                break;
            case VT_I4:
                rhs_integer = static_cast<unsigned>(rhs.lVal);
                break;
            case VT_UI4:
                rhs_integer = static_cast<unsigned>(rhs.ulVal);
                break;
            case VT_INT:
                rhs_integer = static_cast<unsigned>(rhs.intVal);
                break;
            case VT_UINT:
                rhs_integer = static_cast<unsigned>(rhs.uintVal);
                break;
            default:
                return false;
        }
        unsigned lhs_integer;
        switch (lhs.vt)
        {
            case VT_BOOL:
                lhs_integer = static_cast<unsigned>(lhs.boolVal);
                break;
            case VT_I1:
                lhs_integer = static_cast<unsigned>(lhs.cVal);
                break;
            case VT_UI1:
                lhs_integer = static_cast<unsigned>(lhs.bVal);
                break;
            case VT_I2:
                lhs_integer = static_cast<unsigned>(lhs.iVal);
                break;
            case VT_UI2:
                lhs_integer = static_cast<unsigned>(lhs.uiVal);
                break;
            case VT_I4:
                lhs_integer = static_cast<unsigned>(lhs.lVal);
                break;
            case VT_UI4:
                lhs_integer = static_cast<unsigned>(lhs.ulVal);
                break;
            case VT_INT:
                lhs_integer = static_cast<unsigned>(lhs.intVal);
                break;
            case VT_UINT:
                lhs_integer = static_cast<unsigned>(lhs.uintVal);
                break;
            default:
                return false;
        }

        if (rhs.vt == VT_BOOL)
        {
            return rhs.boolVal ? lhs_integer != 0 : lhs_integer == 0;
        }

        if (lhs.vt == VT_BOOL)
        {
            return lhs.boolVal ? rhs_integer != 0 : rhs_integer == 0;
        }

        return lhs_integer == rhs_integer;
    }

    inline bool operator !=(const wil::unique_variant& rhs, const wil::unique_variant& lhs) noexcept
    {
        return !(rhs == lhs);
    }
} // namespace ctl
