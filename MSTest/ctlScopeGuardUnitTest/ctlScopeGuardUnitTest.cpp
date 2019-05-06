/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#include <SDKDDKVer.h>
#include "CppUnitTest.h"

#include "ctScopeGuard.hpp"
#include "ctString.hpp"
#include "../../ctl/ctVersionConversion.hpp"
#include "../../ctl/ctScopeGuard.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace ctsUnitTest
{		
    TEST_CLASS(ctlScopeGuardUnitTest)
    {
    public:
        
        TEST_METHOD(InstanceFunctor)
        {
            Logger::WriteMessage(L"InstanceFunctor - direct instantiation");
            int counter = 0;
            {
                ctlScopeGuard(test, { ++counter; });
                Assert::AreEqual(0, counter);
            }
            Assert::AreEqual(1, counter);
        }

        TEST_METHOD(StaticFunctor)
        {
            Logger::WriteMessage(L"StaticFunctor - direct instantiation");
            static int counter = 0;
            {
                static const auto functor([] () { ++counter; });
                ctl::ctScopeGuardT<decltype(functor)> test(functor);
                Assert::AreEqual(0, counter);
            }
            Assert::AreEqual(1, counter);
        }

        TEST_METHOD(OnException)
        {
            Logger::WriteMessage(L"OnException - direct instantiation");
            int counter = 0;

            try {
                ctlScopeGuard(test, { ++counter; });
                Assert::AreEqual(0, counter);
                throw std::bad_alloc();
            }
            catch (const std::exception&) {
                // stack has unwould before the catch block starts
                Assert::AreEqual(1, counter);
            }
            Assert::AreEqual(1, counter);
        }

        TEST_METHOD(InstanceFunctorDismiss)
        {
            Logger::WriteMessage(L"InstanceFunctorDismiss - direct instantiation");
            int counter = 0;
            {
                ctlScopeGuard(test, { ++counter; });
                Assert::AreEqual(0, counter);
                test.dismiss();
            }
            Assert::AreEqual(0, counter);
        }

        TEST_METHOD(StaticFunctorDismiss)
        {
            Logger::WriteMessage(L"StaticFunctorDismiss - direct instantiation");
            static int counter = 0;
            {
                static const auto functor1([] () { ++counter; });
                ctl::ctScopeGuardT<decltype(functor1)> test(functor1);
                Assert::AreEqual(0, counter);
                test.dismiss();
            }
            Assert::AreEqual(0, counter);
        }

        TEST_METHOD(OnExceptionDismiss)
        {
            Logger::WriteMessage(L"OnExceptionDismiss - direct instantiation");
            int counter = 0;
            try {
                ctlScopeGuard(test, { ++counter; });
                Assert::AreEqual(0, counter);
                test.dismiss();
            }
            catch (const std::exception&) {
                // stack has unwould before the catch block starts
                Assert::AreEqual(0, counter);
            }
            Assert::AreEqual(0, counter);
        }

        TEST_METHOD(ObjectFunctor)
        {
            struct TestStruct {
                int counter;
                TestStruct() noexcept :
                    counter(0)
                {
                }

                void revert_increment_on_error1()
                {
                    ++counter;
                    Assert::AreEqual(1, counter);
                    ctlScopeGuard(test, { --counter; });
                    throw std::bad_alloc();
                }
            };

            Logger::WriteMessage(L"ObjectFunctor - direct instantiation");
            TestStruct testcase1;
            try {
                Assert::AreEqual(0, testcase1.counter);
                testcase1.revert_increment_on_error1();
            }
            catch (const std::exception&) {
                Assert::AreEqual(0, testcase1.counter);
            }
            Assert::AreEqual(0, testcase1.counter);
        }
    };
}