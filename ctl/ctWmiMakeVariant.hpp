/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <vector>
#include <string>

// os headers
#include <Windows.h>
#include <Objbase.h>

// ctl headers
#include "ctComInitialize.hpp"
#include "ctWmiInstance.hpp"


namespace ctl
{
	//////////////////////////////////////////////////////////////////////////////////////////
	///
	/// ctWmiMakeVariant() functions are specializations designed to help
	///   callers who want a way to construct a VARIANT (ctComVariant) that is 
	///   safe for passing into WMI - since WMI has limitations on what VARIANT
	///   types it accepts.
	///
	/// Note: all explicit specializations marked inline to avoid ODR violations
	///
	//////////////////////////////////////////////////////////////////////////////////////////

	inline ctComVariant ctWmiMakeVariant(bool t)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_BOOL>(t);
	}

	inline ctComVariant ctWmiMakeVariant(char _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_UI1>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(unsigned char _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_UI1>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(short _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_I2>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(unsigned short _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_I2>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(long _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_I4>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(unsigned long _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_I4>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(int _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_I4>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(unsigned int _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_I4>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(float _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_R4>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(double _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_R8>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(SYSTEMTIME _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_DATE>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(BSTR _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_BSTR>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(LPCWSTR _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_BSTR>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(std::vector<std::wstring>& _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_BSTR | VT_ARRAY>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(std::vector<unsigned long>& _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_UI4 | VT_ARRAY>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(std::vector<unsigned short>& _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_UI2 | VT_ARRAY>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(std::vector<unsigned char>& _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign<VT_UI1 | VT_ARRAY>(_vtProp);
	}

	inline ctComVariant ctWmiMakeVariant(ctWmiInstance& _vtProp)
	{
		ctComVariant local_variant;
		return local_variant.assign(_vtProp.get_instance());
	}

	inline ctComVariant ctWmiMakeVariant(std::vector<ctWmiInstance>& _vtProp)
	{
		ctComVariant local_variant;
		std::vector<ctComPtr<IWbemClassObject>> local_prop;
		for (const auto& prop : _vtProp) {
			local_prop.push_back(prop.get_instance());
		}
		return local_variant.assign(local_prop);
	}
}
