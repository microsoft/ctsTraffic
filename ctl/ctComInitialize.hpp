/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <utility>
#include <string>
#include <vector>

// os headers
#include <Windows.h>
#include <Objbase.h>
#include <OleAuto.h>

// local headers
#include "ctException.hpp"
#include "ctScopeGuard.hpp"
#include "ctVersionConversion.hpp"


////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// This header exposes the following interfaces - designed to make use of COM and its resources
///  both exception-safe and enabling RAII design patterns.
///
/// All the below *except ctComInitialize* have the following traits:
/// - all are copyable
/// - all implement get()/set()
/// - all implement swap()
///
/// class ctComInitialize
/// class ctComBstr
/// class ctComVariant
/// template <typename T> class ctComPtr
///
///
/// Compilation units including this header must link against the following libraries:
/// - ole32.lib
/// - oleaut32.lib
///
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

namespace ctl
{

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// Callers are expected to have a ctComInitialize instance alive on every thread
    ///   they use COM and WMI. The ctCom* and ctWmi* classes do not call this from
    ///   within the library code.
    ///
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctComInitialize {
    public:
        ///////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// ctl classes have no requirement to be explicitly COINIT_APARTMENTTHREADED
        /// - thus defaulting to COINIT_MULTITHREADED as they can be used with either
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////
        explicit ctComInitialize(DWORD _threading_model = COINIT_MULTITHREADED) : uninit_required(false)
        {
            HRESULT hr = ::CoInitializeEx(nullptr, _threading_model);
            switch (hr) {
            case S_OK:
            case S_FALSE:
                uninit_required = true;
                break;
            case RPC_E_CHANGED_MODE:
                uninit_required = false;
                break;
            default:
                throw ctException(hr, L"CoInitializeEx", L"ctComInitialize::ctComInitialize", false);
            }
        }
        ~ctComInitialize() NOEXCEPT
        {
            if (uninit_required) {
                ::CoUninitialize();
            }
        }

    private:
        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// This object is non-copyable
        /// - there are options to make it "work", but this would open up other issues
        /// - the caller should verify CoInit on all their threads with unique instances
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComInitialize(const ctComInitialize&);
        ctComInitialize& operator =(const ctComInitialize&);

        bool uninit_required;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// template<typename T>
    /// class ctComPtr
    ///
    /// Template class tracking the lifetime of a COM interface pointer.
    ///
    /// Tracks the COM initialization - guaranteeing uninit when goes out of scope
    ///
    /// TODO: add  create() method to allow CoCreateInstance without forcing the static method
    ///       and therefor not requiring the user to specify an RIID or CLSID
    ///          riid = __uuidof(T);
    ///          LPCOLESTR clsid = "CLSID_" + (StringFromIid(riid)+1);
    ///          CoCreateInstance(clsid, ..., riid, ...)
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename T>
    class ctComPtr {
    public:
        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// createInstance(REFCLSID, REFIID)
        ///
        /// Static utility function to CoCreate the tempate interface type
        /// - exists as a useful factory to construct COM objects
        ///
        /// Will throw a ctException on failure
        ///
        ////////////////////////////////////////////////////////////////////////////////
        static ctComPtr createInstance(_In_ REFCLSID _clsid, _In_ REFIID _riid)
        {
            ctComPtr temp;
            HRESULT hr = ::CoCreateInstance(
                _clsid,
                nullptr,
                CLSCTX_INPROC_SERVER,
                _riid,
                reinterpret_cast<LPVOID*>(&temp.t));
            if (FAILED(hr)) {
                throw ctException(hr, L"CoCreateInstance", L"ctComPtr::createInstance", false);
            }
            return temp;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// c'tor and d'tor
        ///
        /// The caller *should* Release() after assigning into this object to track.
        ///  This c'tor is designed to make refcounting less confusing to the caller
        ///  (they should always match their addref's with their releases)
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComPtr() NOEXCEPT : t(nullptr)
        {
        }

        explicit ctComPtr(_In_opt_ T* _t) NOEXCEPT : t(_t)
        {
            if (t != nullptr) {
                t->AddRef();
            }
        }

        ~ctComPtr() NOEXCEPT
        {
            this->release();
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// copy c'tor and copy assignment
        ///
        /// All are no-throw/no-fail operations
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComPtr(_In_ const ctComPtr& _obj) NOEXCEPT : t(_obj.t)
        {
            if (t != nullptr) {
                t->AddRef();
            }
        }
        ctComPtr& operator =(_In_ const ctComPtr& _obj) NOEXCEPT
        {
            ctComPtr copy(_obj);
            this->swap(copy);
            return *this;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// move c'tor and move assignment
        ///
        /// All are no-throw/no-fail operations
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComPtr(_In_ ctComPtr&& _obj) NOEXCEPT : t(nullptr)
        {
            // initialized to nullptr ... swap with the [in] object
            this->swap(_obj);
        }
        ctComPtr& operator =(_In_ ctComPtr&& _obj) NOEXCEPT
        {
            ctComPtr temp(std::move(_obj));
            this->swap(temp);
            return *this;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// comparison operators
        ///
        /// Boolean comparison on the pointer values
        ///
        /// All are no-throw/no-fail operations
        ///
        ////////////////////////////////////////////////////////////////////////////////
        bool operator ==(_In_ const ctComPtr& _obj) const NOEXCEPT
        {
            return this->t == _obj.t;
        }
        bool operator !=(_In_ const ctComPtr& _obj) const NOEXCEPT
        {
            return this->t != _obj.t;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// void set(T*)
        ///
        /// An explicit assignment method.
        ///
        /// The caller *should* Release() after assigning into this object to track.
        ///  This c'tor isn't designed to make refcounting more confusing to the caller
        ///  (they should always match their addref's with their releases)
        ///
        /// A no-throw/no-fail operation
        ///
        ////////////////////////////////////////////////////////////////////////////////
        void set(_In_ const T* _ptr) NOEXCEPT
        {
            this->release();
            this->t = _ptr;
            if (t != nullptr) {
                this->t->AddRef();
            }
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// accessors
        ///
        /// T* operator ->()
        /// - This getter allows the caller to dereference the object as if they were
        ///   directly calling methods off of the encapsulated object.
        ///
        /// T* get()
        /// - This getter directly retrieves the raw interface pointer value.
        ///
        /// IUnknown* get_IUnknown()
        /// - This getter retrieves an IUnknown* from the encapsulated COM ptr.
        ///   Note that all COM ptrs derive from IUnknown.
        ///   This is provided for type-safety and clarity.
        /// 
        /// T** get_addr_of()
        /// - This directly exposes the address of the encapsulated interface pointer
        ///   This break of encapsulation is required as COM will return interface
        ///    pointer values as [out] parameters.
        ///   Note this is *not* const to allow it to be used as an [out] param.
        ///
        /// All are no-throw/no-fail operations
        ///
        ////////////////////////////////////////////////////////////////////////////////
        T* operator ->() NOEXCEPT
        {
            return t;
        }
        const T* operator ->() const NOEXCEPT
        {
            return t;
        }
        T* get() NOEXCEPT
        {
            return t;
        }
        const T* get() const NOEXCEPT
        {
            return t;
        }
        T** get_addr_of() NOEXCEPT
        {
            this->release();
            return (&t);
        }
        IUnknown* get_IUnknown() NOEXCEPT
        {
            return t;
        }
        const IUnknown* get_IUnknown() const NOEXCEPT
        {
            return t;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// release()
        ///
        /// Explicitly releases a refcount of the encapsulated interface ptr
        /// Note that once released, the object no longer tracks that interface pointer
        ///
        /// A no-throw/no-fail operation
        ///
        ////////////////////////////////////////////////////////////////////////////////
        void release() NOEXCEPT
        {
            if (t != nullptr) {
                t->Release();
            }
            t = nullptr;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// swap(ctComPtr&)
        ///
        /// A no-fail swap() operator to safely swap the internal values of 2 objects
        ///
        ////////////////////////////////////////////////////////////////////////////////
        void swap(_Inout_ ctComPtr& _in) NOEXCEPT
        {
            using std::swap;
            swap(t, _in.t);
        }

    private:
        T* t;
    };

    ////////////////////////////////////////////////////////////////////////////////
    ///
    /// non-member swap()
    ///
    /// Required so the correct swap() function is called if the caller writes:
    ///  (which can be done in STL algorithms as well).
    ///
    /// swap(ctcom1, ctcom2);
    ///
    ////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    inline void swap(_Inout_ ctComPtr<T>& _lhs, _Inout_ ctComPtr<T>& _rhs) NOEXCEPT
    {
        _lhs.swap(_rhs);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// class ctComBstr
    ///
    /// Encapsulates a BSTR value, guaranteeing resource management and exception safety
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctComBstr {
    public:
        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// c'tor and d'tor
        ///
        /// The c'tor can take a raw string ptr which it copies to the BSTR.
        /// - on failure, will throw std::bad_alloc (derived from std::exception)
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComBstr() NOEXCEPT : bstr(nullptr)
        {
        }

        explicit ctComBstr(_In_opt_ LPCWSTR _string) : bstr(nullptr)
        {
            if (_string != nullptr) {
                bstr = ::SysAllocString(_string);
                if (nullptr == bstr) {
                    throw std::bad_alloc();
                }
            }
        }

        explicit ctComBstr(_In_opt_ const BSTR _string) : bstr(nullptr)
        {
            if (_string != nullptr) {
                bstr = ::SysAllocString(_string);
                if (nullptr == bstr) {
                    throw std::bad_alloc();
                }
            }
        }

        ctComBstr(_In_reads_z_(_len) LPCWSTR _string, _In_ size_t _len) : bstr(nullptr)
        {
            if (_string != nullptr) {
                bstr = ::SysAllocStringLen(_string, static_cast<UINT>(_len));
                if (nullptr == bstr) {
                    throw std::bad_alloc();
                }
            }
        }

        ~ctComBstr() NOEXCEPT
        {
            ::SysFreeString(bstr);
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// copy c'tor and copy assignment
        ///
        /// Note that both can fail under low-resources
        /// - will throw std::bad_alloc (derived from std::exception)
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComBstr(_In_ const ctComBstr& _copy) : bstr(nullptr)
        {
            if (_copy.bstr != nullptr) {
                bstr = ::SysAllocString(_copy.bstr);
                if (nullptr == bstr) {
                    throw std::bad_alloc();
                }
            }
        }
        ctComBstr& operator =(_In_ const ctComBstr& _copy)
        {
            ctComBstr temp(_copy);
            this->swap(temp);
            return *this;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// move c'tor and move assignment
        ///
        /// Both are no-fail
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComBstr(_In_ ctComBstr&& _obj) NOEXCEPT : bstr(nullptr)
        {
            this->swap(_obj);
        }
        ctComBstr& operator =(_In_ ctComBstr&& _obj) NOEXCEPT
        {
            ctComBstr temp(std::move(_obj));
            this->swap(temp);
            return *this;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// size() and resize()
        ///
        /// retrieve and modify the internal size of the buffer containing the string
        ///
        ////////////////////////////////////////////////////////////////////////////////
        size_t size() const NOEXCEPT
        {
            // if bstr is nullptr, will return zero
            return ::SysStringLen(bstr);
        }
        void resize(_In_ size_t string_length)
        {
            if (bstr != nullptr) {
                if (!::SysReAllocStringLen(&bstr, nullptr, static_cast<unsigned int>(string_length))) {
                    throw std::bad_alloc();
                }
            } else {
                bstr = ::SysAllocStringLen(nullptr, static_cast<unsigned int>(string_length));
                if (nullptr == bstr) {
                    throw std::bad_alloc();
                }
            }
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// reset(), set(), get()
        ///
        /// explicit BSTR access methods
        ///
        ////////////////////////////////////////////////////////////////////////////////
        void reset() NOEXCEPT
        {
            ::SysFreeString(bstr);
            bstr = nullptr;
        }
        void set(_In_opt_ LPCWSTR _string)
        {
            ctComBstr temp(_string);
            this->swap(temp);
        }
        void set(_In_opt_ const BSTR _bstr)
        {
            ctComBstr temp(_bstr);
            this->swap(temp);
        }
        BSTR get() NOEXCEPT
        {
            return bstr;
        }
        const BSTR get() const NOEXCEPT
        {
            return bstr;
        }
        BSTR* get_addr_of() NOEXCEPT
        {
            ::SysFreeString(bstr);
            bstr = nullptr;
            return &bstr;
        }
        LPCWSTR c_str() const NOEXCEPT
        {
            return static_cast<LPCWSTR>(bstr);
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// swap(ctComBstr&)
        ///
        /// A no-fail swap() operator to safely swap the internal values of 2 objects
        ///
        ////////////////////////////////////////////////////////////////////////////////
        void swap(_Inout_ ctComBstr& _in) NOEXCEPT
        {
            using std::swap;
            swap(bstr, _in.bstr);
        }

    private:
        BSTR bstr;
    };
    ////////////////////////////////////////////////////////////////////////////////
    ///
    /// non-member swap()
    ///
    /// Required so the correct swap() function is called if the caller writes:
    ///  (which can be done in STL algorithms as well).
    ///
    /// swap(bstr1, bstr2);
    ///
    ////////////////////////////////////////////////////////////////////////////////
    inline void swap(_Inout_ ctComBstr& _lhs, _Inout_ ctComBstr& _rhs) NOEXCEPT
    {
        _lhs.swap(_rhs);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    /// class ctComVariant
    ///
    /// Encapsulates a VARIANT value, guaranteeing resource management and exception safety
    ///
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    class ctComVariant {
    public:
        ///////////////////////////////////////////////////////////////////////////////////////////////
        ///
        /// VarTypeConverter
        ///
        /// struct to facilitate establishing a type based off a variant type
        /// - allows for template functions based off of a vartype to accept the correct type
        ///
        ///////////////////////////////////////////////////////////////////////////////////////////////
        template <VARTYPE VT>
        struct VarTypeConverter {};

        template <>
        struct VarTypeConverter<VT_I1> {
            typedef signed char assign_type;
            typedef signed char return_type;
        };
        template <>
        struct VarTypeConverter<VT_UI1> {
            typedef unsigned char assign_type;
            typedef unsigned char return_type;
        };
        template <>
        struct VarTypeConverter<VT_I2> {
            typedef signed short assign_type;
            typedef signed short return_type;
        };
        template <>
        struct VarTypeConverter<VT_UI2> {
            typedef unsigned short assign_type;
            typedef unsigned short return_type;
        };
        template <>
        struct VarTypeConverter<VT_I4> {
            typedef signed long assign_type;
            typedef signed long return_type;
        };
        template <>
        struct VarTypeConverter<VT_UI4> {
            typedef unsigned long assign_type;
            typedef unsigned long return_type;
        };
        template <>
        struct VarTypeConverter<VT_INT> {
            typedef signed int assign_type;
            typedef signed int return_type;
        };
        template <>
        struct VarTypeConverter<VT_UINT> {
            typedef unsigned int assign_type;
            typedef unsigned int return_type;
        };
        template <>
        struct VarTypeConverter<VT_I8> {
            typedef signed long long assign_type;
            typedef signed long long return_type;
        };
        template <>
        struct VarTypeConverter<VT_UI8> {
            typedef unsigned long long assign_type;
            typedef unsigned long long return_type;
        };
        template <>
        struct VarTypeConverter<VT_R4> {
            typedef float assign_type;
            typedef float return_type;
        };
        template <>
        struct VarTypeConverter<VT_R8> {
            typedef double assign_type;
            typedef double return_type;
        };
        template <>
        struct VarTypeConverter<VT_BOOL> {
            typedef bool assign_type;
            typedef bool return_type;
        };
        template <>
        struct VarTypeConverter<VT_BSTR> {
            // Defaulting the VT_BSTR type to LPCWSTR instead of type BSTR
            //   so the user can pass either an LPCWSTR or a BSTR through
            // - a BSTR can pass to a function taking a LPCWSTR
            // - a LPCWSTR can *not* pass to a function taking a BSTR
            typedef LPCWSTR   assign_type;
            typedef ctComBstr return_type;
        };
        template <>
        struct VarTypeConverter<VT_DATE> {
            typedef SYSTEMTIME assign_type;
            typedef SYSTEMTIME return_type;
        };
        template <>
        struct VarTypeConverter<VT_BSTR | VT_ARRAY> {
            typedef const std::vector<std::wstring>& assign_type;
            typedef std::vector<std::wstring>        return_type;
        };
        template <>
        struct VarTypeConverter<VT_UI4 | VT_ARRAY> {
            typedef const std::vector<unsigned long>& assign_type;
            typedef std::vector<unsigned long>        return_type;
        };
        template <>
        struct VarTypeConverter<VT_UI2 | VT_ARRAY> {
            typedef const std::vector<unsigned short>& assign_type;
            typedef std::vector<unsigned short>        return_type;
        };
        template <>
        struct VarTypeConverter<VT_UI1 | VT_ARRAY> {
            typedef const std::vector<unsigned char>& assign_type;
            typedef std::vector<unsigned char>        return_type;
        };

    public:
        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// c'tor and d'tor
        /// 
        /// Guarantees initialization and cleanup of the encapsulated VARIANT
        /// The template constructor directly constructs a VARIANT based off the
        ///  input type, streamlining coding requirements from the caller.
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComVariant() NOEXCEPT
        {
            ::VariantInit(&variant);
        }

        explicit ctComVariant(_In_ const VARIANT* _vt) : variant()
        {
            ::VariantInit(&variant);
            HRESULT hr = ::VariantCopy(&variant, _vt);
            if (FAILED(hr)) {
                throw ctException(hr, L"VariantCopy", L"ctComVariant::ctComVariant", false);
            }
        }

        ~ctComVariant() NOEXCEPT
        {
            ::VariantClear(&variant);
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// copy c'tor and copy assignment
        ///
        /// Note that both can fail under low-resources
        /// - will throw std::bad_alloc (derived from std::exception)
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComVariant(_In_ const ctComVariant& _copy) : variant()
        {
            ::VariantInit(&variant);
            HRESULT hr = ::VariantCopy(&variant, &_copy.variant);
            if (FAILED(hr)) {
                throw ctException(hr, L"VariantCopy", L"ctComVariant::ctComVariant", false);
            }
        }
        ctComVariant& operator =(_In_ const ctComVariant& _copy)
        {
            ctComVariant temp(_copy);
            this->swap(temp);
            return *this;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// move c'tor and move assignment
        ///
        /// both are no-fail
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComVariant(_In_ ctComVariant&& _copy) : variant()
        {
            ::VariantInit(&variant);
            this->swap(_copy);
        }
        ctComVariant& operator =(_In_ ctComVariant&& _copy)
        {
            ctComVariant temp(std::move(_copy));
            this->swap(temp);
            return *this;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// operator ->, reset(), set(), get()
        ///
        /// explicit VARIANT access methods
        ///
        ////////////////////////////////////////////////////////////////////////////////
        void reset() NOEXCEPT
        {
            ::VariantClear(&variant);
            // reinitialize in case someone wants to immediately reuse
            ::VariantInit(&variant);
        }
        void set(_In_ const VARIANT* _vt)
        {
            ctComVariant temp(_vt);
            this->swap(temp);
        }
        VARIANT* operator->() NOEXCEPT
        {
            return (&variant);
        }
        const VARIANT* operator->() const NOEXCEPT
        {
            return (&variant);
        }
        VARIANT* get() NOEXCEPT
        {
            return (&variant);
        }
        const VARIANT* get() const NOEXCEPT
        {
            return (&variant);
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// accessors to query for / modify nullptr and EMPTY variant types
        ///
        /// all are const no-throw
        ///
        ////////////////////////////////////////////////////////////////////////////////
        void set_empty() NOEXCEPT
        {
            this->reset();
            variant.vt = VT_EMPTY;
        }
        void set_null() NOEXCEPT
        {
            this->reset();
            variant.vt = VT_NULL;
        }
        bool is_empty() const NOEXCEPT
        {
            return (variant.vt == VT_EMPTY);
        }
        bool is_null() const NOEXCEPT
        {
            return (variant.vt == VT_NULL);
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// swap(ctComVariant&)
        ///
        /// A no-fail swap() operator to safely swap the internal values of 2 objects
        ///
        ////////////////////////////////////////////////////////////////////////////////
        void swap(_Inout_ ctComVariant& _in) NOEXCEPT
        {
            using std::swap;
            swap(variant, _in.variant);
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// comparison operators
        ///
        ////////////////////////////////////////////////////////////////////////////////
        bool operator ==(_In_ const ctComVariant& _in) const NOEXCEPT
        {
            if (variant.vt == VT_NULL) {
                return (_in.variant.vt == VT_NULL);
            }

            if (variant.vt == VT_EMPTY) {
                return (_in.variant.vt == VT_EMPTY);
            }

            if (variant.vt == VT_BSTR) {
                if (_in.variant.vt == VT_BSTR) {
                    return (0 == _wcsicmp(variant.bstrVal, _in.variant.bstrVal));
                } else {
                    return false;
                }
            }

            if (variant.vt == VT_DATE) {
                if (_in.variant.vt == VT_DATE) {
                    return (variant.date == _in.variant.date);
                } else {
                    return false;
                }
            }

            //
            // intentionally not supporting comparing floating-point types
            // - it's not going to provide a correct value
            // - the proper comparison should be < or  >
            //
            if ((variant.vt == VT_R4) || (_in.variant.vt == VT_R4) ||
                (variant.vt == VT_R8) || (_in.variant.vt == VT_R8)) {
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
            unsigned lhs, rhs;
            switch (variant.vt) {
            case VT_BOOL:
                lhs = static_cast<unsigned>(variant.boolVal);
                break;
            case VT_I1:
                lhs = static_cast<unsigned>(variant.cVal);
                break;
            case VT_UI1:
                lhs = static_cast<unsigned>(variant.bVal);
                break;
            case VT_I2:
                lhs = static_cast<unsigned>(variant.iVal);
                break;
            case VT_UI2:
                lhs = static_cast<unsigned>(variant.uiVal);
                break;
            case VT_I4:
                lhs = static_cast<unsigned>(variant.lVal);
                break;
            case VT_UI4:
                lhs = static_cast<unsigned>(variant.ulVal);
                break;
            case VT_INT:
                lhs = static_cast<unsigned>(variant.intVal);
                break;
            case VT_UINT:
                lhs = static_cast<unsigned>(variant.uintVal);
                break;
            default:
                return false;
            }
            switch (_in.variant.vt) {
            case VT_BOOL:
                rhs = static_cast<unsigned>(_in.variant.boolVal);
                break;
            case VT_I1:
                rhs = static_cast<unsigned>(_in.variant.cVal);
                break;
            case VT_UI1:
                rhs = static_cast<unsigned>(_in.variant.bVal);
                break;
            case VT_I2:
                rhs = static_cast<unsigned>(_in.variant.iVal);
                break;
            case VT_UI2:
                rhs = static_cast<unsigned>(_in.variant.uiVal);
                break;
            case VT_I4:
                rhs = static_cast<unsigned>(_in.variant.lVal);
                break;
            case VT_UI4:
                rhs = static_cast<unsigned>(_in.variant.ulVal);
                break;
            case VT_INT:
                rhs = static_cast<unsigned>(_in.variant.intVal);
                break;
            case VT_UINT:
                rhs = static_cast<unsigned>(_in.variant.uintVal);
                break;
            default:
                return false;
            }

            if (variant.vt == VT_BOOL) {
                return (variant.boolVal) ? (rhs != 0) : (rhs == 0);
            }

            if (_in.variant.vt == VT_BOOL) {
                return (_in.variant.boolVal) ? (lhs != 0) : (lhs == 0);
            }
            return (lhs == rhs);
        }

        bool operator !=(_In_ const ctComVariant& _in) const NOEXCEPT
        {
            return !(*this == _in);
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// write() method to print out the variant value
        /// - optional [in] parameter affects how to print if an integer-type
        ///
        ////////////////////////////////////////////////////////////////////////////////
        ctComBstr write(_In_ bool _int_in_hex = false) const
        {
            static const unsigned IntegerLength = 32;
            ctComBstr bstr;
            unsigned int_radix = (_int_in_hex) ? 16 : 10;

            switch (variant.vt) {
            case VT_EMPTY:
                bstr.set(L"<empty>");
                break;

            case VT_NULL:
                bstr.set(L"<null>");
                break;

            case VT_BOOL:
                bstr.set((variant.boolVal) ? L"true" : L"false");
                break;;

            case VT_I1:
                bstr.resize(IntegerLength);
                _itow_s(variant.cVal, bstr.get(), IntegerLength, int_radix);
                break;

            case VT_UI1:
                bstr.resize(IntegerLength);
                _itow_s(variant.bVal, bstr.get(), IntegerLength, int_radix);
                break;

            case VT_I2:
                bstr.resize(IntegerLength);
                _itow_s(variant.iVal, bstr.get(), IntegerLength, int_radix);
                break;

            case VT_UI2:
                bstr.resize(IntegerLength);
                _itow_s(variant.uiVal, bstr.get(), IntegerLength, int_radix);
                break;

            case VT_I4:
                bstr.resize(IntegerLength);
                _itow_s(variant.lVal, bstr.get(), IntegerLength, int_radix);
                break;

            case VT_UI4:
                bstr.resize(IntegerLength);
                _itow_s(variant.ulVal, bstr.get(), IntegerLength, int_radix);
                break;

            case VT_INT:
                bstr.resize(IntegerLength);
                _itow_s(variant.intVal, bstr.get(), IntegerLength, int_radix);
                break;

            case VT_UINT:
                bstr.resize(IntegerLength);
                _itow_s(variant.uintVal, bstr.get(), IntegerLength, int_radix);
                break;


            case VT_R4:
            {
                std::string float_str(_CVTBUFSIZE, 0x00);
                int err = _gcvt_s(&float_str[0], float_str.size(), variant.fltVal, 4); // up to 4 significant digits
                if (err != 0) {
                    throw ctException(err, L"_gcvt_s", L"ctComVariant::write", false);
                }
                float_str.resize(strlen(&float_str[0]));

                int len = ::MultiByteToWideChar(CP_UTF8, 0, float_str.c_str(), -1, nullptr, 0);
                if (len == 0) {
                    throw ctException(::GetLastError(), L"MultiByteToWideChar", L"ctComVariant::write", false);
                }

                bstr.resize(len);
                len = ::MultiByteToWideChar(CP_UTF8, 0, float_str.c_str(), -1, bstr.get(), len);
                if (len == 0) {
                    throw ctException(::GetLastError(), L"MultiByteToWideChar", L"ctComVariant::write", false);
                }
                break;
            }

            case VT_R8:
            {
                std::string float_str(_CVTBUFSIZE, 0x00);
                int err = _gcvt_s(&float_str[0], float_str.size(), variant.dblVal, 4); // up to 4 significant digits
                if (err != 0) {
                    throw ctException(err, L"_gcvt_s", L"ctComVariant::write", false);
                }
                float_str.resize(strlen(&float_str[0]));

                int len = ::MultiByteToWideChar(CP_UTF8, 0, float_str.c_str(), -1, nullptr, 0);
                if (len == 0) {
                    throw ctException(::GetLastError(), L"MultiByteToWideChar", L"ctComVariant::write", false);
                }

                bstr.resize(len);
                len = ::MultiByteToWideChar(CP_UTF8, 0, float_str.c_str(), -1, bstr.get(), len);
                if (len == 0) {
                    throw ctException(::GetLastError(), L"MultiByteToWideChar", L"ctComVariant::write", false);
                }
                break;
            }

            case VT_BSTR:
                bstr.set(variant.bstrVal);
                break;

            case VT_DATE:
            {
                SYSTEMTIME st;
                retrieve(&st);
                // write out: yyy-mm-dd HH:MM:SS:mmm
                // - based off of how CIM DATETIME is written per http://msdn.microsoft.com/en-us/library/aa387237(VS.85).aspx
                wchar_t sz_time[25];
                if (-1 == ::_snwprintf_s(
                    sz_time, 25, 24,
                    L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds
                    )) {
                    throw ctException(errno, L"_snwprintf_s VT_DATE conversion", L"ctComVariant::write", false);
                }
                bstr.set(sz_time);
                break;
            }


            default:
                throw ctException(variant.vt, L"Unknown VARAINT type", L"ctComVariant::write", false);
            }

            return bstr;
        }

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// Possible VARIANT types (VARTYPE)
        ///
        ///  VT_EMPTY            nothing
        ///  VT_NULL             SQL style Null
        ///  VT_I1               signed char
        ///  VT_I2               2 byte signed int
        ///  VT_I4               4 byte signed int
        ///  VT_UI1              unsigned char
        ///  VT_UI2              unsigned short
        ///  VT_UI4              unsigned long
        ///  VT_INT              signed machine int
        ///  VT_UINT             unsigned machine int
        ///  VT_R4               4 byte real
        ///  VT_R8               8 byte real
        ///  VT_BSTR             OLE Automation string
        ///  VT_BOOL             True=-1, False=0
        ///  VT_DATE             date
        ///  VT_UNKNOWN          IUnknown
        ///
        ///  Not yet implemented coversion methods for the below types:
        ///  VT_CY               currency
        ///  VT_DISPATCH         IDispatch
        ///  VT_ERROR            SCODE
        ///  VT_VARIANT          VARIANT
        ///  VT_DECIMAL          16 byte fixed point
        ///  VT_RECORD           user defined type
        ///  VT_ARRAY            SAFEARRAY*
        ///  VT_BYREF            void* for local use
        ///
        ////////////////////////////////////////////////////////////////////////////////

        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// Assigns a value into the variant based off the explict template
        ///   VARTYPE passed through the template type
        ///
        /// For each possible VARTYPE, the VarTypeConverter struct defines
        ///   the allowable input type (matching the VARTYPE)
        ///
        /// On failure, can throw an std::exception (std::bad_alloc) or
        ///   ctException for types that need conversion (non-integers).
        ///
        ////////////////////////////////////////////////////////////////////////////////
        template <typename T>
        ctComVariant& assign(_In_ ctComPtr<T> _t)
        {
            ctComVariant temp;
            temp.assign_impl(_t);
            this->swap(temp);
            return *this;
        }
        template <typename T>
        ctComVariant& assign(_In_ std::vector<ctComPtr<T>> _t)
        {
            ctComVariant temp;
            temp.assign_impl(_t);
            this->swap(temp);
            return *this;
        }
        template <VARTYPE VT>
        ctComVariant& assign(_In_ typename VarTypeConverter<VT>::assign_type _t)
        {
            ctComVariant temp;
            temp.assign_impl(_t);
            this->swap(temp);
            return *this;
        }


        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// T retrieve<T>()
        ///
        /// inlined accessor to the internal value of the VARIANT
        /// - offers simpler usage model than the below [out] ptr
        ///
        ////////////////////////////////////////////////////////////////////////////////
        template <typename T>
        T retrieve()
        {
            T t;
            return this->retrieve(&t);
        }
        ////////////////////////////////////////////////////////////////////////////////
        ///
        /// T& retrieve(T*)
        ///
        /// Allows retrieval of data from within the variant
        /// Note that returns the [out] value directly - allowing inlining the call.
        /// e.g.
        ///
        /// int data;
        /// printf("Variant integer - %d", retrieve(&data));
        ///
        /// If types are incompatible, will throw a ctException
        ///
        ////////////////////////////////////////////////////////////////////////////////
        signed char& retrieve(_Out_ signed char * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = static_cast<signed char>(variant.boolVal);
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for char", L"ctComVariant::retrieve(signed char)", false);
            }
            return *_data;
        }
        unsigned char& retrieve(_Out_ unsigned char * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = static_cast<unsigned char>(variant.boolVal);
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for unsigned char", L"ctComVariant::retrieve(unsigned char)", false);
            }
            return *_data;
        }
        signed short& retrieve(_Out_ signed short * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = variant.boolVal;
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            case VT_I2:
                *_data = variant.iVal;
                break;
            case VT_UI2:
                *_data = variant.uiVal;
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for short", L"ctComVariant::retrieve(signed short)", false);
            }
            return *_data;
        }
        unsigned short& retrieve(_Out_ unsigned short * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = variant.boolVal;
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            case VT_I2:
                *_data = variant.iVal;
                break;
            case VT_UI2:
                *_data = variant.uiVal;
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for unsigned short", L"ctComVariant::retrieve(unsigned short)", false);
            }
            return *_data;
        }
        signed long& retrieve(_Out_ signed long * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = variant.boolVal;
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            case VT_I2:
                *_data = variant.iVal;
                break;
            case VT_UI2:
                *_data = variant.uiVal;
                break;
            case VT_I4:
                *_data = variant.lVal;
                break;
            case VT_UI4:
                *_data = variant.ulVal;
                break;
            case VT_INT:
                *_data = variant.intVal;
                break;
            case VT_UINT:
                *_data = variant.uintVal;
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for long", L"ctComVariant::retrieve(signed long)", false);
            }
            return *_data;
        }
        unsigned long& retrieve(_Out_ unsigned long * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = variant.boolVal;
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            case VT_I2:
                *_data = variant.iVal;
                break;
            case VT_UI2:
                *_data = variant.uiVal;
                break;
            case VT_I4:
                *_data = variant.lVal;
                break;
            case VT_UI4:
                *_data = variant.ulVal;
                break;
            case VT_INT:
                *_data = variant.intVal;
                break;
            case VT_UINT:
                *_data = variant.uintVal;
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for unsigned long", L"ctComVariant::retrieve(unsigned long)", false);
            }
            return *_data;
        }
        signed int& retrieve(_Out_ signed int * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = variant.boolVal;
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            case VT_I2:
                *_data = variant.iVal;
                break;
            case VT_UI2:
                *_data = variant.uiVal;
                break;
            case VT_I4:
                *_data = variant.lVal;
                break;
            case VT_UI4:
                *_data = variant.ulVal;
                break;
            case VT_INT:
                *_data = variant.intVal;
                break;
            case VT_UINT:
                *_data = variant.uintVal;
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for int", L"ctComVariant::retrieve(signed int)", false);
            }
            return *_data;
        }
        unsigned int& retrieve(_Out_ unsigned int * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = variant.boolVal;
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            case VT_I2:
                *_data = variant.iVal;
                break;
            case VT_UI2:
                *_data = variant.uiVal;
                break;
            case VT_I4:
                *_data = variant.lVal;
                break;
            case VT_UI4:
                *_data = variant.ulVal;
                break;
            case VT_INT:
                *_data = variant.intVal;
                break;
            case VT_UINT:
                *_data = variant.uintVal;
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for unsigned int", L"ctComVariant::retrieve(unsigned int)", false);
            }
            return *_data;
        }
        signed long long& retrieve(_Out_ signed long long * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = variant.boolVal;
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            case VT_I2:
                *_data = variant.iVal;
                break;
            case VT_UI2:
                *_data = variant.uiVal;
                break;
            case VT_I4:
                *_data = variant.lVal;
                break;
            case VT_UI4:
                *_data = variant.ulVal;
                break;
            case VT_INT:
                *_data = variant.intVal;
                break;
            case VT_UINT:
                *_data = variant.uintVal;
                break;
            case VT_I8:
                *_data = variant.llVal;
                break;
            case VT_UI8:
                *_data = variant.ullVal;
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for long", L"ctComVariant::retrieve(signed long)", false);
            }
            return *_data;
        }
        unsigned long long& retrieve(_Out_ unsigned long long * _data) const
        {
            // allow any convertable integer types as long as not losing data
            switch (variant.vt) {
            case VT_BOOL:
                *_data = variant.boolVal;
                break;
            case VT_I1:
                *_data = variant.cVal;
                break;
            case VT_UI1:
                *_data = variant.bVal; // BYTE == unsigned char
                break;
            case VT_I2:
                *_data = variant.iVal;
                break;
            case VT_UI2:
                *_data = variant.uiVal;
                break;
            case VT_I4:
                *_data = variant.lVal;
                break;
            case VT_UI4:
                *_data = variant.ulVal;
                break;
            case VT_INT:
                *_data = variant.intVal;
                break;
            case VT_UINT:
                *_data = variant.uintVal;
                break;
            case VT_I8:
                *_data = variant.llVal;
                break;
            case VT_UI8:
                *_data = variant.ullVal;
                break;
            default:
                throw ctException(variant.vt, L"Mismatching VARTYPE for unsigned long", L"ctComVariant::retrieve(unsigned long)", false);
            }
            return *_data;
        }
        float& retrieve(_Out_ float * _data) const
        {
            if (variant.vt != VT_R4) {
                throw ctException(variant.vt, L"Mismatching VARTYPE for float", L"ctComVariant::retrieve(float)", false);
            }
            *_data = variant.fltVal;
            return *_data;
        }
        double& retrieve(_Out_ double * _data) const
        {
            if ((variant.vt != VT_R4) && (variant.vt != VT_R8)) {
                throw ctException(variant.vt, L"Mismatching VARTYPE for double", L"ctComVariant::retrieve(double)", false);
            }
            *_data = variant.dblVal;
            return *_data;
        }
        bool& retrieve(_Out_ bool * _data) const
        {
            if (variant.vt != VT_BOOL) {
                throw ctException(variant.vt, L"Mismatching VARTYPE for bool", L"ctComVariant::retrieve(bool)", false);
            }
            *_data = (variant.boolVal) ? true : false;
            return *_data;
        }
        ctComBstr& retrieve(_Inout_ ctComBstr * _data) const
        {
            if (variant.vt != VT_BSTR) {
                throw ctException(variant.vt, L"Mismatching VARTYPE for ctComBstr", L"ctComVariant::retrieve(ctComBstr)", false);
            }
            _data->set(variant.bstrVal);
            return *_data;
        }
        std::wstring& retrieve(_Inout_ std::wstring * _data) const
        {
            if (variant.vt != VT_BSTR) {
                throw ctException(variant.vt, L"Mismatching VARTYPE for std::wstring", L"ctComVariant::retrieve(std::wstring)", false);
            }
            if (variant.bstrVal != nullptr) {
                _data->assign(variant.bstrVal);
            } else {
                _data->clear();
            }
            return *_data;
        }
        SYSTEMTIME& retrieve(_Out_ SYSTEMTIME * _data) const
        {
            if (variant.vt != VT_DATE) {
                throw ctException(variant.vt, L"Mismatching VARTYPE for SYSTEMTIME", L"ctComVariant::retrieve(SYSTEMTIME)", false);
            }
            if (!::VariantTimeToSystemTime(variant.date, _data)) {
                throw ctException(::GetLastError(), L"SystemTimeToVariantTime", L"ctComVariant::retrieve(SYSTEMTIME)", false);
            }
            return *_data;
        }
        FILETIME& retrieve(_Out_ FILETIME* _data) const
        {
            if (variant.vt != VT_DATE) {
                throw ctException(variant.vt, L"Mismatching VARTYPE for FILETIME", L"ctComVariant::retrieve(FILETIME)", false);
            }
            SYSTEMTIME st;
            if (!::VariantTimeToSystemTime(variant.date, &st)) {
                throw ctException(::GetLastError(), L"SystemTimeToVariantTime", L"ctComVariant::retrieve(FILETIME)", false);
            }
            if (!::SystemTimeToFileTime(&st, _data)) {
                throw ctException(::GetLastError(), L"SystemTimeToFileTime", L"ctComVariant::retrieve(FILETIME)", false);
            }
            return *_data;
        }
        VARIANT& retrieve(_Inout_ VARIANT* _data) const
        {
            // don't modify the member variant until known success
            ctComVariant temp(&variant);
            // move the copy of this->variant into _data,
            // - and move _data into temp, where it will be cleared on exit
            using std::swap;
            swap(temp.variant, *_data);
            return *_data;
        }
        ctComVariant& retrieve(_Inout_ ctComVariant* _data) const
        {
            _data->set(&variant);
            return *_data;
        }
        std::vector<std::wstring>& retrieve(_Inout_ std::vector<std::wstring>* _data) const
        {
            if (variant.vt != (VT_BSTR | VT_ARRAY)) {
                throw ctException(
                    variant.vt,
                    L"Mismatching VARTYPE for std::vector<std::wstring>",
                    L"ctComVariant::retrieve(std::vector<std::wstring>)",
                    false);
            }

            BSTR *stringArray;
            HRESULT hr = ::SafeArrayAccessData(variant.parray, reinterpret_cast<void **>(&stringArray));
            if (FAILED(hr)) {
                throw ctException(hr, L"SafeArrayAccessData", L"ctComVariant::retrieve(std::vector<std::wstring>)", false);
            }

            // scope guard will guarantee SafeArrayUnaccessData is called on variant.parray even in the face of an exception
            ctlScopeGuard(unaccessArray, {::SafeArrayUnaccessData(variant.parray); });

            // don't modify the out param should an exception be thrown - don't leave the user with bogus data
            std::vector<std::wstring> tempData;
            for (unsigned loop = 0; loop < variant.parray->rgsabound[0].cElements; ++loop) {
                tempData.push_back(stringArray[loop]);
            }

            // swap the safely constructed data to the out param - a no-fail operation
            _data->swap(tempData);
            return *_data;
        }

        std::vector<unsigned long>& retrieve(__out std::vector<unsigned long> * _data) const
        {
            if (variant.vt != (VT_UI4 | VT_ARRAY)) {
                throw ctException(
                    variant.vt,
                    L"Mismatching VARTYPE for std::vector<unsigned long>",
                    L"ctl::ctComVariant::retrieve(std::vector<unsigned long>)",
                    false);
            }

            unsigned long *intArray;
            HRESULT hr = ::SafeArrayAccessData(variant.parray, reinterpret_cast<void **>(&intArray));
            if (FAILED(hr)) {
                throw ctException(hr, L"SafeArrayAccessData", L"ctl::ctComVariant::retrieve(std::vector<std::wstring>)", false);
            }

            // scope guard will guarantee SafeArrayUnaccessData is called on variant.parray even in the face of an exception
            ctlScopeGuard(unaccessArray, {::SafeArrayUnaccessData(variant.parray); });

            // don't modify the out param should an exception be thrown - don't leave the user with bogus data
            std::vector<unsigned long> tempData;
            for (unsigned loop = 0; loop < variant.parray->rgsabound[0].cElements; ++loop) {
                tempData.push_back(intArray[loop]);
            }

            // swap the safely constructed data to the out param - a no-fail operation
            _data->swap(tempData);
            return *_data;
        }

        template <typename T>
        ctComPtr<T>& retrieve(_Inout_ ctComPtr<T>* _data) const
        {
            if (variant.vt != VT_UNKNOWN) {
                throw ctException(variant.vt, L"Mismatching VARTYPE for ctComPtr<T>", L"ctComVariant::retrieve(ctComPtr<T>)", false);
            }

            HRESULT hr = variant.punkVal->QueryInterface(__uuidof(T), reinterpret_cast<void**>(_data->get_addr_of()));
            if (FAILED(hr)) {
                throw ctException(variant.vt, L"IUnknown::QueryInterface", L"ctComVariant::retrieve(ctComPtr<T>)", false);
            }

            return *_data;
        }

        template <typename T>
        std::vector<ctComPtr<T>>& retrieve(_Inout_ std::vector<ctComPtr<T>>* _data) const
        {
            if (variant.vt != (VT_UNKNOWN | VT_ARRAY)) {
                throw ctException(
                    variant.vt,
                    L"Mismatching VARTYPE for std::vector<ctComPtr<T>>",
                    L"ctComVariant::retrieve(std::vector<ctComPtr<T>>)",
                    false);
            }

            IUnknown** iUnknownArray;
            HRESULT hr = ::SafeArrayAccessData(variant.parray, reinterpret_cast<void **>(&iUnknownArray));
            if (FAILED(hr)) {
                throw ctException(hr, L"SafeArrayAccessData", L"ctl::ctComVariant::retrieve(std::vector<ctComPtr<T>>)", false);
            }

            // scope guard will guarantee SafeArrayUnaccessData is called on variant.parray even in the face of an exception
            ctlScopeGuard(unaccessArray, {::SafeArrayUnaccessData(variant.parray); });

            // don't modify the out param should an exception be thrown - don't leave the user with bogus data
            std::vector<ctComPtr<T>> tempData;
            for (unsigned loop = 0; loop < variant.parray->rgsabound[0].cElements; ++loop) {
                ctComPtr<T> tempPtr;

                hr = iUnknownArray[loop]->QueryInterface(__uuidof(T), reinterpret_cast<void**>(tempPtr.get_addr_of()));
                if (FAILED(hr)) {
                    throw ctException(hr, L"IUnknown::QueryInterface", L"ctComVariant::retrieve(std::vector<ctComPtr<T>>)", false);
                }

                tempData.push_back(tempPtr);
            }

            // swap the safely constructed data to the out param - a no-fail operation
            _data->swap(tempData);
            return *_data;
        }

    private:
        VARIANT variant;

        void assign_impl(bool _value) NOEXCEPT
        {
            variant.boolVal = (_value) ? VARIANT_TRUE : VARIANT_FALSE;
            variant.vt = VT_BOOL;
        }
        void assign_impl(signed char _value) NOEXCEPT
        {
            variant.cVal = _value;
            variant.vt = VT_I1;
        }
        void assign_impl(unsigned char _value) NOEXCEPT
        {
            variant.bVal = _value; // BYTE == unsigned char
            variant.vt = VT_UI1;
        }
        void assign_impl(signed short _value) NOEXCEPT
        {
            variant.iVal = _value;
            variant.vt = VT_I2;
        }
        void assign_impl(unsigned short _value) NOEXCEPT
        {
            variant.uiVal = _value;
            variant.vt = VT_UI2;
        }
        void assign_impl(signed long _value) NOEXCEPT
        {
            variant.lVal = _value;
            variant.vt = VT_I4;
        }
        void assign_impl(unsigned long _value) NOEXCEPT
        {
            variant.ulVal = _value;
            variant.vt = VT_UI4;
        }
        void assign_impl(signed int _value) NOEXCEPT
        {
            variant.intVal = _value;
            variant.vt = VT_INT;
        }
        void assign_impl(unsigned int _value) NOEXCEPT
        {
            variant.uintVal = _value;
            variant.vt = VT_UINT;
        }
        void assign_impl(signed long long _value) NOEXCEPT
        {
            variant.llVal = _value;
            variant.vt = VT_I8;
        }
        void assign_impl(unsigned long long _value) NOEXCEPT
        {
            variant.ullVal = _value;
            variant.vt = VT_UI8;
        }
        void assign_impl(float _value) NOEXCEPT
        {
            variant.fltVal = _value;
            variant.vt = VT_R4;
        }
        void assign_impl(double _value) NOEXCEPT
        {
            variant.dblVal = _value;
            variant.vt = VT_R8;
        }

        void assign_impl(_In_opt_ LPCWSTR _data)
        {
            BSTR temp = nullptr;
            if (_data != nullptr) {
                temp = ::SysAllocString(_data);
                if (nullptr == temp) {
                    throw std::bad_alloc();
                }
            }
            variant.bstrVal = temp;
            variant.vt = VT_BSTR;
        }

        void assign_impl(SYSTEMTIME _data)
        {
            DOUBLE time;
            if (!::SystemTimeToVariantTime(&_data, &time)) {
                throw ctException(::GetLastError(), L"SystemTimeToVariantTime", L"ctComVariant::assign", false);
            }
            variant.date = time;
            variant.vt = VT_DATE;
        }

        void assign_impl(_In_ const std::vector<std::wstring>& _data)
        {
            SAFEARRAY* temp_safe_array = ::SafeArrayCreateVector(VT_BSTR, 0, static_cast<ULONG>(_data.size()));
            if (nullptr == temp_safe_array) {
                throw std::bad_alloc();
            }
            // store the SAFEARRY in an exception safe container
            ctlScopeGuard(guard_array, {::SafeArrayDestroy(temp_safe_array); });

            for (size_t loop = 0; loop < _data.size(); ++loop) {
                //
                // SafeArrayPutElement requires an array of indexes for each dimension of the array
                // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
                //
                long index[1] = {static_cast<long>(loop)};

                ctComVariant stringVariant;
                stringVariant.assign<VT_BSTR>(_data[loop].c_str());
                HRESULT hr = ::SafeArrayPutElement(temp_safe_array, index, stringVariant->bstrVal);
                if (FAILED(hr)) {
                    throw ctException(hr, L"SafeArrayPutElement", L"ctComVariant::assign(std::vector<std::wstring>)", false);
                }
            }
            // don't free the SAFEARRAY on success - its lifetime is transferred to this->variant
            guard_array.dismiss();
            variant.parray = temp_safe_array;
            variant.vt = VT_BSTR | VT_ARRAY;
        }

        void assign_impl(__in const std::vector<unsigned long>& _data)
        {
            SAFEARRAY* temp_safe_array = ::SafeArrayCreateVector(VT_UI4, 0, static_cast<ULONG>(_data.size()));
            if (nullptr == temp_safe_array) {
                throw std::bad_alloc();
            }
            // store the SAFEARRY in an exception safe container
            ctlScopeGuard(guard_array, {::SafeArrayDestroy(temp_safe_array); });

            for (size_t loop = 0; loop < _data.size(); ++loop) {
                //
                // SafeArrayPutElement requires an array of indexes for each dimension of the array
                // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
                //
                long index[1] = {static_cast<long>(loop)};

                HRESULT hr = ::SafeArrayPutElement(temp_safe_array, index, const_cast<unsigned long *>(&(_data[loop])));
                if (FAILED(hr)) {
                    throw ctException(hr, L"SafeArrayPutElement", L"ctl::ctComVariant::assign(std::vector<unsigned long>)", false);
                }
            }
            // don't free the SAFEARRAY on success - its lifetime is transferred to this->variant
            guard_array.dismiss();
            variant.parray = temp_safe_array;
            variant.vt = VT_UI4 | VT_ARRAY;
        }

        void assign_impl(__in const std::vector<unsigned short>& _data)
        {
            //WMI marshaler complaines type mismatch using VT_UI2 | VT_ARRAY, and VT_I4 | VT_ARRAY works fine.
            SAFEARRAY* temp_safe_array = ::SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(_data.size()));
            if (nullptr == temp_safe_array) {
                throw std::bad_alloc();
            }
            // store the SAFEARRY in an exception safe container
            ctlScopeGuard(guard_array, {::SafeArrayDestroy(temp_safe_array); });

            for (size_t loop = 0; loop < _data.size(); ++loop) {
                //
                // SafeArrayPutElement requires an array of indexes for each dimension of the array
                // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
                //
                long index[1] = {static_cast<long>(loop)};
                // Expand unsigned short to long because SafeArrayPutElement takes the memory with size equals to the element type
                long value = _data[loop];
                HRESULT hr = ::SafeArrayPutElement(temp_safe_array, index, &value);
                if (FAILED(hr)) {
                    throw ctException(hr, L"SafeArrayPutElement", L"ctl::ctComVariant::assign(std::vector<unsigned short>)", false);
                }
            }
            // don't free the SAFEARRAY on success - its lifetime is transferred to this->variant
            guard_array.dismiss();
            variant.parray = temp_safe_array;
            variant.vt = VT_I4 | VT_ARRAY;
        }

        void assign_impl(__in const std::vector<unsigned char>& _data)
        {
            SAFEARRAY* temp_safe_array = ::SafeArrayCreateVector(VT_UI1, 0, static_cast<ULONG>(_data.size()));
            if (nullptr == temp_safe_array) {
                throw std::bad_alloc();
            }
            // store the SAFEARRY in an exception safe container
            ctlScopeGuard(guard_array, {::SafeArrayDestroy(temp_safe_array); });

            for (size_t loop = 0; loop < _data.size(); ++loop) {
                //
                // SafeArrayPutElement requires an array of indexes for each dimension of the array
                // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
                //
                long index[1] = {static_cast<long>(loop)};

                HRESULT hr = ::SafeArrayPutElement(temp_safe_array, index, const_cast<unsigned char *>(&(_data[loop])));
                if (FAILED(hr)) {
                    throw ctException(hr, L"SafeArrayPutElement", L"ctl::ctComVariant::assign(std::vector<unsigned char>)", false);
                }
            }
            // don't free the SAFEARRAY on success - its lifetime is transferred to this->variant
            guard_array.dismiss();
            variant.parray = temp_safe_array;
            variant.vt = VT_UI1 | VT_ARRAY;
        }

        template <typename T>
        void assign_impl(ctComPtr<T>& _value) NOEXCEPT
        {
            variant.punkVal = _value.get_IUnknown();
            variant.punkVal->AddRef();
            variant.vt = VT_UNKNOWN;
        }

        template <typename T>
        void assign_impl(std::vector<ctComPtr<T>>& _data)
        {
            SAFEARRAY* temp_safe_array = ::SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(_data.size()));
            if (nullptr == temp_safe_array) {
                throw std::bad_alloc();
            }
            // store the SAFEARRY in an exception safe container
            ctlScopeGuard(guard_array, {::SafeArrayDestroy(temp_safe_array); });

            // to be exception safe, AddRef every object before trying to add them to the safe-array
            // - on exception, the ScopeGuard will Release these extra references
            for (auto& ptr : _data) {
                ptr->AddRef();
            }
            ctlScopeGuard(guard_comptr, {
                for (auto& ptr : _data) {
                    ptr->Release();
                }
            });

            for (size_t loop = 0; loop < _data.size(); ++loop) {
                //
                // SafeArrayPutElement requires an array of indexes for each dimension of the array
                // - in this case, we have a 1-dimensional array, thus an array of 1 LONG - assigned to the loop variable
                //
                long index[1] = {static_cast<long>(loop)};

                HRESULT hr = ::SafeArrayPutElement(temp_safe_array, index, _data[loop].get_IUnknown());
                if (FAILED(hr)) {
                    throw ctException(hr, L"SafeArrayPutElement", L"ctl::ctComVariant::assign(std::vector<ctComPtr<T>>)", false);
                }
            }

            // don't free the SAFEARRAY or Release the IUnknown* on success 
            // - the lifetime of both are now transferred to this->variant
            guard_comptr.dismiss();
            guard_array.dismiss();

            variant.parray = temp_safe_array;
            variant.vt = VT_UNKNOWN | VT_ARRAY;
        }
    };

    ////////////////////////////////////////////////////////////////////////////////
    ///
    /// non-member swap()
    ///
    /// Required so the correct swap() function is called if the caller writes:
    ///  (which can be done in STL algorithms as well).
    ///
    /// swap(var1, var2);
    ///
    ////////////////////////////////////////////////////////////////////////////////
    inline void swap(_Inout_ ctComVariant& _lhs, _Inout_ ctComVariant& _rhs) NOEXCEPT
    {
        _lhs.swap(_rhs);
    }

    ////////////////////////////////////////////////////////////////////////////////
    ///
    /// ctString helper implementations
    ///
    /// Defining these here allows ctString functions such as ordinal_equals to
    /// work with ctComBstrs as arguments without requiring every user of ctString
    /// to pull in this header
    ///
    ////////////////////////////////////////////////////////////////////////////////
    namespace ctString
    {
        namespace _detail
        {
            inline _Ret_z_ const wchar_t* convert_to_ptr(const ctComBstr& source)
            {
                return source.c_str();
            }

            inline size_t get_string_length(const ctComBstr& source)
            {
                return source.size();
            }
        }
    } // namespace ctString::_detail

} // namespace ctl
