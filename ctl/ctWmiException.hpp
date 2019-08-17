/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <cwchar>
#include <utility>
#include <vector>
#include <string>
#include <exception>
#include <algorithm>
// os headers
#include <windows.h>
#include <OleAuto.h>
#include <Wbemidl.h>
// wil headers
#include <wil/com.h>
// ctl headers
#include "ctException.hpp"
#include "ctWmiVariant.hpp"

namespace ctl
{
	// ctWmiErrorInfo
	//
	// class which encapsulates information about CIM_Error's that are returned
	//   through *_async methods.
	// 
	// Each __ExtendedStatus and CIM_Error property is exposed though a method
	// All properties return bool - if that property was returned or not.
	//
	// class __ExtendedStatus 
	// {
	//   string ProviderName;
	//   string Operation;
	//   string ParameterInfo;
	//   string Description;
	//   uint StatusCode; 
	// };
	//
	// class CIM_Error
	// {
	//   uint16 ErrorType; xxx
	//   string OtherErrorType;
	//   string OwningEntity;
	//   string MessageID;
	//   string Message;
	//   string MessageArguments[];
	//   uint16 PerceivedSeverity;
	//   uint16 ProbableCause;
	//   string ProbableCauseDescription;
	//   string RecommendedActions[];
	//   string ErrorSource;
	//   uint16 ErrorSourceFormat = 0;
	//   string OtherErrorSourceFormat;
	//   uint32 CIMStatusCode;
	//   string CIMStatusCodeDescription;
	// };

    class ctWmiErrorInfo
	{
		//
		// Marking mutable as the COM methods are not marked 'const'
		//   and 'Get' is the only COM interface invoked from a const method
		//
		mutable wil::com_ptr<IWbemClassObject> m_errorInfo;

	public:
		// c'tors and d'tor
		ctWmiErrorInfo() noexcept
		{
			get_m_errorInfo();
		}

		explicit ctWmiErrorInfo(wil::com_ptr<IWbemClassObject> errorInfo) noexcept
		: m_errorInfo(std::move(errorInfo))
		{
		}

		~ctWmiErrorInfo() noexcept = default;
		ctWmiErrorInfo(const ctWmiErrorInfo&) noexcept = default;
		ctWmiErrorInfo& operator=(const ctWmiErrorInfo&) noexcept = default;
		ctWmiErrorInfo(ctWmiErrorInfo&&) noexcept = default;
		ctWmiErrorInfo& operator=(ctWmiErrorInfo&&) noexcept = default;

		void get_m_errorInfo() const noexcept
		{
			wil::com_ptr<IErrorInfo> errorInfo;
            auto hr = ::GetErrorInfo(0, errorInfo.put());
			if (S_OK == hr)
			{
				//  S_FALSE == no error object was returned
				hr = errorInfo->QueryInterface(IID_IWbemClassObject, reinterpret_cast<void**>(m_errorInfo.put()));
			}
			if (FAILED(hr))
			{
				m_errorInfo.reset();
			}
		}

		void release() const noexcept
		{
			m_errorInfo.reset();
		}

		bool has_error() const noexcept
		{
			return m_errorInfo.get() != nullptr;
		}

		std::wstring error_text() const
		{
			return write_ExtendedStatus() + write_CIMError();
		}

		// ReSharper disable once CppMemberFunctionMayBeConst
		void swap(_Inout_ ctWmiErrorInfo& rhs) noexcept
		{
			using std::swap;
			swap(m_errorInfo, rhs.m_errorInfo);
		}

		//
		// All __ExtendedStatus properties exposed
		//
		_Success_(return) bool get_ProviderName(_Inout_ std::wstring* provider_name) const
		{
			provider_name->clear();
			return get_parameter_value(L"ProviderName", provider_name);
		}

		_Success_(return) bool get_Operation(_Inout_ std::wstring* _operation) const
		{
			_operation->clear();
			return get_parameter_value(L"Operation", _operation);
		}

		_Success_(return) bool get_ParameterInfo(_Inout_ std::wstring* _parameter_info) const
		{
			_parameter_info->clear();
			return get_parameter_value(L"ParameterInfo", _parameter_info);
		}

		_Success_(return) bool get_Description(_Inout_ std::wstring* _description) const
		{
			_description->clear();
			return get_parameter_value(L"Description", _description);
		}

		_Success_(return) bool get_StatusCode(_Out_ unsigned int* _status_code) const
		{
			*_status_code = 0xffffffff;
			return get_parameter_value(L"StatusCode", _status_code);
		}

		//
		// All CIM_Error properties exposed
		//
		_Success_(return) bool get_ErrorType(_Out_ unsigned* _error_type) const
		{
			*_error_type = 0xffffffff;
			return get_parameter_value(L"ErrorType", _error_type);
		}

		_Success_(return) bool get_OtherErrorType(_Inout_ std::wstring* _other_error_type) const
		{
			_other_error_type->clear();
			return get_parameter_value(L"OtherErrorType", _other_error_type);
		}

		_Success_(return) bool get_OwningEntity(_Inout_ std::wstring* _owning_entity) const
		{
			_owning_entity->clear();
			return get_parameter_value(L"OwningEntity", _owning_entity);
		}

		_Success_(return) bool get_MessageID(_Inout_ std::wstring* _message_id) const
		{
			_message_id->clear();
			return get_parameter_value(L"MessageID", _message_id);
		}

		_Success_(return) bool get_Message(_Inout_ std::wstring* _message) const
		{
			_message->clear();
			return get_parameter_value(L"Message", _message);
		}

		_Success_(return) bool get_MessageArguments(_Inout_ std::vector<std::wstring>* _message_arguments) const
		{
			_message_arguments->clear();
			return get_parameter_value(L"MessageArguments", _message_arguments);
		}

		_Success_(return) bool get_PerceivedSeverity(_Out_ unsigned short* _perceived_severity) const
		{
			*_perceived_severity = 0xffff;
			return get_parameter_value(L"PerceivedSeverity", _perceived_severity);
		}

		_Success_(return) bool get_ProbableCause(_Out_ unsigned short* _probable_cause) const
		{
			*_probable_cause = 0xffff;
			return get_parameter_value(L"ProbableCause", _probable_cause);
		}

		_Success_(return) bool get_ProbableCauseDescription(_Inout_ std::wstring* _probable_cause_description) const
		{
			_probable_cause_description->clear();
			return get_parameter_value(L"ProbableCauseDescription", _probable_cause_description);
		}

		_Success_(return) bool get_RecommendedActions(_Inout_ std::vector<std::wstring>* _recommended_actions) const
		{
			_recommended_actions->clear();
			return get_parameter_value(L"RecommendedActions", _recommended_actions);
		}

		_Success_(return) bool get_ErrorSource(_Inout_ std::wstring* _error_source) const
		{
			_error_source->clear();
			return get_parameter_value(L"ErrorSource", _error_source);
		}

		_Success_(return) bool get_ErrorSourceFormat(_Out_ unsigned short* _error_source_format) const
		{
			*_error_source_format = 0xffff;
			return get_parameter_value(L"ErrorSourceFormat", _error_source_format);
		}

		_Success_(return) bool get_OtherErrorSourceFormat(_Inout_ std::wstring* _other_error_source_format) const
		{
			_other_error_source_format->clear();
			return get_parameter_value(L"OtherErrorSourceFormat", _other_error_source_format);
		}

		_Success_(return) bool get_CIMStatusCode(_Out_ unsigned int* _cim_status_code) const
		{
			*_cim_status_code = 0;
			return get_parameter_value(L"CIMStatusCode", _cim_status_code);
		}

		_Success_(return) bool get_CIMStatusCodeDescription(_Inout_ std::wstring* _cim_status_code_description) const
		{
			_cim_status_code_description->clear();
			return get_parameter_value(L"CIMStatusCodeDescription", _cim_status_code_description);
		}

		std::wstring write_ExtendedStatus() const;
		std::wstring write_CIMError() const;

	private:
		template <typename T>
		_Success_(return) bool get_parameter_value(PCWSTR _parameter, _Out_ T* _value) const;
	};

	class ctWmiException final : public ctException
	{
	public:
		//
		// constructors
		//
		ctWmiException() noexcept = default;

		explicit ctWmiException(HRESULT ulCode) noexcept : ctException(ulCode)
		{
		}

		explicit ctWmiException(HRESULT ulCode, const IWbemClassObject* classObject) noexcept :
			ctException(ulCode)
		{
			get_className(classObject);
		}

		explicit ctWmiException(PCWSTR wszMessage, bool bMessageCopy = true) noexcept :
			ctException(wszMessage, bMessageCopy)
		{
		}

		explicit ctWmiException(PCWSTR wszMessage, const IWbemClassObject* classObject, bool bMessageCopy = true) noexcept :
			ctException(wszMessage, bMessageCopy)
		{
			get_className(classObject);
		}

		explicit ctWmiException(HRESULT ulCode, PCWSTR wszMessage, bool bMessageCopy = true) noexcept :
			ctException(ulCode, wszMessage, bMessageCopy)
		{
		}

		explicit ctWmiException(HRESULT ulCode, const IWbemClassObject* classObject, PCWSTR wszMessage, bool bMessageCopy = true) noexcept :
			ctException(ulCode, wszMessage, bMessageCopy)
		{
			get_className(classObject);
		}

		explicit ctWmiException(HRESULT ulCode, PCWSTR wszMessage, PCWSTR wszLocation, bool bBothStringCopy = true) noexcept :
			ctException(ulCode, wszMessage, wszLocation, bBothStringCopy)
		{
		}

		explicit ctWmiException(HRESULT ulCode, const IWbemClassObject* classObject, PCWSTR wszMessage, PCWSTR wszLocation, bool bBothStringCopy = true) noexcept :
			ctException(ulCode, wszMessage, wszLocation, bBothStringCopy)
		{
			get_className(classObject);
		}

		ctWmiException(const ctWmiException& e) noexcept : 
			ctException(e),
		    m_errorInfo(e.m_errorInfo)
		{
			// if this fails, we'll just have nullptr instead of a class name
			// - not failing this just because we can't get extra info
			if (e.m_className)
            {
                *m_className.put() = SysAllocString(e.m_className.get());
			}
		}
		ctWmiException& operator=(const ctWmiException& e) noexcept
		{
			using std::swap;
			auto temp(e);
			swap(*this, temp);
			return *this;
		}
		ctWmiException(ctWmiException&& rhs) noexcept
		: m_className(std::move(rhs.m_className)),
		  m_errorInfo(std::move(rhs.m_errorInfo))
		{
		}
		ctWmiException& operator=(ctWmiException&& rhs) noexcept
        {
            using std::swap;
            auto temp(std::move(rhs));
            swap(*this, temp);
            return *this;
        }

        ~ctWmiException() noexcept override = default;

		// public accessors
        const wchar_t* class_w() const noexcept
		{
			return m_className ? L"" : m_className.get();
		}

        ctWmiErrorInfo error_info() const noexcept
		{
			// copy c'tor of ctWmiErrorInfo is no-throw
			// - so is safe to return a copy here
			return m_errorInfo;
		}

	private:
		wil::unique_bstr m_className;
		ctWmiErrorInfo m_errorInfo;

		void get_className(const IWbemClassObject* classObject) noexcept
		{
			//
			// protect against a null IWbemClassObject pointer
			//
			if (classObject != nullptr) {
				VARIANT variant;
				::VariantInit(&variant);
				// the method should allow to be called from const() methods
				// - forced to const-cast to make this const-correct
				const auto hr = const_cast<IWbemClassObject*>(classObject)->Get(
					L"__CLASS", 0, &variant, nullptr, nullptr);
				if (SUCCEEDED(hr)) {
					// copy the BSTR from the VARIANT
					// - do NOT free the VARIANT
					*m_className.put() = variant.bstrVal;
				}
			}
		}
	};


	inline std::wstring ctWmiErrorInfo::write_ExtendedStatus() const
	{
		// class __ExtendedStatus 
		// {
		//   string ProviderName;
		//   string Operation;
		//   string ParameterInfo;
		//   string Description;
		//   uint StatusCode; 
		// };

		std::wstring wsTemp;
		unsigned int intTemp;
		wchar_t intString[32];

		std::wstring wsprint(L"__ExtendedStatus information:");

		if (get_parameter_value(L"ProviderName", &wsTemp)) {
			wsprint.append(L"\n\tProviderName: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"Operation", &wsTemp)) {
			wsprint.append(L"\n\tOperation: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"ParameterInfo", &wsTemp)) {
			wsprint.append(L"\n\tParameterInfo: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"Description", &wsTemp)) {
			wsprint.append(L"\n\tDescription: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"StatusCode", &intTemp)) {
			if (0 == _ultow_s(intTemp, intString, 10)) {
				intString[31] = L'\0';
				wsprint.append(L"\n\tStatusCode: ");
				wsprint.append(intString);
			}
		}

		wsprint.push_back(L'\n');
		return wsprint;
	}

	inline std::wstring ctWmiErrorInfo::write_CIMError() const
	{
		// class CIM_Error
		// {
		//   uint16 ErrorType;
		//   string OtherErrorType;
		//   string OwningEntity;
		//   string MessageID;
		//   string Message;
		//   string MessageArguments[];
		//   uint16 PerceivedSeverity;
		//   uint16 ProbableCause;
		//   string ProbableCauseDescription;
		//   string RecommendedActions[];
		//   string ErrorSource;
		//   uint16 ErrorSourceFormat = 0;
		//   string OtherErrorSourceFormat;
		//   uint32 CIMStatusCode;
		//   string CIMStatusCodeDescription;
		// };

		std::wstring wsTemp;
		unsigned int intTemp;
		unsigned short shortTemp;
		wchar_t intString[32];
		std::vector<std::wstring> vsTemp;

		std::wstring wsprint(L"CIMError information:");

		if (get_parameter_value(L"ErrorType", &intTemp)) {
			if (0 == _ultow_s(intTemp, intString, 10)) {
				intString[31] = L'\0';
				wsprint.append(L"\n\tErrorType: ");
				wsprint.append(intString);
			}
		}

		if (get_parameter_value(L"OtherErrorType", &wsTemp)) {
			wsprint.append(L"\n\tOtherErrorType: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"OwningEntity", &wsTemp)) {
			wsprint.append(L"\n\tOwningEntity: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"MessageID", &wsTemp)) {
			wsprint.append(L"\n\tMessageID: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"Message", &wsTemp)) {
			wsprint.append(L"\n\tMessage: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"MessageArguments", &vsTemp)) {
			wsprint.append(L"\n\tMessageArguments:");
			for(const auto& wsMessage : vsTemp) {
				wsprint.append(L"\n\t");
				wsprint.append(wsMessage);
			}
		}

		if (get_parameter_value(L"PerceivedSeverity", &shortTemp)) {
			if (0 == _itow_s(shortTemp, intString, 10)) {
				intString[31] = L'\0';
				wsprint.append(L"\n\tPerceivedSeverity: ");
				wsprint.append(intString);
			}
		}

		if (get_parameter_value(L"ProbableCause", &shortTemp)) {
			if (0 == _itow_s(shortTemp, intString, 10)) {
				intString[31] = L'\0';
				wsprint.append(L"\n\tProbableCause: ");
				wsprint.append(intString);
			}
		}

		if (get_parameter_value(L"ProbableCauseDescription", &wsTemp)) {
			wsprint.append(L"\n\tProbableCauseDescription: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"RecommendedActions", &vsTemp)) {
			wsprint.append(L"\n\tRecommendedActions:");
			for (const auto& wsAction : vsTemp) {
				wsprint.append(L"\n\t");
				wsprint.append(wsAction);
			}
		}

		if (get_parameter_value(L"ErrorSource", &wsTemp)) {
			wsprint.append(L"\n\tErrorSource: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"ErrorSourceFormat", &shortTemp)) {
			if (0 == _itow_s(shortTemp, intString, 10)) {
				intString[31] = L'\0';
				wsprint.append(L"\n\tErrorSourceFormat: ");
				wsprint.append(intString);
			}
		}

		if (get_parameter_value(L"OtherErrorSourceFormat", &wsTemp)) {
			wsprint.append(L"\n\tOtherErrorSourceFormat: ");
			wsprint.append(wsTemp);
		}

		if (get_parameter_value(L"CIMStatusCode", &intTemp)) {
			if (0 == _ultow_s(intTemp, intString, 10)) {
				intString[31] = L'\0';
				wsprint.append(L"\n\tCIMStatusCode: ");
				wsprint.append(intString);
			}
		}

		if (get_parameter_value(L"CIMStatusCodeDescription", &wsTemp)) {
			wsprint.append(L"\n\tCIMStatusCodeDescription: ");
			wsprint.append(wsTemp);
		}

		wsprint.push_back(L'\n');
		return wsprint;
	}

	template <typename T>
	_Success_(return) bool ctWmiErrorInfo::get_parameter_value(PCWSTR _parameter, _Out_ T* _value) const
	{
		if (!m_errorInfo)
		{
            return false;
		}

        wil::unique_variant var;
        const auto hr = m_errorInfo->Get(_parameter, 0, var.addressof(), nullptr, nullptr);
        if (hr == WBEM_E_NOT_FOUND)
        {
            return false;
        }

        THROW_IF_FAILED(hr);

        return ctWmiReadFromVariant(var.addressof(), _value);
	}

	//
	// non-member swap function
	//
	inline void swap(_Inout_ ctWmiErrorInfo& rhs, _Inout_ ctWmiErrorInfo& lhs) noexcept
	{
		rhs.swap(lhs);
	}
} // namespace
