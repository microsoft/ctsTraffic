/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <iterator>
#include <exception>
#include <stdexcept>
#include <utility>
// os headers
#include <windows.h>
#include <Wbemidl.h>
// local headers
#include "ctComInitialize.hpp"
#include "ctWmiException.hpp"
#include "ctWmiService.hpp"
#include "ctVersionConversion.hpp"

namespace ctl
{
	////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////
	///
	/// class ctWmiClassObject
	///
	/// Exposes enumerating properties of a WMI Provider through an property_iterator interface.
	///
	////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////
	class ctWmiClassObject
	{
	private:
		ctWmiService wbemServices;
		ctComPtr<IWbemClassObject> wbemClass;

	public:
		//
		// forward declare iterator classes
		//
		class property_iterator;
		class method_iterator;

		ctWmiClassObject(ctWmiService _wbemServices, ctComPtr<IWbemClassObject> _wbemClass) :
			wbemServices(std::move(_wbemServices)),
			wbemClass(std::move(_wbemClass))
		{
		}

		ctWmiClassObject(ctWmiService _wbemServices, _In_ LPCWSTR _className) :
			wbemServices(std::move(_wbemServices))
		{
			const auto hr = this->wbemServices->GetObject(
				ctComBstr(_className).get(),
				0,
				nullptr,
				this->wbemClass.get_addr_of(),
				nullptr);
			if (FAILED(hr)) {
				throw ctWmiException(hr, L"IWbemServices::GetObject", L"ctWmiClassObject::ctWmiClassObject", false);
			}
		}

		ctWmiClassObject(ctWmiService _wbemServices, const ctComBstr& _className) :
			wbemServices(std::move(_wbemServices))
		{
			const auto hr = this->wbemServices->GetObjectW(
				_className.get(),
				0,
				nullptr,
				this->wbemClass.get_addr_of(),
				nullptr);
			if (FAILED(hr)) {
				throw ctWmiException(hr, L"IWbemServices::GetObject", L"ctWmiClassObject::ctWmiClassObject", false);
			}
		}

		////////////////////////////////////////////////////////////////////////////////
		///
		/// Accessor to retrieve the encapsulated IWbemClassObject
		///
		/// - returns the IWbemClassObject holding the class instance
		///
		////////////////////////////////////////////////////////////////////////////////
		ctComPtr<IWbemClassObject> get_class_object() const NOEXCEPT
		{
			return this->wbemClass;
		}


		////////////////////////////////////////////////////////////////////////////////
		///
		/// begin() and end() to return property property_iterators
		///
		/// begin() arguments:
		///   _wbemServices: a instance of IWbemServices
		///   _className: a string of the class name which to enumerate
		///   _fNonSystemPropertiesOnly: flag to control if should only enumerate
		///       non-system properties
		///
		/// begin() will throw an exception derived from std::exception on error
		/// end() is no-fail / no-throw
		///
		////////////////////////////////////////////////////////////////////////////////
		property_iterator property_begin(bool _fNonSystemPropertiesOnly = true) const
		{
			return property_iterator(this->wbemClass, _fNonSystemPropertiesOnly);
		}

		property_iterator property_end() const NOEXCEPT
		{
			return property_iterator();
		}

		//
		// Not yet implemented
		//
		/// method_iterator method_begin(bool _fLocalMethodsOnly = true)
		/// {
		///     return method_iterator(this->wbemClass, _fLocalMethodsOnly);
		/// }
		/// method_iterator method_end() NOEXCEPT
		/// {
		///     return method_iterator();
		/// }

		////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////
		///
		/// class ctWmiClassObject::property_iterator
		///
		/// A forward property_iterator class type to enable forward-traversing instances of the queried
		///  WMI provider
		///
		////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////
		class property_iterator
		{
		private:
			static const unsigned long END_ITERATOR_INDEX = 0xffffffff;

			ctComPtr<IWbemClassObject> wbemClassObj{};
			ctComBstr propName{};
			CIMTYPE propType = 0;
			DWORD dwIndex = END_ITERATOR_INDEX;

		public:
			property_iterator() NOEXCEPT = default;

			property_iterator(ctComPtr<IWbemClassObject> _classObj, bool _fNonSystemPropertiesOnly) :
				wbemClassObj(std::move(_classObj)),
				dwIndex(0)
			{
				const auto hr = wbemClassObj->BeginEnumeration((_fNonSystemPropertiesOnly) ? WBEM_FLAG_NONSYSTEM_ONLY : 0);
				if (FAILED(hr)) {
					throw ctWmiException(hr, wbemClassObj.get(), L"IWbemClassObject::BeginEnumeration", L"ctWmiClassObject::property_iterator::property_iterator", false);
				}

				increment();
			}

			~property_iterator() NOEXCEPT = default;
			// copy c'tors can throw since it will try to copy the BSTR
			property_iterator(const property_iterator&) = default;
			property_iterator& operator =(const property_iterator&) = default;
			// move c'tors are no-throw
			property_iterator(property_iterator&&) NOEXCEPT = default;
			property_iterator& operator=(property_iterator&&) NOEXCEPT = default;

			void swap(property_iterator& _i) NOEXCEPT
			{
				using std::swap;
				swap(this->dwIndex, _i.dwIndex);
				swap(this->wbemClassObj, _i.wbemClassObj);
				swap(this->propName, _i.propName);
				swap(this->propType, _i.propType);
			}

			////////////////////////////////////////////////////////////////////////////////
			///
			/// accessors:
			/// - dereference operators to access the property name
			/// - explicit type() method to expose its CIM type
			///
			////////////////////////////////////////////////////////////////////////////////
			ctComBstr& operator*()
			{
				if (this->dwIndex == END_ITERATOR_INDEX) {
					throw std::out_of_range("ctWmiClassObject::property_iterator::operator * - invalid subscript");
				}
				return this->propName;
			}

			const ctComBstr& operator*() const
			{
				if (this->dwIndex == END_ITERATOR_INDEX) {
					throw std::out_of_range("ctWmiClassObject::property_iterator::operator * - invalid subscript");
				}
				return this->propName;
			}

			ctComBstr* operator->()
			{
				if (this->dwIndex == END_ITERATOR_INDEX) {
					throw std::out_of_range("ctWmiClassObject::property_iterator::operator-> - invalid subscript");
				}
				return &(this->propName);
			}

			const ctComBstr* operator->() const
			{
				if (this->dwIndex == END_ITERATOR_INDEX) {
					throw std::out_of_range("ctWmiClassObject::property_iterator::operator-> - invalid subscript");
				}
				return &(this->propName);
			}

			CIMTYPE type() const
			{
				if (this->dwIndex == END_ITERATOR_INDEX) {
					throw std::out_of_range("ctWmiClassObject::property_iterator::type - invalid subscript");
				}
				return this->propType;
			}

			////////////////////////////////////////////////////////////////////////////////
			///
			/// comparison and arithmatic operators
			/// 
			/// comparison operators are no-throw/no-fail
			/// arithmatic operators can fail 
			/// - throwing a ctWmiException object capturing the WMI failures
			///
			////////////////////////////////////////////////////////////////////////////////
			bool operator==(const property_iterator& _iter) const NOEXCEPT
			{
				if (this->dwIndex != END_ITERATOR_INDEX) {
					return ((this->dwIndex == _iter.dwIndex) &&
						    (this->wbemClassObj == _iter.wbemClassObj));
				}
				return (this->dwIndex == _iter.dwIndex);
			}

			bool operator!=(const property_iterator& _iter) const NOEXCEPT
			{
				return !(*this == _iter);
			}

			// preincrement
			property_iterator& operator++()
			{
				this->increment();
				return *this;
			}

			// postincrement
			property_iterator operator++(int)
			{
				property_iterator temp(*this);
				this->increment();
				return temp;
			}

			// increment by integer
			property_iterator& operator+=(DWORD _inc)
			{
				for (unsigned loop = 0; loop < _inc; ++loop) {
					this->increment();
					if (this->dwIndex == END_ITERATOR_INDEX) {
						throw std::out_of_range("ctWmiClassObject::property_iterator::operator+= - invalid subscript");
					}
				}
				return *this;
			}

			////////////////////////////////////////////////////////////////////////////////
			///
			/// property_iterator_traits
			/// - allows <algorithm> functions to be used
			///
			////////////////////////////////////////////////////////////////////////////////
			typedef std::forward_iterator_tag  iterator_category;
			typedef ctComBstr                  value_type;
			typedef int                        difference_type;
			typedef ctComBstr*                 pointer;
			typedef ctComBstr&                 reference;

		private:
			void increment()
			{
				if (dwIndex == END_ITERATOR_INDEX) {
					throw std::out_of_range("ctWmiClassObject::property_iterator - cannot increment: at the end");
				}

				CIMTYPE next_cimtype;
				ctComBstr next_name;
				ctComVariant next_value;
				const auto hr = this->wbemClassObj->Next(
					0,
					next_name.get_addr_of(),
					next_value.get(),
					&next_cimtype,
					nullptr);
				switch (hr) {
					case WBEM_S_NO_ERROR: {
						// update the instance members
						++dwIndex;
						using std::swap;
						swap(propName, next_name);
						swap(propType, next_cimtype);
						break;
					}
#pragma warning (suppress : 6221)
					// "Implicit cast between semantically different integer types:  comparing HRESULT to an integer.  Consider using SUCCEEDED or FAILED macros instead."
					// Directly comparing the HRESULT return to WBEM_S_NO_ERROR, even though WBEM did not properly define that constant as an HRESULT
					case WBEM_S_NO_MORE_DATA: {
						// at the end...
						dwIndex = END_ITERATOR_INDEX;
						propName.reset();
						propType = 0;
						break;
					}

				default:
					throw ctWmiException(
						hr,
						this->wbemClassObj.get(),
						L"IEnumWbemClassObject::Next",
						L"ctWmiClassObject::property_iterator::increment",
						false);
				}
			}
		};
	};
} // namespace ctl
