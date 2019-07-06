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
#include <memory>
#include <utility>
// os headers
#include <windows.h>
#include <Wbemidl.h>
// local headers
#include "ctComInitialize.hpp"
#include "ctWmiService.hpp"
#include "ctWmiInstance.hpp"
#include "ctWmiException.hpp"

namespace ctl
{
	////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////
	///
	/// class ctWmiEnumerate
	///
	/// Exposes enumerating instances of a WMI Provider through an iterator interface.
	///
	////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////
	class ctWmiEnumerate
	{
	public:
		////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////
		///
		/// class ctWmiEnumerate::iterator
		///
		/// A forward iterator class type to enable forward-traversing instances of the queried
		///  WMI provider
		///
		////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////
		class iterator
		{
		public:

			////////////////////////////////////////////////////////////////////////////////
			///
			/// c'tor and d'tor
			/// - default c'tor is an 'end' iterator
			/// - c'tor can take a reference to the parent's WMI Enum interface (to traverse)
			///
			////////////////////////////////////////////////////////////////////////////////
			explicit iterator(ctWmiService _services) noexcept :
				wbemServices(std::move(_services))
			{
			}

			iterator(ctWmiService _services, ctComPtr<IEnumWbemClassObject> _wbemEnumerator) :
				index(0),
				wbemServices(std::move(_services)),
				wbemEnumerator(std::move(_wbemEnumerator))
			{
				this->increment();
			}

			~iterator() noexcept = default;
			iterator(const iterator&) noexcept = default;
			iterator& operator =(const iterator&) noexcept = default;
			iterator(iterator&&) noexcept = default;
			iterator& operator =(iterator&&) noexcept = default;


			void swap(_Inout_ iterator& _i) noexcept
			{
				using std::swap;
				swap(this->index, _i.index);
				swap(this->wbemServices, _i.wbemServices);
				swap(this->wbemEnumerator, _i.wbemEnumerator);
				swap(this->ctInstance, _i.ctInstance);
			}

			unsigned long location() const noexcept
			{
				return this->index;
			}

			////////////////////////////////////////////////////////////////////////////////
			///
			/// accessors:
			/// - dereference operators to access the internal WMI class object
			///
			////////////////////////////////////////////////////////////////////////////////
			ctWmiInstance& operator*() const noexcept
			{
				return *this->ctInstance;
			}

			ctWmiInstance* operator->() const noexcept
			{
				return this->ctInstance.get();
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
			bool operator==(const iterator&) const noexcept;
			bool operator!=(const iterator&) const noexcept;

			iterator& operator++(); // preincrement
			iterator operator++(int); // postincrement
			iterator& operator+=(DWORD); // increment by integer

			////////////////////////////////////////////////////////////////////////////////
			///
			/// iterator_traits
			/// - allows <algorithm> functions to be used
			///
			////////////////////////////////////////////////////////////////////////////////
			typedef std::forward_iterator_tag  iterator_category;
			typedef ctWmiInstance              value_type;
			typedef int                        difference_type;
			typedef ctWmiInstance*             pointer;
			typedef ctWmiInstance&             reference;


		private:
			void increment();

			static const unsigned long END_ITERATOR_INDEX = 0xffffffff;

			unsigned long index = END_ITERATOR_INDEX;
			ctWmiService wbemServices;
			ctComPtr<IEnumWbemClassObject> wbemEnumerator;
			std::shared_ptr<ctWmiInstance> ctInstance;
		};


	public:
		////////////////////////////////////////////////////////////////////////////////
		///
		/// c'tor takes a reference to the initialized ctWmiService interface
		///
		/// Default d'tor, copy c'tor, and copy assignment operators
		//
		////////////////////////////////////////////////////////////////////////////////
		explicit ctWmiEnumerate(ctWmiService _wbemServices) noexcept :
			wbemServices(std::move(_wbemServices))
		{
		}

		////////////////////////////////////////////////////////////////////////////////
		///
		/// query(LPCWSTR)
		///
		/// Allows for executing a WMI query against the WMI service for an enumeration
		/// of WMI objects.
		/// Assumes the query of of the WQL query language.
		///
		/// Will throw a ctWmiException if the WMI query fails
		/// Will throw a std::bad_alloc if fails to low-resources
		///
		////////////////////////////////////////////////////////////////////////////////
		void query(LPCWSTR _query)
		{
			ctComBstr wql(L"WQL");
			ctComBstr query(_query);

			const auto hr = this->wbemServices->ExecQuery(
				wql.get(),
				query.get(),
				WBEM_FLAG_BIDIRECTIONAL,
				nullptr,
				this->wbemEnumerator.get_addr_of());
			if (FAILED(hr)) {
				throw ctWmiException(hr, L"IWbemServices::ExecQuery", L"ctWmiEnumerate::query", false);
			}
		}

		void query(LPCWSTR _query, const ctComPtr<IWbemContext>& _context)
		{
			ctComBstr wql(L"WQL");
			ctComBstr query(_query);

			// forced to const-cast to make this const-correct as COM does not have
			//   an expression for saying a method call is const
			const auto hr = this->wbemServices->ExecQuery(
				wql.get(),
				query.get(),
				WBEM_FLAG_BIDIRECTIONAL,
				const_cast<IWbemContext*>(_context.get()),
				this->wbemEnumerator.get_addr_of());
			if (FAILED(hr)) {
				throw ctWmiException(hr, L"IWbemServices::ExecQuery", L"ctWmiEnumerate::query", false);
			}
		}


		////////////////////////////////////////////////////////////////////////////////
		///
		/// access methods to the child iterator class
		///
		/// begin() - iterator pointing to the first of the contained enumerator
		/// end()   - defined end-iterator for comparison
		///
		/// cbegin() / cend() - const versions of the above
		///
		/// Iterator construction can fail - will throw a ctWmiException
		///
		////////////////////////////////////////////////////////////////////////////////
		iterator begin() const
		{
			if (nullptr == this->wbemEnumerator.get()) {
				return end();
			}

			const auto hr = this->wbemEnumerator->Reset();
			if (FAILED(hr)) {
				throw ctWmiException(hr, L"IEnumWbemClassObject::Reset", L"ctWmiEnumerate::begin", false);
			}

			return iterator(this->wbemServices, this->wbemEnumerator);
		}

		iterator end() const noexcept
		{
			return iterator(this->wbemServices);
		}

		iterator cbegin() const
		{
			if (nullptr == this->wbemEnumerator.get()) {
				return cend();
			}

			const auto hr = this->wbemEnumerator->Reset();
			if (FAILED(hr)) {
				throw ctWmiException(hr, L"IEnumWbemClassObject::Reset", L"ctWmiEnumerate::cbegin", false);
			}

			return iterator(this->wbemServices, this->wbemEnumerator);
		}

		iterator cend() const noexcept
		{
			return iterator(this->wbemServices);
		}

	private:
		ctWmiService wbemServices;
		//
		// Marking wbemEnumerator mutabale to allow for const correctness of begin() and end()
		//   specifically, invoking Reset() is an implementation detail and should not affect external contracts
		//
		mutable ctComPtr<IEnumWbemClassObject> wbemEnumerator;
	};


	////////////////////////////////////////////////////////////////////////////////
	///
	/// Definitions of iterator comparison operators and arithmatic operators
	///
	////////////////////////////////////////////////////////////////////////////////
	inline
	bool ctWmiEnumerate::iterator::operator==(const iterator& _iter) const noexcept
	{
		if (this->index != END_ITERATOR_INDEX) {
			return (
				(this->index == _iter.index) &&
				(this->wbemServices == _iter.wbemServices) &&
				(this->wbemEnumerator == _iter.wbemEnumerator) &&
				(this->ctInstance == _iter.ctInstance));
		}
		return ((this->index == _iter.index) &&
			    (this->wbemServices == _iter.wbemServices));
	}

	inline
	bool ctWmiEnumerate::iterator::operator!=(const iterator& _iter) const noexcept
	{
		return !(*this == _iter);
	}

	// preincrement
	inline
	ctWmiEnumerate::iterator& ctWmiEnumerate::iterator::operator++()
	{
		this->increment();
		return *this;
	}

	// postincrement
	inline
	ctWmiEnumerate::iterator ctWmiEnumerate::iterator::operator++(int)
	{
		auto temp(*this);
		this->increment();
		return temp;
	}

	// increment by integer
	inline
	ctWmiEnumerate::iterator& ctWmiEnumerate::iterator::operator+=(DWORD _inc)
	{
		for (unsigned loop = 0; loop < _inc; ++loop) {
			this->increment();
			if (this->index == END_ITERATOR_INDEX) {
				throw std::out_of_range("ctWmiEnumerate::iterator::operator+= - invalid subscript");
			}
		}
		return *this;
	}

	inline
	void ctWmiEnumerate::iterator::increment()
	{
		if (index == END_ITERATOR_INDEX) {
			throw std::out_of_range("ctWmiEnumerate::iterator::increment at the end");
		}

		ULONG uReturn;
		wil::com_ptr<IWbemClassObject> wbemTarget;
		const auto hr = this->wbemEnumerator->Next(
			WBEM_INFINITE,
			1,
			wbemTarget.put(),
			&uReturn);
		if (FAILED(hr)) {
			throw ctWmiException(hr, L"IEnumWbemClassObject::Next", L"ctWmiEnumerate::iterator::increment", false);
		}

		if (0 == uReturn) {
			// at the end...
			this->index = END_ITERATOR_INDEX;
			this->ctInstance.reset();
		} else {
			++this->index;
			this->ctInstance = std::make_shared<ctWmiInstance>(this->wbemServices, wbemTarget);
		}
	}
} // namespace ctl
