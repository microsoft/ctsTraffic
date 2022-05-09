/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

// ReSharper disable CppInconsistentNaming
#pragma once

// cpp headers
#include <random>
#include <memory>

namespace ctl
{
// Wraps the somewhat unweildy STL random routines for common use cases
//
// This random number generator makes several important assumptions:
//   - Cryptographic-level randomness is unnecessary
//   - Moderately high space usage is okay (an instance takes ~5kb)
//   - Seeding with only an uint32_t's worth of entropy is okay
//
// These assumptions are perfectly okay in most common cases. If any of them
// are invalid, use either the windows cryptographic-quality random generation
// routines or STL <random> directly instead of this class.
//
// This class uses the STL's mersenne twister implementation internally, which means
// that this class requires considerable space (~5kb heap space), but random number
// generation is fast and provides fairly good random distributions (good
// enough for just about anything non-cryptographic)
class ctRandomTwister
{
public:
    using engine_type = std::mt19937;

    // Constructs the generator with an explicitly specified seed.
    // This is usually unnecessary, since the default constructor will seed the generator
    // with an appropriately random seed
    //
    // This allocates a large (5kb) chunk of heap memory and may throw std::bad_alloc
    explicit ctRandomTwister(uint32_t seed);

    // Seeds itself randomly with std::random_device
    ///
    // This allocates a large (5kb) chunk of heap memory and may throw std::bad_alloc
    ctRandomTwister();

    // Generates a new random integer in the range [lowerInclusiveBound, upperInclusiveBound].
    // Each integer in the range is equally likely to be chosen.
    //
    // It's usually best to explicitly specify the template argument to this function - the compiler
    // can surprise you if you let it choose what type to output.
    template <class IntegerT>
    IntegerT uniform_int(IntegerT lowerInclusiveBound, IntegerT upperInclusiveBound);

    // Generates a new random floating-point number in the range [lowerInclusiveBound, upperInclusiveBound].
    //
    // The result is chosen according to a uniformaly random distribution of real numbers, not a uniformly
    // random distribution of those numbers representable as RealTs. That is, even though a double can represent
    // more distinct values in the range [0.0, 1.0] than it can in the range [99.0, 100.0], uniform_real(0.0, 100.0)
    // will return a number in those two ranges equally often.
    template <class RealT>
    RealT uniform_real(RealT lowerInclusiveBound, RealT upperInclusiveBound);

    // Generates a new random floating-point number chosen uniformly at random from the range [0.0, 1.0].
    [[nodiscard]] double uniform_probability() const;

    // Generates a new random double chosen randomly from a normal distribution with the characteristics
    // given by the two parameters (by default, a standard normal distribution).
    [[nodiscard]] double normal_real(double distributionMean = 0.0, double distributionSigma = 1.0) const;

    // Seeds the generator manually.
    void seed(uint32_t seed) const;

    // Enabling move and swap
    ctRandomTwister(ctRandomTwister&& other) noexcept = default;

    void swap(ctRandomTwister& other) noexcept;

    ~ctRandomTwister() = default;

    // Non-copyable mostly because instances are space-heavy (mt19937s are big)
    ctRandomTwister(const ctRandomTwister&) = delete;
    ctRandomTwister& operator=(const ctRandomTwister&) = delete;
    ctRandomTwister& operator=(ctRandomTwister&& other) noexcept = delete;

private:
    // Keep the 5kb engine on the heap
    std::unique_ptr<engine_type> m_engine{};
};

// non-member namespace swap
inline void swap(ctRandomTwister& lhs, ctRandomTwister& rhs) noexcept
{
    lhs.swap(rhs);
}


// Implementation

inline ctRandomTwister::ctRandomTwister(uint32_t seed) :
    m_engine(std::make_unique<engine_type>(seed))
{
}

inline ctRandomTwister::ctRandomTwister() :
    m_engine(std::make_unique<engine_type>(std::random_device()()))
{
}

inline void ctRandomTwister::swap(ctRandomTwister& other) noexcept
{
    using std::swap;
    swap(m_engine, other.m_engine);
}

template <class IntegerT>
IntegerT ctRandomTwister::uniform_int(IntegerT lowerInclusiveBound, IntegerT upperInclusiveBound)
{
    return std::uniform_int_distribution<IntegerT>(lowerInclusiveBound, upperInclusiveBound)(*m_engine);
}

template <class RealT>
RealT ctRandomTwister::uniform_real(RealT lowerInclusiveBound, RealT upperInclusiveBound)
{
    return std::uniform_real_distribution<RealT>(lowerInclusiveBound, upperInclusiveBound)(*m_engine);
}

inline double ctRandomTwister::uniform_probability() const
{
    return std::uniform_real_distribution(0.0, 1.0)(*m_engine);
}

inline double ctRandomTwister::normal_real(double distributionMean, double distributionSigma) const
{
    return std::normal_distribution(distributionMean, distributionSigma)(*m_engine);
}

inline void ctRandomTwister::seed(uint32_t seed) const
{
    m_engine->seed(seed);
}
} // namespace ctl
