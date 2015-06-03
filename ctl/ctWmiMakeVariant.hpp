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


namespace ctl {

    //////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctWmiMakeVariant() template functions are specializations designed to help
    ///   callers who want a way to construct a VARIANT (ctComVariant) that is 
    ///   safe for passing into WMI - since WMI has limitations on what VARIANT
    ///   types it accepts.
    ///
    /// Note: all explicit specializations marked inline to avoid ODR violations
    ///
    //////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    ctComVariant ctWmiMakeVariant(T t);

    template <>
    inline ctComVariant ctWmiMakeVariant<bool>(bool t)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_BOOL>(t);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<char>(char _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_UI1>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<unsigned char>(unsigned char _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_UI1>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<short>(short _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_I2>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<unsigned short>(unsigned short _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_I2>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<long>(long _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_I4>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<unsigned long>(unsigned long _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_I4>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<int>(int _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_I4>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<unsigned int>(unsigned int _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_I4>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<float>(float _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_R4>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<double>(double _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_R8>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<SYSTEMTIME>(SYSTEMTIME _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_DATE>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<BSTR>(BSTR _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_BSTR>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<LPCWSTR>(LPCWSTR _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_BSTR>(_vtProp);
    }
    template <>
    inline ctComVariant ctWmiMakeVariant<std::vector<std::wstring>&>(std::vector<std::wstring>& _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_BSTR | VT_ARRAY>(_vtProp);
    }

    template <>
    inline ctComVariant ctWmiMakeVariant<std::vector<unsigned long>&>(std::vector<unsigned long>& _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_UI4 | VT_ARRAY>(_vtProp);
    }

    template <>
    inline ctComVariant ctWmiMakeVariant<std::vector<unsigned short>&>(std::vector<unsigned short>& _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_UI2 | VT_ARRAY>(_vtProp);
    }

    template <>
    inline ctComVariant ctWmiMakeVariant<std::vector<unsigned char>&>(std::vector<unsigned char>& _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign<VT_UI1 | VT_ARRAY>(_vtProp);
    }

    template <>
    inline ctComVariant ctWmiMakeVariant<ctWmiInstance&>(ctWmiInstance& _vtProp)
    {
        ctComVariant local_variant;
        return local_variant.assign(_vtProp.get_instance());
    }

    template <>
    inline ctComVariant ctWmiMakeVariant<std::vector<ctWmiInstance>&>(std::vector<ctWmiInstance>& _vtProp)
    {
        ctComVariant local_variant;
        std::vector<ctComPtr<IWbemClassObject>> local_prop;
        for (const auto& prop : _vtProp) {
            local_prop.push_back(prop.get_instance());
        }
        return local_variant.assign(local_prop);
    }
}
