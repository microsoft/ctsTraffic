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
#include <wil/resource.h>
#include <wil/com.h>

// ctWmiMakeVariant(const ) functions are specializations designed to help callers
// who want a way to construct a VARIANT that is safe for passing into WMI
// since WMI has limitations on what VARIANT types it accepts
namespace ctl
{
    inline bool ctIsVariantEmptyOrNull(const VARIANT* variant) noexcept
    {
        return V_VT(variant) == VT_EMPTY || V_VT(variant) == VT_NULL;
    }

    inline wil::unique_variant ctWmiMakeVariant(const bool value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_BOOL;
        V_BOOL(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, bool* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_BOOL);
        *value = V_BOOL(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const char value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_UI1;
        V_UI1(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, char* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_UI1);
        *value = V_UI1(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const unsigned char value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_UI1;
        V_UI1(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, unsigned char* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_UI1);
        *value = V_UI1(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const short value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_I2;
        V_I2(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, short* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_I2);
        *value = V_I2(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const unsigned short value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_I2;
        V_I2(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, unsigned short* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_I2);
        *value = V_I2(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const long value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_I4;
        V_I4(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, long* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_I4);
        *value = V_I4(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const unsigned long value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_I4;
        V_I4(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, unsigned long* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_I4);
        *value = V_I4(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const int value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_I4;
        V_I4(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, int* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_I4);
        *value = V_I4(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const unsigned int value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_I4;
        V_I4(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, unsigned int* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_I4);
        *value = V_I4(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const float value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_R4;
        V_R4(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, float* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_R4);
        *value = V_R4(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const double value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_R8;
        V_R8(local_variant.addressof()) = value;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, double* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_R8);
        *value = V_R8(variant);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(SYSTEMTIME value)
    {
        wil::unique_variant local_variant;
        DOUBLE time{};
        THROW_HR_IF(E_INVALIDARG, !::SystemTimeToVariantTime(&value, &time));
        V_VT(local_variant.addressof()) = VT_DATE;
        V_DATE(local_variant.addressof()) = time;
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, SYSTEMTIME* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_DATE);
        THROW_HR_IF(E_INVALIDARG, !::VariantTimeToSystemTime(V_DATE(variant), value));
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const BSTR value)  // NOLINT(misc-misplaced-const)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_BSTR;
        V_BSTR(local_variant.addressof()) = SysAllocString(value);
        THROW_IF_NULL_ALLOC(V_BSTR(local_variant.addressof()));
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, BSTR* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_BSTR);
        *value = SysAllocString(V_BSTR(variant));
        THROW_IF_NULL_ALLOC(*value);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const PCWSTR value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_BSTR;
        V_BSTR(local_variant.addressof()) = SysAllocString(value);
        THROW_IF_NULL_ALLOC(V_BSTR(local_variant.addressof()));
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, std::wstring* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_BSTR);
        value->assign(V_BSTR(variant));
        return true;
    }

    // Even though VARIANTs support 64-bit integers, WMI passes them around as BSTRs
    inline wil::unique_variant ctWmiMakeVariant(const UINT64 value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_BSTR;
        V_BSTR(local_variant.addressof()) = SysAllocString(std::to_wstring(value).c_str());
        THROW_IF_NULL_ALLOC(V_BSTR(local_variant.addressof()));
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, _Out_ UINT64* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_BSTR);
        *value = _wcstoui64(V_BSTR(variant), nullptr, 10);
        return true;
    }

    // Even though VARIANTs support 64-bit integers, WMI passes them around as BSTRs
    inline wil::unique_variant ctWmiMakeVariant(const INT64 value)
    {
        wil::unique_variant local_variant;
        V_VT(local_variant.addressof()) = VT_BSTR;
        V_BSTR(local_variant.addressof()) = SysAllocString(std::to_wstring(value).c_str());
        THROW_IF_NULL_ALLOC(V_BSTR(local_variant.addressof()));
        return local_variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, _Out_ INT64* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_BSTR);
        *value = _wcstoi64(V_BSTR(variant), nullptr, 10);
        return true;
    }

    template <typename T>
    wil::unique_variant ctWmiMakeVariant(const wil::com_ptr<T>& value) noexcept
    {
        wil::unique_variant variant;
        V_VT(&variant) = VT_UNKNOWN;
        V_UNKNOWN(variant.addressof()) = value.get();
        // Must deliberately AddRef the raw pointer assigned to punkVal in the variant
        V_UNKNOWN(variant.addressof())->AddRef();
        return variant;
    }
    template <typename T>
    bool ctWmiReadFromVariant(const VARIANT* variant, wil::com_ptr<T>* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != VT_UNKNOWN);
        THROW_IF_FAILED(V_UNKNOWN(variant)->QueryInterface(__uuidof(T), reinterpret_cast<void**>(value->put())));
        return true;
    }

    template <typename T>
    bool ctWmiReadFromVariant(const VARIANT* variant, std::vector<wil::com_ptr<T>>* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != (VT_UNKNOWN | VT_ARRAY));

        IUnknown** iUnknownArray;
        THROW_IF_FAILED(::SafeArrayAccessData(variant->parray, reinterpret_cast<void**>(&iUnknownArray)));
        auto unaccessArray = wil::scope_exit([&]() noexcept {SafeArrayUnaccessData(variant->parray); });

        std::vector<wil::com_ptr<T>> tempData;
        for (unsigned loop = 0; loop < variant->parray->rgsabound[0].cElements; ++loop)
        {
            wil::com_ptr<T> tempPtr;
            THROW_IF_FAILED(iUnknownArray[loop]->QueryInterface(__uuidof(T), reinterpret_cast<void**>(tempPtr.put())));
            tempData.push_back(tempPtr);
        }
        value->swap(tempData);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const std::vector<std::wstring>& data)
    {
        const auto temp_safe_array = SafeArrayCreateVector(VT_BSTR, 0, static_cast<ULONG>(data.size()));
        THROW_IF_NULL_ALLOC(temp_safe_array);
        auto guard_array = wil::scope_exit([&]() noexcept {SafeArrayDestroy(temp_safe_array); });

        for (size_t loop = 0; loop < data.size(); ++loop)
        {
            // SafeArrayPutElement requires an array of indexes for each dimension of the array
            // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
            long index[1]{ static_cast<long>(loop) };

            const auto bstr = SysAllocString(data[loop].c_str());
            THROW_IF_NULL_ALLOC(bstr);
            THROW_IF_FAILED(::SafeArrayPutElement(temp_safe_array, index, bstr));
        }

        wil::unique_variant variant;
        variant.parray = temp_safe_array;
        variant.vt = VT_BSTR | VT_ARRAY;

        // don't free the SAFEARRAY on success - its lifetime is transferred to variant
        guard_array.release();
        return variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, std::vector<std::wstring>* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != (VT_BSTR | VT_ARRAY));

        BSTR* stringArray{};
        THROW_IF_FAILED(::SafeArrayAccessData(variant->parray, reinterpret_cast<void**>(&stringArray)));
        auto unaccessArray = wil::scope_exit([&]() noexcept {SafeArrayUnaccessData(variant->parray); });

        std::vector<std::wstring> tempData;
        for (unsigned loop = 0; loop < variant->parray->rgsabound[0].cElements; ++loop)
        {
            tempData.emplace_back(stringArray[loop]);
        }
        value->swap(tempData);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const std::vector<unsigned long>& data)
    {
        const auto temp_safe_array = SafeArrayCreateVector(VT_UI4, 0, static_cast<ULONG>(data.size()));
        THROW_IF_NULL_ALLOC(temp_safe_array);
        auto guard_array = wil::scope_exit([&]() noexcept {SafeArrayDestroy(temp_safe_array); });

        for (size_t loop = 0; loop < data.size(); ++loop)
        {
            // SafeArrayPutElement requires an array of indexes for each dimension of the array
            // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
            long index[1]{ static_cast<long>(loop) };

            unsigned long value = data[loop];
            THROW_IF_FAILED(::SafeArrayPutElement(temp_safe_array, index, &value));
        }

        wil::unique_variant variant;
        variant.parray = temp_safe_array;
        variant.vt = VT_UI4 | VT_ARRAY;

        // don't free the SAFEARRAY on success - its lifetime is transferred to variant
        guard_array.release();
        return variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, std::vector<unsigned long>* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != (VT_UI4 | VT_ARRAY));

        unsigned long* intArray{};
        THROW_IF_FAILED(::SafeArrayAccessData(variant->parray, reinterpret_cast<void**>(&intArray)));
        auto unaccessArray = wil::scope_exit([&]() noexcept {SafeArrayUnaccessData(variant->parray); });

        std::vector<unsigned long> tempData;
        for (unsigned loop = 0; loop < variant->parray->rgsabound[0].cElements; ++loop)
        {
            tempData.push_back(intArray[loop]);
        }
        value->swap(tempData);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const std::vector<unsigned short>& data)
    {
        // WMI marshaler complaines type mismatch using VT_UI2 | VT_ARRAY, and VT_I4 | VT_ARRAY works fine.
        const auto temp_safe_array = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(data.size()));
        THROW_IF_NULL_ALLOC(temp_safe_array);
        auto guard_array = wil::scope_exit([&]() noexcept {SafeArrayDestroy(temp_safe_array); });

        for (size_t loop = 0; loop < data.size(); ++loop)
        {
            // SafeArrayPutElement requires an array of indexes for each dimension of the array
            // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
            long index[1]{ static_cast<long>(loop) };

            // Expand unsigned short to long because the SAFEARRAY created assumes VT_I4 elements
            long value = data[loop];
            THROW_IF_FAILED(::SafeArrayPutElement(temp_safe_array, index, &value));
        }

        wil::unique_variant variant;
        variant.parray = temp_safe_array;
        variant.vt = VT_I4 | VT_ARRAY;

        // don't free the SAFEARRAY on success - its lifetime is transferred to variant
        guard_array.release();
        return variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, std::vector<unsigned short>* value)
    {
        // WMI marshaler complaines type mismatch using VT_UI2 | VT_ARRAY, and VT_I4 | VT_ARRAY works fine.
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != (VT_I4 | VT_ARRAY));

        long* intArray{};
        THROW_IF_FAILED(::SafeArrayAccessData(variant->parray, reinterpret_cast<void**>(&intArray)));
        auto unaccessArray = wil::scope_exit([&]() noexcept {SafeArrayUnaccessData(variant->parray); });

        std::vector<unsigned short> tempData;
        for (unsigned loop = 0; loop < variant->parray->rgsabound[0].cElements; ++loop)
        {
            THROW_HR_IF(E_INVALIDARG, intArray[loop] > MAXUINT16);
            tempData.push_back(static_cast<unsigned short>(intArray[loop]));
        }
        value->swap(tempData);
        return true;
    }

    inline wil::unique_variant ctWmiMakeVariant(const std::vector<unsigned char>& data)
    {
        const auto temp_safe_array = SafeArrayCreateVector(VT_UI1, 0, static_cast<ULONG>(data.size()));
        THROW_IF_NULL_ALLOC(temp_safe_array);
        auto guard_array = wil::scope_exit([&]() noexcept {SafeArrayDestroy(temp_safe_array); });

        for (size_t loop = 0; loop < data.size(); ++loop)
        {
            // SafeArrayPutElement requires an array of indexes for each dimension of the array
            // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
            long index[1]{ static_cast<long>(loop) };

            unsigned char value = data[loop];
            THROW_IF_FAILED(::SafeArrayPutElement(temp_safe_array, index, &value));
        }

        wil::unique_variant variant;
        variant.parray = temp_safe_array;
        variant.vt = VT_UI1 | VT_ARRAY;

        // don't free the SAFEARRAY on success - its lifetime is transferred to variant
        guard_array.release();
        return variant;
    }
    inline bool ctWmiReadFromVariant(const VARIANT* variant, std::vector<unsigned char>* value)
    {
        if (ctIsVariantEmptyOrNull(variant)) return false;
        THROW_HR_IF(E_INVALIDARG, V_VT(variant) != (VT_UI1 | VT_ARRAY));

        unsigned char* charArray{};
        THROW_IF_FAILED(::SafeArrayAccessData(variant->parray, reinterpret_cast<void**>(&charArray)));
        auto unaccessArray = wil::scope_exit([&]() noexcept {SafeArrayUnaccessData(variant->parray); });

        std::vector<unsigned char> tempData;
        for (unsigned loop = 0; loop < variant->parray->rgsabound[0].cElements; ++loop)
        {
            tempData.push_back(charArray[loop]);
        }
        value->swap(tempData);
        return true;
    }
}
