/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// os headers
#include <Windows.h>
#include <WbemIdl.h>
// wil headers
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
            nullptr, // User name. NULL = current user
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

    const IWbemServices* operator ->() const noexcept
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
        HRESULT status;
        THROW_IF_FAILED(result->GetCallStatus(WBEM_INFINITE, &status));
        THROW_IF_FAILED(status);
    }

    // Deletes the WMI object based off the object path specified in the input
    // The object path takes the form of:
    //    MyClass.MyProperty1='33',MyProperty2='value'
    void delete_path(_In_ PCWSTR objPath) const
    {
        const wil::com_ptr<IWbemContext> nullcontext;
        delete_path(objPath, nullcontext.get());
    }

private:
    wil::com_ptr<IWbemLocator> m_wbemLocator{};
    wil::com_ptr<IWbemServices> m_wbemServices{};
};
} // namespace ctl
