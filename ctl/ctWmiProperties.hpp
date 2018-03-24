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
	/// class ctWmiProperties
	///
	/// Exposes enumerating properties of a WMI Provider through an iterator interface.
	///
	////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////
	class ctWmiProperties
	{
	private:
		ctWmiService wbemServices;
		ctComPtr<IWbemClassObject> wbemClass;

	public:
		//
		// forward declare iterator class
		//
		class iterator;


		ctWmiProperties(ctWmiService _wbemServices, ctComPtr<IWbemClassObject> _wbemClass) :
			wbemServices(std::move(_wbemServices)), wbemClass(std::move(_wbemClass))
		{
		}

		ctWmiProperties(ctWmiService _wbemServices, LPCWSTR _className) :
			wbemServices(std::move(_wbemServices))
		{
			const auto hr = this->wbemServices->GetObjectW(
				ctComBstr(_className).get(),
				0,
				nullptr,
				this->wbemClass.get_addr_of(),
				nullptr);
			if (FAILED(hr))
			{
				throw ctWmiException(hr, L"IWbemServices::GetObject", L"ctWmiProperties::ctWmiProperties", false);
			}
		}

		ctWmiProperties(ctWmiService _wbemServices, const ctComBstr& _className)
			: wbemServices(std::move(_wbemServices))
		{
			const auto hr = this->wbemServices->GetObjectW(
				_className.get(),
				0,
				nullptr,
				this->wbemClass.get_addr_of(),
				nullptr);
			if (FAILED(hr))
			{
				throw ctWmiException(hr, L"IWbemServices::GetObject", L"ctWmiProperties::ctWmiProperties", false);
			}
		}


		////////////////////////////////////////////////////////////////////////////////
		///
		/// begin() and end() to return property iterators
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
		iterator begin(bool _fNonSystemPropertiesOnly = true) const
		{
			return iterator(this->wbemClass, _fNonSystemPropertiesOnly);
		}

		iterator end() const NOEXCEPT
		{
			return iterator();
		}

		////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////
		///
		/// class ctWmiProperties::iterator
		///
		/// A forward iterator class type to enable forward-traversing instances of the queried
		///  WMI provider
		///
		////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////
		class iterator
		{
		private:
			static const unsigned long END_ITERATOR_INDEX = 0xffffffff;

			ctComPtr<IWbemClassObject> wbemClassObj{};
			ctComBstr propName{};
			CIMTYPE propType = 0;
			DWORD dwIndex = END_ITERATOR_INDEX;

		public:
			////////////////////////////////////////////////////////////////////////////////
			///
			/// c'tor and d'tor
			/// - default c'tor is an 'end' iterator
			/// - traversal requires the callers IWbemServices interface and class name
			///
			////////////////////////////////////////////////////////////////////////////////
			iterator() NOEXCEPT = default;

			iterator(ctComPtr<IWbemClassObject> _classObj, bool _fNonSystemPropertiesOnly) :
				wbemClassObj(std::move(_classObj)), dwIndex(0)
			{
				const auto hr = wbemClassObj->BeginEnumeration((_fNonSystemPropertiesOnly) ? WBEM_FLAG_NONSYSTEM_ONLY : 0);
				if (FAILED(hr))
				{
					throw ctWmiException(hr, wbemClassObj.get(), L"IWbemClassObject::BeginEnumeration",
					                     L"ctWmiProperties::iterator::iterator", false);
				}

				increment();
			}

			~iterator() NOEXCEPT = default;

			iterator(const iterator&) NOEXCEPT = default;
			iterator& operator =(const iterator&) NOEXCEPT = default;
			iterator(iterator&&) NOEXCEPT = default;
			iterator& operator =(iterator&&) NOEXCEPT = default;


			void swap(_Inout_ iterator& _i) NOEXCEPT
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
					throw std::out_of_range("ctWmiProperties::iterator::operator - invalid subscript");
				}
				return this->propName;
			}

			ctComBstr* operator->()
			{
				if (this->dwIndex == END_ITERATOR_INDEX) {
					throw std::out_of_range("ctWmiProperties::iterator::operator-> - invalid subscript");
				}
				return &(this->propName);
			}

			CIMTYPE type() const
			{
				if (this->dwIndex == END_ITERATOR_INDEX) {
					throw std::out_of_range("ctWmiProperties::iterator::type - invalid subscript");
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
			bool operator==(const iterator& _iter) const NOEXCEPT
			{
				if (this->dwIndex != END_ITERATOR_INDEX) {
					return ((this->dwIndex == _iter.dwIndex) &&
						(this->wbemClassObj == _iter.wbemClassObj));
				}
				return (this->dwIndex == _iter.dwIndex);
			}

			bool operator!=(const iterator& _iter) const NOEXCEPT
			{
				return !(*this == _iter);
			}

			// preincrement
			iterator& operator++()
			{
				this->increment();
				return *this;
			}

			// postincrement
			iterator operator++(int)
			{
				iterator temp(*this);
				this->increment();
				return temp;
			}

			// increment by integer
			iterator& operator+=(DWORD _inc)
			{
				for (unsigned loop = 0; loop < _inc; ++loop) {
					this->increment();
					if (this->dwIndex == END_ITERATOR_INDEX) {
						throw std::out_of_range("ctWmiProperties::iterator::operator+= - invalid subscript");
					}
				}
				return *this;
			}

			////////////////////////////////////////////////////////////////////////////////
			///
			/// iterator_traits
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
					throw std::out_of_range("ctWmiProperties::iterator - cannot increment: at the end");
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
						L"ctWmiProperties::iterator::increment",
						false);
				}
			}
		};
	};
} // namespace ctl
