/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <cstdlib>
#include <cwchar>
#include <string>
#include <exception>
// os headers
#include <Windows.h>

namespace ctl
{
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //  ctException
    //
    //  Derived from the base class of exception.
    //
    //  A class to provide deeper exception details including:
    //    - a wide-char version of exception::what()
    //    - an optional unsigned long variable to store a numeric error code
    //  - an optional wide-char string for the location of the exception throwing
    //  - a method to translate Win32 error codes to a wide-char string
    //
    //  All methods are specified noexcept - no method can fail
    //  - should alloc failures occur, an empty string is returned the the caller
    //
    //  ** Note on methods returning pointers to const buffers:
    //  Just as with the wstring::c_str() and string::c_str() methods, as well as the
    //    virtual method exception::what() that class this overrides:
    //    The semantics of the pointers returned:
    //      - must remain const (modifying them can and will corrupt the internals of the object)
    //      - only survive the lifetime of the object
    //  Where the semantics differ from the wstring::c_str() and string::c_str() methods
    //    is that once the const pointer is created, it is guaranteed not to change 
    //    for the lifetime of the ctException object.
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    class ctException : public std::exception
    {
    public:
        ctException() noexcept;
        explicit ctException(const std::exception& e) noexcept;
        explicit ctException(unsigned long _ulCode) noexcept;
        explicit ctException(_In_ PCSTR _szMessage, bool _bMessageCopy = true) noexcept;
        explicit ctException(PCWSTR _wszMessage, bool _bMessageCopy = true) noexcept;
        explicit ctException(const std::wstring& _wsMessage) noexcept;
        explicit ctException(const std::string& _sMessage) noexcept;

        explicit ctException(unsigned long _ulCode, PCWSTR _wszMessage, bool _bMessageCopy = true) noexcept;
        explicit ctException(unsigned long _ulCode, PCWSTR _wszMessage, PCWSTR _wszLocation, bool _bBothStringCopy = true) noexcept;
        explicit ctException(unsigned long _ulCode, _In_ PCSTR _szMessage, bool _bMessageCopy = true) noexcept;
        explicit ctException(unsigned long _ulCode, const std::wstring& _wsMessage) noexcept;
        explicit ctException(unsigned long _ulCode, const std::string& _sMessage) noexcept;

        // ReSharper disable once CppHidingFunction
        virtual ctException& operator=(const std::exception& e) noexcept;

        ctException(const ctException& e) noexcept;
        ctException& operator=(const ctException& e) noexcept;
        ctException(ctException&& e) noexcept;
        ctException& operator=(ctException&& e) noexcept;

        virtual ~ctException() noexcept;

        // public accessors
        unsigned long why() const noexcept;
        const char* what() const noexcept override;
        const wchar_t* what_w() const noexcept;
        const wchar_t* where_w() const noexcept;

        const wchar_t* translation_w() const noexcept;

        // no-fail operators
        void reset() noexcept;

    private:
        // private functions to alloc and copy string buffers
        char* calloc_and_copy_s(_In_ PCSTR tSrc) const noexcept;
        wchar_t* calloc_and_copy_w(PCWSTR tSrc) const noexcept;

        // not making swap() public since the base class does not implement swap()
        // - and that risks getting our objects out of sync
        // - using this internally in a controlled manner with temporaries
        void swap(ctException& e) noexcept;

        // private multi-byte/wide-char translators
        void wszMessageToszMessage() noexcept;
        void szMessageTowszMessage() noexcept;

        // private member variables
        const char* szMessage = nullptr;
        const wchar_t* wszMessage = nullptr;
        const wchar_t* wszLocation = nullptr;
        mutable wchar_t* wszTranslation = nullptr;
        unsigned long ulCode = 0;
        bool bMessageCopy_s = false;
        bool bMessageCopy_w = false;
        bool bLocationCopy_w = false;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // Constructors
    //
    // - all provide a best attempt at preserving the original error strings
    // - allows char* strings for the "message" to maintain compatibility with exception::what()
    // - allows for an optional bool value to be passed to specify whether to make a copy of the string
    //   - if no copy is made, the pointer won't be deleted on destruction
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline ctException::ctException() noexcept :
        std::exception("") // initialize the base c'tor
    {
    }

    inline ctException::ctException(unsigned long _ulCode) noexcept :
        std::exception(""), // initialize the base c'tor
        ulCode(_ulCode) // initializing with args
    {
    }

    inline ctException::ctException(_In_ PCSTR _szMessage, bool _bMessageCopy) noexcept :
        std::exception(""), // initialize the base c'tor
        szMessage(_szMessage), // initializing with args
        bMessageCopy_s(_bMessageCopy) // initializing with args
    {
        if (bMessageCopy_s)
        {
            szMessage = calloc_and_copy_s(_szMessage);
        }
        // build the corresponding wchar_t string
        szMessageTowszMessage();
    }

    inline ctException::ctException(PCWSTR _wszMessage, bool _bMessageCopy) noexcept :
        std::exception(""), // initialize the base c'tor
        wszMessage(_wszMessage), // initializing with args
        bMessageCopy_w(_bMessageCopy) // initializing with args
    {
        if (bMessageCopy_w)
        {
            wszMessage = calloc_and_copy_w(_wszMessage);
        }
        // build the corresponding char string
        wszMessageToszMessage();
    }

    inline ctException::ctException(const std::wstring& _wsMessage) noexcept :
        std::exception("") // initialize the base c'tor
    {
        wszMessage = calloc_and_copy_w(_wsMessage.c_str());
        bMessageCopy_w = true;
        // build the corresponding char string
        wszMessageToszMessage();
    }

    inline ctException::ctException(const std::string& _sMessage) noexcept :
        std::exception("") // initialize the base c'tor
    {
        szMessage = calloc_and_copy_s(_sMessage.c_str());
        bMessageCopy_s = true;
        // build the corresponding wchar_t string
        szMessageTowszMessage();
    }

    inline ctException::ctException(unsigned long _ulCode, PCWSTR _wszMessage, bool _bMessageCopy) noexcept :
        std::exception(""), // initialize the base c'tor
        wszMessage(_wszMessage), // initializing with args
        ulCode(_ulCode), // initializing with args
        bMessageCopy_w(_bMessageCopy) // initializing with args
    {
        if (bMessageCopy_w)
        {
            wszMessage = calloc_and_copy_w(_wszMessage);
        }
        // build the corresponding char string
        wszMessageToszMessage();
    }

    inline ctException::ctException(unsigned long _ulCode, PCWSTR _wszMessage, PCWSTR _wszLocation,
        bool _bBothStringCopy) noexcept :
        std::exception(""), // initialize the base c'tor
        wszMessage(_wszMessage),
        wszLocation(_wszLocation), // initializing with args
        ulCode(_ulCode), // initializing with args
        bMessageCopy_w(_bBothStringCopy), // initializing with args
        bLocationCopy_w(_bBothStringCopy) // initializing with args
    {
        if (_bBothStringCopy)
        {
            wszMessage = calloc_and_copy_w(_wszMessage);
            wszLocation = calloc_and_copy_w(_wszLocation);
        }
        // build the corresponding char string
        wszMessageToszMessage();
    }

    inline ctException::ctException(unsigned long _ulCode, _In_ PCSTR _szMessage, bool _bMessageCopy) noexcept :
        std::exception(_szMessage), // initialize the base c'tor
        szMessage(_szMessage), // initializing with args
        ulCode(_ulCode), // initializing with args
        bMessageCopy_s(_bMessageCopy) // initializing with args
    {
        if (bMessageCopy_s)
        {
            szMessage = calloc_and_copy_s(_szMessage);
        }
        // build the corresponding wchar_t string
        szMessageTowszMessage();
    }

    inline ctException::ctException(unsigned long _ulCode, const std::wstring& _wsMessage) noexcept :
        std::exception(""), // initialize the base c'tor
        ulCode(_ulCode) // initializing with args
    {
        wszMessage = calloc_and_copy_w(_wsMessage.c_str());
        bMessageCopy_w = true;
        // build the corresponding char string
        wszMessageToszMessage();
    }

    inline ctException::ctException(unsigned long _ulCode, const std::string& _sMessage) noexcept :
        std::exception(""), // initialize the base c'tor
        ulCode(_ulCode) // initializing with args
    {
        szMessage = calloc_and_copy_s(_sMessage.c_str());
        bMessageCopy_s = true;
        // build the corresponding wchar_t string
        szMessageTowszMessage();
    }

    inline ctException::ctException(const std::exception& e) noexcept :
        std::exception(e) // initialize the base c'tor
    {
        szMessage = calloc_and_copy_s(e.what());
        bMessageCopy_s = true;
        // build the corresponding wchar_t string
        szMessageTowszMessage();
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // Copy constructor
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline ctException::ctException(const ctException& e) noexcept :
        std::exception(""), // initialize the base c'tor
        szMessage(e.szMessage),
        wszMessage(e.wszMessage),
        wszLocation(e.wszLocation),
        ulCode(e.ulCode), // don't copy this buffer allocated by FormatString
        bMessageCopy_s(e.bMessageCopy_s),
        bMessageCopy_w(e.bMessageCopy_w),
        bLocationCopy_w(e.bLocationCopy_w)
    {
        if (bMessageCopy_s)
        {
            szMessage = calloc_and_copy_s(e.szMessage);
        }

        if (bMessageCopy_w)
        {
            wszMessage = calloc_and_copy_w(e.wszMessage);
        }

        if (bLocationCopy_w)
        {
            wszLocation = calloc_and_copy_w(e.wszLocation);
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // operator= assignment
    // - safe even if (this == &e)
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline ctException& ctException::operator=(const std::exception& e) noexcept
    {
        ctException ctTemp(e);
        this->swap(ctTemp);
        return *this;
    }

    inline ctException& ctException::operator=(const ctException& e) noexcept
    {
        // ReSharper disable once CppUseAuto
        ctException ctTemp(e);
        this->swap(ctTemp);
        return *this;
    }

    inline ctException::ctException(ctException&& e) noexcept
        : szMessage(e.szMessage),
        wszMessage(e.wszMessage),
        wszLocation(e.wszLocation),
        wszTranslation(e.wszTranslation),
        ulCode(e.ulCode),
        bMessageCopy_s(e.bMessageCopy_s),
        bMessageCopy_w(e.bMessageCopy_w),
        bLocationCopy_w(e.bLocationCopy_w)
    {
        // tell the [in] moved from object not to free the raw pointers we just took ownership over
        e.bMessageCopy_s = false;
        e.bMessageCopy_w = false;
        e.bLocationCopy_w = false;
        e.wszTranslation = nullptr;
    }

    inline ctException& ctException::operator=(ctException&& e) noexcept
    {
        szMessage = e.szMessage;
        wszMessage = e.wszMessage;
        wszLocation = e.wszLocation;
        wszTranslation = e.wszTranslation;
        ulCode = e.ulCode;
        bMessageCopy_s = e.bMessageCopy_s;
        bMessageCopy_w = e.bMessageCopy_w;
        bLocationCopy_w = e.bLocationCopy_w;
        // tell the [in] moved from object not to free the raw pointers we just took ownership over
        e.bMessageCopy_s = false;
        e.bMessageCopy_w = false;
        e.bLocationCopy_w = false;
        e.wszTranslation = nullptr;
        return *this;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // Destructor
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline ctException::~ctException() noexcept
    {
        this->reset();
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // reset()
    // - no-fail operation to clear all members
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline void ctException::reset() noexcept
    {
        if (this->bMessageCopy_s) free(const_cast<void*>(static_cast<const void*>(this->szMessage)));
        if (this->bMessageCopy_w) free(const_cast<void*>(static_cast<const void*>(this->wszMessage)));
        if (this->bLocationCopy_w) free(const_cast<void*>(static_cast<const void*>(this->wszLocation)));
        free(this->wszTranslation);

        this->ulCode = 0;
        this->szMessage = nullptr;
        this->wszMessage = nullptr;
        this->wszLocation = nullptr;
        this->wszTranslation = nullptr;
        this->bMessageCopy_s = false;
        this->bMessageCopy_w = false;
        this->bLocationCopy_w = false;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // why()
    // - returns the unsigned long integer error code
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline unsigned long ctException::why() const noexcept
    {
        return ulCode;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // what()
    // - returns the char* member for the reason for the failure
    // - virtual function called if called polymorphically from an std::exception object
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline const char* ctException::what() const noexcept
    {
        return this->szMessage != nullptr ? this->szMessage : "";
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // what_w()
    // - returns the wchar_t* member for the reason for the failure
    // - the "wide" version of std::exception::what()
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline const wchar_t* ctException::what_w() const noexcept
    {
        return this->wszMessage != nullptr ? this->wszMessage : L"";
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // where_w()
    // - returns the wchar_t* member for where the failure occured
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline const wchar_t* ctException::where_w() const noexcept
    {
        return this->wszLocation != nullptr ? this->wszLocation : L"";
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // translation_w()
    // - returns the wchar_t* member for the system translation of the error from this->why()
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline const wchar_t* ctException::translation_w() const noexcept
    {
        // If the translation has already been performed, return the existing value.
        if (this->wszTranslation)
        {
            return this->wszTranslation;
        }

        constexpr DWORD cchBuffer = 1024;
        auto* wszBuffer = static_cast<wchar_t*>(calloc(cchBuffer, sizeof(wchar_t)));
        if (wszBuffer)
        {
            // We carefully avoid using the FORMAT_MESSAGE_ALLOCATE_BUFFER flag.
            // It triggers a use of the LocalAlloc() function. LocalAlloc() and LocalFree() are in an API set that is obsolete.
            constexpr DWORD DWFLAGS =
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS |
                FORMAT_MESSAGE_MAX_WIDTH_MASK;

            const auto dwReturn = FormatMessageW(
                DWFLAGS,
                nullptr, // just search the system
                this->ulCode,
                0, // allow for proper MUI language fallback
                wszBuffer,
                cchBuffer,
                nullptr
            );
            if (0 == dwReturn)
            {
                // Free the temporary buffer here, as it won't be assigned to wszTranslation to be freed later.
                free(wszBuffer);
            }
            else
            {
                this->wszTranslation = wszBuffer;
            }
        }

        return this->wszTranslation ? this->wszTranslation : L"";
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // wszMessageToszMessage()
    // - converts the wide-char message string to the multi-byte char string
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline void ctException::wszMessageToszMessage() noexcept
    {
        if (!this->wszMessage)
        {
            return;
        }

        auto iResult = WideCharToMultiByte(
            CP_ACP,
            WC_NO_BEST_FIT_CHARS,
            this->wszMessage,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr
        );
        if (iResult != 0)
        {
            auto* temp_szMessage = static_cast<char*>(calloc(iResult, sizeof(char)));
            if (temp_szMessage != nullptr)
            {
                iResult = WideCharToMultiByte(
                    CP_ACP,
                    WC_NO_BEST_FIT_CHARS,
                    this->wszMessage,
                    -1,
                    temp_szMessage,
                    iResult,
                    nullptr,
                    nullptr
                );
                if (0 == iResult)
                {
                    // failed to populate the buffer, so should free it
                    free(temp_szMessage);
                }
                else
                {
                    this->szMessage = temp_szMessage;
                    this->bMessageCopy_s = true;
                }
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // szMessageTowszMessage()
    // - converts the multi-byte char message string to the wide-char string
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline void ctException::szMessageTowszMessage() noexcept
    {
        if (!this->szMessage)
        {
            return;
        }

        auto iResult = MultiByteToWideChar(CP_ACP, 0, this->szMessage, -1, nullptr, 0);
        if (iResult != 0)
        {
            auto* temp_wszMessage = static_cast<wchar_t*>(calloc(iResult, sizeof(wchar_t)));
            if (temp_wszMessage != nullptr)
            {
                iResult = MultiByteToWideChar(CP_ACP, 0, this->szMessage, -1, temp_wszMessage, iResult);
                if (0 == iResult)
                {
                    // failed to populate the buffer, so should free it
                    free(temp_wszMessage);
                }
                else
                {
                    this->wszMessage = temp_wszMessage;
                    this->bMessageCopy_w = true;
                }
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // calloc_and_copy_s(char* )
    //
    // - allocates a buffer and copies the multi-byte char string
    // - returns the pointer to the allocated buffer
    //        - or nullptr if failed to allocate
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline char* ctException::calloc_and_copy_s(_In_ PCSTR tSrc) const noexcept
    {
        // no need to copy a nullptr ptr
        if (!tSrc)
        {
            return nullptr;
        }

        const auto tSize = strlen(tSrc) + 1;
        auto tDest = static_cast<char*>(calloc(tSize, sizeof(char)));
        if (tDest != nullptr)
        {
            const auto err = strcpy_s(tDest, tSize, tSrc);
            if (err != 0)
            {
                free(tDest);
                tDest = nullptr;
            }
        }

        return tDest;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // calloc_and_copy_w(wchar_t* )
    //
    // - allocates a buffer and copies the wide-char string
    // - returns the pointer to the allocated buffer
    //        - or nullptr if failed to allocate
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline wchar_t* ctException::calloc_and_copy_w(PCWSTR tSrc) const noexcept
    {
        // no need to copy a nullptr ptr
        if (!tSrc)
        {
            return nullptr;
        }

        const auto tSize = wcslen(tSrc) + 1;
        auto tDest = static_cast<wchar_t*>(calloc(tSize, sizeof(wchar_t)));
        if (tDest != nullptr)
        {
            const auto err = wcscpy_s(tDest, tSize, tSrc);
            if (err != 0)
            {
                free(tDest);
                tDest = nullptr;
            }
        }

        return tDest;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // swap()
    // - no-fail swap operation to swap 2 ctException objects
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    inline void ctException::swap(ctException& e) noexcept
    {
        using std::swap;
        swap(this->ulCode, e.ulCode);
        swap(this->szMessage, e.szMessage);
        swap(this->wszMessage, e.wszMessage);
        swap(this->wszLocation, e.wszLocation);
        swap(this->wszTranslation, e.wszTranslation);
        swap(this->bMessageCopy_s, e.bMessageCopy_s);
        swap(this->bMessageCopy_w, e.bMessageCopy_w);
        swap(this->bLocationCopy_w, e.bLocationCopy_w);
    }

#ifdef _CPPRTTI
    inline int ctErrorCode(const std::exception& _exception) noexcept
    {
        const auto* ctex = dynamic_cast<const ctException*>(&_exception);
        if (!ctex)
        {
            return ERROR_OUTOFMEMORY;
        }

        return ctex->why() == 0 ? ERROR_OUTOFMEMORY : static_cast<int>(ctex->why());
    }
#else
    inline int ctErrorCode(const std::exception&) noexcept
    {
        return ERROR_OUTOFMEMORY;
    }
#endif

} // namespace ctl
