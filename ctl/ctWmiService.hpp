// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include <stdexcept>

#include <Windows.h>
#include <WbemIdl.h>

#include <wil/stl.h>
#include <wil/com.h>
#include <wil/resource.h>

namespace ctl
{
	// Callers must instantiate a ctWmiService instance in order to use any of the ctWmi* classes
	// This class tracks the WMI initialization of the IWbemLocator and IWbemService interfaces
	//   which maintain a connection to the specified WMI Service through which WMI calls are made
	class ctWmiService
	{
	public:
		// CoInitializeSecurity is not called by the ctWmi* classes. This security
		//   policy should be defined by the code consuming these libraries, as these
		//   libraries cannot assume the security context to apply to the process.
		explicit ctWmiService(_In_ PCWSTR path)
		{
			m_wbemLocator = wil::CoCreateInstance<WbemLocator, IWbemLocator>();

			THROW_IF_FAILED(m_wbemLocator->ConnectServer(
				wil::make_bstr(path).get(), // Object path of WMI namespace
				nullptr, // Username. NULL = current user
				nullptr, // User password. NULL = current
				nullptr, // Locale. NULL indicates current
				0, // Security flags.
				nullptr, // Authority (e.g. Kerberos)
				nullptr, // Context object 
				m_wbemServices.put())); // receive pointer to IWbemServices proxy

			THROW_IF_FAILED(CoSetProxyBlanket(
				m_wbemServices.get(), // Indicates the proxy to set
				RPC_C_AUTHN_WINNT, // RPC_C_AUTHN_xxx
				RPC_C_AUTHZ_NONE, // RPC_C_AUTHZ_xxx
				nullptr, // Server principal name 
				RPC_C_AUTHN_LEVEL_CALL, // RPC_C_AUTHN_LEVEL_xxx 
				RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
				nullptr, // client identity
				EOAC_NONE)); // proxy capabilities 
		}

		~ctWmiService() = default;
		ctWmiService(const ctWmiService& service) noexcept = default;
		ctWmiService& operator=(const ctWmiService& service) noexcept = default;
		ctWmiService(ctWmiService&& rhs) noexcept = default;
		ctWmiService& operator=(ctWmiService&& rhs) noexcept = default;

		IWbemServices* operator->() noexcept
		{
			return m_wbemServices.get();
		}

		const IWbemServices* operator->() const noexcept
		{
			return m_wbemServices.get();
		}

		bool operator ==(const ctWmiService& service) const noexcept
		{
			return m_wbemLocator == service.m_wbemLocator &&
				m_wbemServices == service.m_wbemServices;
		}

		bool operator !=(const ctWmiService& service) const noexcept
		{
			return !(*this == service);
		}

		IWbemServices* get() noexcept
		{
			return m_wbemServices.get();
		}

		[[nodiscard]] const IWbemServices* get() const noexcept
		{
			return m_wbemServices.get();
		}

		void delete_path(_In_ PCWSTR objPath, const wil::com_ptr<IWbemContext>& context) const
		{
			wil::com_ptr<IWbemCallResult> result;
			THROW_IF_FAILED(m_wbemServices->DeleteInstance(
				wil::make_bstr(objPath).get(),
				WBEM_FLAG_RETURN_IMMEDIATELY,
				context.get(),
				result.addressof()));
			// wait for the call to complete
			HRESULT status{};
			THROW_IF_FAILED(result->GetCallStatus(WBEM_INFINITE, &status));
			THROW_IF_FAILED(status);
		}

		// Deletes the WMI object based off the object path specified in the input
		// The object path takes the form of:
		//    MyClass.MyProperty1='33',MyProperty2='value'
		void delete_path(_In_ PCWSTR objPath) const
		{
			const wil::com_ptr<IWbemContext> nullContext;
			delete_path(objPath, nullContext.get());
		}

	private:
		wil::com_ptr<IWbemLocator> m_wbemLocator{};
		wil::com_ptr<IWbemServices> m_wbemServices{};
	};

	class ctWmiStaticMethod
	{
	public:
		ctWmiStaticMethod(_In_ PCWSTR className, _In_ PCWSTR methodName, ctWmiService wbemService = ctWmiService{ L"ROOT\\StandardCimv2" }) :
			m_wbemService(std::move(wbemService)),
			m_className(wil::make_bstr(className)),
			m_methodName(wil::make_bstr(methodName))
		{
			THROW_IF_FAILED(m_wbemService->GetObjectW(
				m_className.get(),
				0,
				nullptr,
				m_wbemClassObject.put(),
				nullptr));

			THROW_IF_FAILED(m_wbemClassObject->GetMethod(
				m_methodName.get(),
				0,
				m_wbemFunctionObject.put(),
				nullptr));

			THROW_IF_FAILED(m_wbemFunctionObject->SpawnInstance(
				0,
				m_wbemParameterObject.put()));

		}

		HRESULT add_parameter_nothrow(_In_ PCWSTR parameterName, _In_opt_ const VARIANT* value) const noexcept
		{
			RETURN_IF_FAILED(m_wbemParameterObject->Put(
				wil::make_bstr(parameterName).get(),
				0,
				const_cast<VARIANT*>(value),
				0));
			return S_OK;
		}

		void add_parameter(_In_ PCWSTR parameterName, _In_opt_ const VARIANT* value) const
		{
			THROW_IF_FAILED(m_wbemParameterObject->Put(
				wil::make_bstr(parameterName).get(),
				0,
				const_cast<VARIANT*>(value),
				0));
		}

		HRESULT execute_method_nothrow() noexcept
		{
			wil::com_ptr<IWbemCallResult> result;
			RETURN_IF_FAILED(m_wbemService->ExecMethod(
				m_className.get(),
				m_methodName.get(),
				0,
				nullptr,
				m_wbemParameterObject.get(),
				nullptr,
				result.addressof()));

			// wait for the call to complete
			HRESULT status{};
			RETURN_IF_FAILED(result->GetCallStatus(WBEM_INFINITE, &status));
			return status;
		}
		void execute_method()
		{
			THROW_IF_FAILED(execute_method_nothrow());
		}

	private:
		ctWmiService m_wbemService;
		wil::unique_bstr m_className;
		wil::unique_bstr m_methodName;
		wil::com_ptr<IWbemClassObject> m_wbemClassObject{};
		wil::com_ptr<IWbemClassObject> m_wbemFunctionObject{};
		wil::com_ptr<IWbemClassObject> m_wbemParameterObject{};
	};

	// Exposes enumerating properties of a WMI Provider (class object) through an iterator interface.
	class ctWmiEnumerateClassProperties
	{
	public:
		class iterator;

		ctWmiEnumerateClassProperties(ctWmiService service, wil::com_ptr<IWbemClassObject> classObject) noexcept :
			m_wbemServices(std::move(service)),
			m_wbemClass(std::move(classObject))
		{
		}

		ctWmiEnumerateClassProperties(ctWmiService service, _In_ PCWSTR className) :
			m_wbemServices(std::move(service))
		{
			THROW_IF_FAILED(m_wbemServices->GetObjectW(
				wil::make_bstr(className).get(),
				0,
				nullptr,
				m_wbemClass.put(),
				nullptr));
		}

		ctWmiEnumerateClassProperties(ctWmiService service, _In_ BSTR className) :
			m_wbemServices(std::move(service))
		{
			THROW_IF_FAILED(m_wbemServices->GetObjectW(
				className,
				0,
				nullptr,
				m_wbemClass.put(),
				nullptr));
		}

		[[nodiscard]] iterator begin(const bool nonSystemPropertiesOnly = true) const
		{
			return { m_wbemClass, nonSystemPropertiesOnly };
		}

		[[nodiscard]] static iterator end() noexcept
		{
			return {};
		}

		// A forward iterator to enable forward-traversing instances of the queried WMI provider

		class iterator
		{
			const uint32_t m_endIteratorIndex = ULONG_MAX;

			wil::com_ptr<IWbemClassObject> m_wbemClassObject{};
			wil::shared_bstr m_propertyName{};
			CIMTYPE m_propertyType = 0;
			uint32_t m_index = m_endIteratorIndex;

		public:
			// Iterator requires the caller's IWbemServices interface and class name
			iterator() noexcept = default;

			iterator(wil::com_ptr<IWbemClassObject> classObject, bool nonSystemPropertiesOnly) :
				m_wbemClassObject(std::move(classObject)), m_index(0)
			{
				THROW_IF_FAILED(m_wbemClassObject->BeginEnumeration(nonSystemPropertiesOnly ? WBEM_FLAG_NONSYSTEM_ONLY : 0));
				increment();
			}

			~iterator() noexcept = default;
			iterator(const iterator&) noexcept = default;
			iterator(iterator&&) noexcept = default;

			iterator& operator =(const iterator&) noexcept = delete;
			iterator& operator =(iterator&&) noexcept = delete;

			void swap(_Inout_ iterator& rhs) noexcept
			{
				using std::swap;
				swap(m_index, rhs.m_index);
				swap(m_wbemClassObject, rhs.m_wbemClassObject);
				swap(m_propertyName, rhs.m_propertyName);
				swap(m_propertyType, rhs.m_propertyType);
			}

			wil::shared_bstr operator*() const
			{
				if (m_index == m_endIteratorIndex)
				{
					throw std::out_of_range("ctWmiProperties::iterator::operator - invalid subscript");
				}
				return m_propertyName;
			}

			const wil::shared_bstr* operator->() const
			{
				if (m_index == m_endIteratorIndex)
				{
					throw std::out_of_range("ctWmiProperties::iterator::operator-> - invalid subscript");
				}
				return &m_propertyName;
			}

			[[nodiscard]] CIMTYPE type() const
			{
				if (m_index == m_endIteratorIndex)
				{
					throw std::out_of_range("ctWmiProperties::iterator::type - invalid subscript");
				}
				return m_propertyType;
			}

			bool operator==(const iterator& iter) const noexcept
			{
				if (m_index != m_endIteratorIndex)
				{
					return m_index == iter.m_index &&
						m_wbemClassObject == iter.m_wbemClassObject;
				}
				return m_index == iter.m_index;
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
				iterator temp(*this);
				increment();
				return temp;
			}

			iterator& operator+=(uint32_t _inc)
			{
				for (auto loop = 0ul; loop < _inc; ++loop)
				{
					increment();
					if (m_index == m_endIteratorIndex)
					{
						throw std::out_of_range("ctWmiProperties::iterator::operator+= - invalid subscript");
					}
				}
				return *this;
			}

			// iterator_traits (allows <algorithm> functions to be used)
			using iterator_category = std::forward_iterator_tag;
			using value_type = wil::shared_bstr;
			using difference_type = int;
			using pointer = BSTR;
			using reference = wil::shared_bstr&;

		private:
			void increment()
			{
				if (m_index == m_endIteratorIndex)
				{
					throw std::out_of_range("ctWmiProperties::iterator - cannot increment: at the end");
				}

				CIMTYPE nextCimType{};
				wil::shared_bstr nextName;
				const auto hr = m_wbemClassObject->Next(
					0,
					nextName.addressof(),
					nullptr,
					&nextCimType,
					nullptr);
				switch (hr)
				{
				case WBEM_S_NO_ERROR:
				{
					// update the instance members
					++m_index;
					using std::swap;
					swap(m_propertyName, nextName);
					swap(m_propertyType, nextCimType);
					break;
				}

				case WBEM_S_NO_MORE_DATA:
				{
					// at the end...
					m_index = m_endIteratorIndex;
					m_propertyName.reset();
					m_propertyType = 0;
					break;
				}

				default: THROW_IF_FAILED(hr);
				}
			}
		};

	private:
		ctWmiService m_wbemServices;
		wil::com_ptr<IWbemClassObject> m_wbemClass{};
	};
} // namespace ctl
