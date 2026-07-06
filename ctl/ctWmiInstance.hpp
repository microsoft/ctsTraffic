// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include <utility>
#include <algorithm>

#include <Windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <WbemIdl.h>

#include "ctWmiService.hpp"
#include "ctWmiVariant.hpp"

#include <wil/stl.h>
#include <wil/com.h>
#include <wil/resource.h>

namespace ctl
{
	class ctWmiInstance
	{
	public:
		// Constructors:
		// - requires a IWbemServices object already connected to WMI
		explicit ctWmiInstance(ctWmiService service) noexcept :
			m_wbemServices(std::move(service))
		{
		}

		ctWmiInstance(ctWmiService service, _In_ PCWSTR className) :
			m_wbemServices(std::move(service))
		{
			// get the object from the WMI service
			wil::com_ptr<IWbemClassObject> classObject;
			THROW_IF_FAILED(m_wbemServices->GetObject(
				::wil::make_bstr(className).get(),
				0,
				nullptr,
				classObject.addressof(),
				nullptr));
			// spawn an instance of this object
			THROW_IF_FAILED(classObject->SpawnInstance(
				0,
				m_instanceObject.put()));
		}

		ctWmiInstance(ctWmiService service, wil::com_ptr<IWbemClassObject> classObject) noexcept :
			m_wbemServices(std::move(service)),
			m_instanceObject(std::move(classObject))
		{
		}

		bool operator ==(const ctWmiInstance& obj) const noexcept
		{
			return m_wbemServices == obj.m_wbemServices &&
				m_instanceObject == obj.m_instanceObject;
		}

		bool operator !=(const ctWmiInstance& obj) const noexcept
		{
			return !(*this == obj);
		}

		[[nodiscard]] wil::com_ptr<IWbemClassObject> get_instance() const noexcept
		{
			return m_instanceObject;
		}

		[[nodiscard]] wil::unique_bstr get_path() const
		{
			wil::unique_variant objectPathVariant;
			get(L"__RELPATH", objectPathVariant.addressof());

			if (IsVariantEmptyOrNull(objectPathVariant.addressof()))
			{
				return nullptr;
			}

			if (V_VT(&objectPathVariant) != VT_BSTR)
			{
				THROW_HR(E_INVALIDARG);
			}

			return wil::make_bstr(V_BSTR(&objectPathVariant));
		}

		[[nodiscard]] ctWmiService get_service() const noexcept
		{
			return m_wbemServices;
		}

		// Retrieves the class name this ctWmiInstance is representing if any
		[[nodiscard]] wil::unique_bstr get_class_name() const
		{
			wil::unique_variant classVariant;
			get(L"__CLASS", classVariant.addressof());

			if (IsVariantEmptyOrNull(classVariant.addressof()))
			{
				return nullptr;
			}
			if (V_VT(&classVariant) != VT_BSTR)
			{
				THROW_HR(E_INVALIDARG);
			}

			return wil::make_bstr(V_BSTR(&classVariant));
		}

		// Returns a class object for the class represented by this instance
		[[nodiscard]] ctWmiEnumerateClassProperties get_class_object() const noexcept
		{
			return { m_wbemServices, m_instanceObject };
		}

		// Writes the instantiated object to the WMI repository
		// Supported wbemFlags:
		//   WBEM_FLAG_CREATE_OR_UPDATE
		//   WBEM_FLAG_UPDATE_ONLY
		//   WBEM_FLAG_CREATE_ONLY
		void write_instance(_In_opt_ const IWbemContext* context, const LONG wbemFlags = WBEM_FLAG_CREATE_OR_UPDATE)
		{
			THROW_IF_FAILED(write_instance_no_throw(context, wbemFlags));
		}
		HRESULT write_instance_no_throw(_In_opt_ const IWbemContext* context, const LONG wbemFlags = WBEM_FLAG_CREATE_OR_UPDATE) noexcept
		{
			wil::com_ptr<IWbemCallResult> result;
			RETURN_IF_FAILED(m_wbemServices->PutInstance(
				m_instanceObject.get(),
				wbemFlags | WBEM_FLAG_RETURN_IMMEDIATELY,
				const_cast<IWbemContext*>(context),
				result.addressof()));
			// wait for the call to complete
			HRESULT status{};
			RETURN_IF_FAILED(result->GetCallStatus(WBEM_INFINITE, &status));
			RETURN_IF_FAILED(status);
			return status;
		}

		void write_instance(LONG wbemFlags = WBEM_FLAG_CREATE_OR_UPDATE)
		{
			write_instance(nullptr, wbemFlags);
		}

		void delete_instance()
		{
			THROW_IF_FAILED(delete_instance_no_throw());
		}
		HRESULT delete_instance_no_throw() noexcept
		{
			// delete the instance based off the __REPATH property
			wil::com_ptr<IWbemCallResult> result;
			RETURN_IF_FAILED(m_wbemServices->DeleteInstance(
				get_path().get(),
				WBEM_FLAG_RETURN_IMMEDIATELY,
				nullptr,
				result.addressof()));
			// wait for the call to complete
			HRESULT status{};
			RETURN_IF_FAILED(result->GetCallStatus(WBEM_INFINITE, &status));
			RETURN_IF_FAILED(status);
			return status;
		}


		// Invokes an instance method with zero -> 5 arguments from the instantiated IWbemClassObject
		// Returns a ctWmiInstance containing the [out] parameters from the method call
		// (the property "ReturnValue" contains the return value)
		ctWmiInstance execute_method(_In_ PCWSTR method)
		{
			return execute_method_impl(method, nullptr);
		}

		template <typename Arg1>
		ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1)
		{
			// establish the class object for the [in] params to the method
			wil::com_ptr<IWbemClassObject> inParamsDefinition;
			THROW_IF_FAILED(m_instanceObject->GetMethod(
				method,
				0,
				inParamsDefinition.addressof(),
				nullptr));

			// spawn an instance to store the params
			wil::com_ptr<IWbemClassObject> inParamsInstance;
			THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

			// Instantiate a class object to iterate through each property
			const ctWmiEnumerateClassProperties propertyObject(m_wbemServices, inParamsDefinition);
			const auto propertyIterator = propertyObject.begin();

			// write the property
			ctWmiInstance propertyClassObject(m_wbemServices, inParamsInstance);
			propertyClassObject.set(*propertyIterator, arg1);

			// execute the method with the properties set
			return execute_method_impl(method, inParamsInstance.get());
		}

		template <typename Arg1, typename Arg2>
		ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1, Arg2 arg2)
		{
			// establish the class object for the [in] params to the method
			wil::com_ptr<IWbemClassObject> inParamsDefinition;
			THROW_IF_FAILED(m_instanceObject->GetMethod(
				method,
				0,
				inParamsDefinition.addressof(),
				nullptr));

			// spawn an instance to store the params
			wil::com_ptr<IWbemClassObject> inParamsInstance;
			THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

			// Instantiate a class object to iterate through each property
			const ctWmiEnumerateClassProperties propertyObject(m_wbemServices, inParamsDefinition);
			auto propertyIterator = propertyObject.begin();

			// write each property
			ctWmiInstance propertyClassObject(m_wbemServices, inParamsInstance);
			propertyClassObject.set(*propertyIterator, arg1);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg2);

			// execute the method with the properties set
			return execute_method_impl(method, inParamsInstance.get());
		}

		template <typename Arg1, typename Arg2, typename Arg3>
		ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1, Arg2 arg2, Arg3 arg3)
		{
			// establish the class object for the [in] params to the method
			wil::com_ptr<IWbemClassObject> inParamsDefinition;
			THROW_IF_FAILED(m_instanceObject->GetMethod(
				method,
				0,
				inParamsDefinition.addressof(),
				nullptr));

			// spawn an instance to store the params
			wil::com_ptr<IWbemClassObject> inParamsInstance;
			THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

			// Instantiate a class object to iterate through each property
			const ctWmiEnumerateClassProperties propertyObject(m_wbemServices, inParamsDefinition);
			auto propertyIterator = propertyObject.begin();

			// write each property
			ctWmiInstance propertyClassObject(m_wbemServices, inParamsInstance);
			propertyClassObject.set(*propertyIterator, arg1);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg2);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg3);

			// execute the method with the properties set
			return execute_method_impl(method, inParamsInstance.get());
		}

		template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
		ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4)
		{
			// establish the class object for the [in] params to the method
			wil::com_ptr<IWbemClassObject> inParamsDefinition;
			THROW_IF_FAILED(m_instanceObject->GetMethod(
				method,
				0,
				inParamsDefinition.addressof(),
				nullptr));

			// spawn an instance to store the params
			wil::com_ptr<IWbemClassObject> inParamsInstance;
			THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

			// Instantiate a class object to iterate through each property
			const ctWmiEnumerateClassProperties propertyObject(m_wbemServices, inParamsDefinition);
			auto propertyIterator = propertyObject.begin();

			// write each property
			ctWmiInstance propertyClassObject(m_wbemServices, inParamsInstance);
			propertyClassObject.set(*propertyIterator, arg1);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg2);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg3);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg4);

			// execute the method with the properties set
			return execute_method_impl(method, inParamsInstance.get());
		}

		template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
		ctWmiInstance execute_method(_In_ PCWSTR method, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5)
		{
			// establish the class object for the [in] params to the method
			wil::com_ptr<IWbemClassObject> inParamsDefinition;
			THROW_IF_FAILED(m_instanceObject->GetMethod(
				method,
				0,
				inParamsDefinition.addressof(),
				nullptr));

			// spawn an instance to store the params
			wil::com_ptr<IWbemClassObject> inParamsInstance;
			THROW_IF_FAILED(inParamsDefinition->SpawnInstance(0, inParamsInstance.addressof()));

			// Instantiate a class object to iterate through each property
			const ctWmiEnumerateClassProperties propertyObject(m_wbemServices, inParamsDefinition);
			auto propertyIterator = propertyObject.begin();

			// write each property
			//
			ctWmiInstance propertyClassObject(m_wbemServices, inParamsInstance);
			propertyClassObject.set(*propertyIterator, arg1);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg2);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg3);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg4);
			++propertyIterator;
			propertyClassObject.set(*propertyIterator, arg5);

			// execute the method with the properties set
			return execute_method_impl(method, inParamsInstance.get());
		}

		bool is_null(_In_ PCWSTR property_name) const
		{
			wil::unique_variant variant;
			THROW_IF_FAILED(get_property_no_throw(property_name, variant.addressof()));
			return V_VT(&variant) == VT_NULL;
		}

		// get() and set()
		//
		//   Exposes the properties of the WMI object instantiated
		//   WMI instances don't use all VARIANT types - some specializations
		//   exist because, for example, 64-bit integers actually get passed through
		//   WMI as BSTRs (even though variants support 64-bit integers directly).
		//   See the MSDN documentation for WMI MOF Data Types (Numbers):
		//   http://msdn.microsoft.com/en-us/library/aa392716(v=VS.85).aspx
		//
		//   Even though VARIANTS support 16- and 32-bit unsigned integers, WMI passes them both
		//   around as 32-bit signed integers. Yes, that means you can't pass very large UINT32 values
		//   correctly through WMI directly.
		//
		// get() returns false if the value is empty or null
		//       returns true if retrieved the matching type

		HRESULT get_no_throw(_In_ PCWSTR property_name, _Inout_ VARIANT* value) const noexcept
		{
			VariantClear(value);
			RETURN_IF_FAILED(get_property_no_throw(property_name, value));
			return S_OK;
		}

		bool get(_In_ PCWSTR property_name, _Inout_ wil::unique_variant* value) const
		{
			VariantClear(value->addressof());
			THROW_IF_FAILED(get_property_no_throw(property_name, value->addressof()));
			return !IsVariantEmptyOrNull(value->addressof());
		}

		bool get(_In_ PCWSTR property_name, _Inout_ VARIANT* value) const
		{
			VariantClear(value);
			THROW_IF_FAILED(get_property_no_throw(property_name, value));
			return !IsVariantEmptyOrNull(value);
		}

		template <typename T>
		bool get(_In_ PCWSTR property_name, _Out_ T* value) const
		{
			*value = {};
			wil::unique_variant variant;
			THROW_IF_FAILED(get_property_no_throw(property_name, variant.addressof()));
			return ctWmiReadFromVariant(variant.addressof(), value);
		}

		HRESULT set_no_throw(_In_ PCWSTR property_name, _In_ const VARIANT* value) const noexcept
		{
			return set_property_no_throw(property_name, value);
		}

		void set(_In_ PCWSTR property_name, _In_ const VARIANT* value) const
		{
			THROW_IF_FAILED(set_property_no_throw(property_name, value));
		}

		template <typename T>
		void set(_In_ PCWSTR property_name, const T value) const
		{
			THROW_IF_FAILED(set_property_no_throw(property_name, ctWmiMakeVariant(value).addressof()));
		}

		// Calling IWbemClassObject::Delete on a property of an instance resets to the default value.
		void set_default(_In_ PCWSTR property_name) const
		{
			THROW_IF_FAILED(m_instanceObject->Delete(property_name));
		}

		HRESULT set_if_not_null_no_throw(_In_ PCWSTR property_name, _Inout_opt_ const VARIANT* value) const noexcept
		{
			if (IsVariantEmptyOrNull(value))
			{
				return S_FALSE;
			}
			return set_property_no_throw(property_name, value);
		}

		HRESULT set_if_not_null_no_throw(_In_ PCWSTR property_name, const wil::unique_variant& value) const noexcept
		{
			const VARIANT* pValue = const_cast<wil::unique_variant*>(&value)->addressof();
			return set_if_not_null_no_throw(property_name, pValue);
		}

	private:
		HRESULT get_property_no_throw(_In_ PCWSTR propertyName, _Inout_ VARIANT* pVariant) const noexcept
		{
			auto* pInstance = m_instanceObject.get();
			const auto hr = pInstance->Get(
				propertyName,
				0,
				pVariant,
				nullptr,
				nullptr);
#ifdef ENABLE_WMI_DEBUG_OUTPUT
			wprintf(L"\t** ctWmiInstance::get_property_no_throw(%ws) returned HRESULT: 0x%08X, returned variant type 0x%x\n", propertyName, hr, pVariant->vt);
#endif
			RETURN_IF_FAILED(hr);
			return S_OK;
		}

		HRESULT set_property_no_throw(_In_ PCWSTR property_name, _In_ const VARIANT* pVariant) const noexcept
		{
			auto* pInstance = m_instanceObject.get();
			const auto hr = pInstance->Put(
				property_name,
				0,
				const_cast<VARIANT*>(pVariant), // COM is not const-correct
				0);
#if defined(ENABLE_WMI_DEBUG_OUTPUT)
			wprintf(L"\t** ctWmiInstance::set_property_no_throw(%ws) returned HRESULT: 0x%08X, setting variant type 0x%x\n", property_name, hr, pVariant->vt);
#endif
			RETURN_IF_FAILED(hr);
			return S_OK;
		}

		ctWmiInstance execute_method_impl(_In_ PCWSTR method, _In_opt_ IWbemClassObject* pParams)
		{
			// exec the method semi-synchronously from this instance based off the __REPATH property
			wil::com_ptr<IWbemCallResult> result;
			THROW_IF_FAILED(m_wbemServices->ExecMethod(
				get_path().get(),
				::wil::make_bstr(method).get(),
				WBEM_FLAG_RETURN_IMMEDIATELY,
				nullptr,
				pParams,
				nullptr,
				result.addressof()));

			// wait for the call to complete - and get the [out] param object
			wil::com_ptr<IWbemClassObject> outParamsInstance;
			THROW_IF_FAILED(result->GetResultObject(WBEM_INFINITE, outParamsInstance.addressof()));

			// the call went through - return a ctWmiInstance from this retrieved instance
			return { m_wbemServices, outParamsInstance };
		}

		ctWmiService m_wbemServices;
		wil::com_ptr<IWbemClassObject> m_instanceObject;
	};

	class ctWmiEnumerateInstance
	{
	public:
		// A forward iterator class type to enable forward-traversing instances of the queried WMI provider
		// 
		class iterator
		{
		public:
			explicit iterator(ctWmiService service) noexcept :
				m_wbemServices(std::move(service))
			{
			}

			iterator(ctWmiService service, wil::com_ptr<IEnumWbemClassObject> wbemEnumerator) :
				m_index(0),
				m_wbemServices(std::move(service)),
				m_wbemEnumerator(std::move(wbemEnumerator))
			{
				increment();
			}

			~iterator() noexcept = default;
			iterator(const iterator&) noexcept = default;
			iterator& operator =(const iterator&) noexcept = default;
			iterator(iterator&&) noexcept = default;
			iterator& operator =(iterator&&) noexcept = default;

			void swap(_Inout_ iterator& rhs) noexcept
			{
				using std::swap;
				swap(m_index, rhs.m_index);
				swap(m_wbemServices, rhs.m_wbemServices);
				swap(m_wbemEnumerator, rhs.m_wbemEnumerator);
				swap(m_wmiInstance, rhs.m_wmiInstance);
			}

			[[nodiscard]] uint32_t location() const noexcept
			{
				return m_index;
			}

			ctWmiInstance& operator*() const noexcept
			{
				return *m_wmiInstance;
			}

			ctWmiInstance* operator->() const noexcept
			{
				return m_wmiInstance.get();
			}

			bool operator==(const iterator& iter) const noexcept
			{
				if (m_index != c_endIteratorIndex)
				{
					return m_index == iter.m_index &&
						m_wbemServices == iter.m_wbemServices &&
						m_wbemEnumerator == iter.m_wbemEnumerator &&
						m_wmiInstance == iter.m_wmiInstance;
				}
				return m_index == iter.m_index &&
					m_wbemServices == iter.m_wbemServices;
			}

			bool operator!=(const iterator& iter) const noexcept
			{
				return !(*this == iter);
			}


			iterator& operator++()
			{
				increment();
				return *this;
			}

			iterator operator++(int)
			{
				auto temp(*this);
				increment();
				return temp;
			}

			iterator& operator+=(uint32_t inc)
			{
				for (auto loop = 0ul; loop < inc; ++loop)
				{
					increment();
					if (m_index == c_endIteratorIndex)
					{
						throw std::out_of_range("ctWmiEnumerate::iterator::operator+= - invalid subscript");
					}
				}
				return *this;
			}


			// iterator_traits
			// - allows <algorithm> functions to be used
			using iterator_category = std::forward_iterator_tag;
			using value_type = ctWmiInstance;
			using difference_type = int;
			using pointer = ctWmiInstance*;
			using reference = ctWmiInstance&;

		private:
			void increment()
			{
				if (m_index == c_endIteratorIndex)
				{
					throw std::out_of_range("ctWmiEnumerate::iterator::increment at the end");
				}

				ULONG uReturn{};
				wil::com_ptr<IWbemClassObject> wbemTarget;
				THROW_IF_FAILED(m_wbemEnumerator->Next(
					WBEM_INFINITE,
					1,
					wbemTarget.put(),
					&uReturn));
				if (0 == uReturn)
				{
					// at the end...
					m_index = c_endIteratorIndex;
					m_wmiInstance.reset();
				}
				else
				{
					++m_index;
					m_wmiInstance = std::make_shared<ctWmiInstance>(m_wbemServices, wbemTarget);
				}
			}

			static constexpr uint32_t c_endIteratorIndex = ULONG_MAX;
			uint32_t m_index = c_endIteratorIndex;
			ctWmiService m_wbemServices;
			wil::com_ptr<IEnumWbemClassObject> m_wbemEnumerator;
			std::shared_ptr<ctWmiInstance> m_wmiInstance;
		};

		static ctWmiEnumerateInstance Query(_In_ PCWSTR query, ctWmiService wbemServices = ctWmiService{ L"ROOT\\StandardCimv2" })
		{
			ctWmiEnumerateInstance instance(std::move(wbemServices));
			return instance.query(query);
		}

		static ctWmiEnumerateInstance Query(_In_ PCWSTR query, const wil::com_ptr<IWbemContext>& context, ctWmiService wbemServices = ctWmiService{ L"ROOT\\StandardCimv2" })
		{
			ctWmiEnumerateInstance instance(std::move(wbemServices));
			return instance.query(query, context);
		}

		explicit ctWmiEnumerateInstance(ctWmiService wbemServices) noexcept :
			m_wbemServices(std::move(wbemServices))
		{
		}

		// Allows for executing a WMI query against the WMI service for an enumeration of WMI objects.
		// Assumes the query of the WQL query language.
		const ctWmiEnumerateInstance& query(_In_ PCWSTR query)
		{
			THROW_IF_FAILED(m_wbemServices->ExecQuery(
				::wil::make_bstr(L"WQL").get(),
				::wil::make_bstr(query).get(),
				WBEM_FLAG_BIDIRECTIONAL,
				nullptr,
				m_wbemEnumerator.put()));
			return *this;
		}

		const ctWmiEnumerateInstance& query(_In_ PCWSTR query, const wil::com_ptr<IWbemContext>& context)
		{
			THROW_IF_FAILED(m_wbemServices->ExecQuery(
				::wil::make_bstr(L"WQL").get(),
				::wil::make_bstr(query).get(),
				WBEM_FLAG_BIDIRECTIONAL,
				context.get(),
				m_wbemEnumerator.put()));
			return *this;
		}

		iterator begin() const
		{
			if (nullptr == m_wbemEnumerator.get())
			{
				return end();
			}
			THROW_IF_FAILED(m_wbemEnumerator->Reset());
			return { m_wbemServices, m_wbemEnumerator };
		}

		iterator end() const noexcept
		{
			return iterator(m_wbemServices);
		}

		iterator cbegin() const
		{
			if (nullptr == m_wbemEnumerator.get())
			{
				return cend();
			}
			THROW_IF_FAILED(m_wbemEnumerator->Reset());
			return { m_wbemServices, m_wbemEnumerator };
		}

		iterator cend() const noexcept
		{
			return iterator(m_wbemServices);
		}

	private:
		ctWmiService m_wbemServices;
		// Marking wbemEnumerator mutable to allow for const correctness of begin() and end()
		// specifically, invoking Reset() is an implementation detail and should not affect external contracts
		mutable wil::com_ptr<IEnumWbemClassObject> m_wbemEnumerator;
	};
} // namespace ctl
