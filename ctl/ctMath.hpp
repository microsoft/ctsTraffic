/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

#include <tuple>
#include <numeric>
#include <cmath>

namespace ctl
{
    ///
    /// calculating a sampled standard deviation and mean
    ///
    /// Returns a tuple of doubles recording the results:
    ///   get<0> : the mean value
    ///   get<1> : the standard deviation
    ///
    template <typename BidirectionalIterator>
    std::tuple<double, double> ctSampledStandardDeviation(const BidirectionalIterator& begin, const BidirectionalIterator& end)
    {
        const auto size = end - begin;
        if (size == 0)
        {
            return std::make_tuple(
                static_cast<double>(0),
                static_cast<double>(0));
        }
        if (size == 1)
        {
            return std::make_tuple(
                static_cast<double>(*begin),
                static_cast<double>(0));
        }

        const double sum = std::accumulate(begin, end, 0.0);
        const double mean = sum / size;
        // ReSharper disable once CppUseAuto
        double accum = 0.0;
        for (auto iter = begin; iter != end; ++iter)
        {
            auto& value = *iter;
            accum += (value - mean) * (value - mean);
        }

        const auto stdev = std::sqrt(static_cast<double>(accum / (size - 1.0)));
        return std::make_tuple(mean, stdev);
    }

    ///
    /// calculating the interquartile range from this basic algorithm
    /// 1. split data into 2 sections evenly
    /// 2. determine the median of these 2 groups
    ////   - median is either the number splitting the group evenly, or the average between the middle 2 values
    ///
    /// ** Requires input to be sorted **
    ///
    /// Returns a tuple of doubles recording the results:
    ///   get<0> : quartile 1 (median of the lower half - at the 25% mark)
    ///   get<1> : quartile 2 (the median value - at the 50% mark)
    ///   get<2> : quartile 3 (median of the upper half - at the 75% mark)
    ///
    template <typename BidirectionalIterator>
    std::tuple<double, double, double> ctInterquartileRange(const BidirectionalIterator& begin, const BidirectionalIterator& end)
    {
        const auto size = end - begin;
        if (size < 3)
        {
            return std::make_tuple(
                static_cast<double>(0),
                static_cast<double>(0),
                static_cast<double>(0));
        }

        if (size == 3)
        {
            return std::make_tuple(
                static_cast<double>(*begin),
                static_cast<double>(*(begin + 1)),
                static_cast<double>(*(begin + 2)));
        }

        const auto split_section = [](const BidirectionalIterator& split_begin, const BidirectionalIterator& split_end)
            -> std::tuple<BidirectionalIterator, BidirectionalIterator> {
            const size_t numeric_count = split_end - split_begin + 1; // this is the N + 1 value

            // if begin and end are already right next to each other, immediately return the same values
            if (numeric_count < 3)
            {
                return std::make_tuple(split_begin, split_end);
            }

            const auto numeric_quotient = numeric_count / 2;
            const auto numeric_remaineder = numeric_count % 2;

            // choose the (N+1)/2 value
            // - if it lands on a value, the iterator before and after
            // - if it lands between 2 values, return those 2 values
            BidirectionalIterator lhs;
            BidirectionalIterator rhs;
            if (numeric_remaineder == 0)
            {
                // before and after the median
                lhs = split_begin + numeric_quotient - 2;
                rhs = split_begin + numeric_quotient;
            }
            else
            {
                // the 2 consecutive center iterators
                lhs = split_begin + numeric_quotient - 1;
                rhs = split_begin + numeric_quotient;
            }
            return std::make_tuple(lhs, rhs);
        };

        const auto find_median = [](const std::tuple<BidirectionalIterator, BidirectionalIterator>& split) -> double {
            const BidirectionalIterator& lhs = std::get<0>(split);
            const BidirectionalIterator& rhs = std::get<1>(split);
            FAIL_FAST_IF_MSG(rhs < lhs, "ctInterquartileRange internal error - the rhs iterator is less than the lhs iterator");

            double median_value = 0.0;
            switch (rhs - lhs)
            {
                case 1: {
                    // next to each other: take the average for the median
                    // must guard against overflow
                    auto lhs_value = *lhs;
                    auto rhs_value = *rhs;
                    const double sum = static_cast<double>(lhs_value) + static_cast<double>(rhs_value);
                    if (sum < lhs_value || sum < rhs_value)
                    {
                        // overflow - divide first, then add
                        median_value = static_cast<double>(lhs_value) / 2.0;
                        median_value += static_cast<double>(rhs_value) / 2.0;
                    }
                    else
                    {
                        median_value = static_cast<double>(sum) / 2.0;
                    }
                    break;
                }

                case 2: {
                    // two apart: the one in the middle is the median
                    median_value = static_cast<double>(*(lhs + 1));
                    break;
                }

                default:
                    FAIL_FAST_MSG("ctInterquartileRange internal error - returned iterators more than two apart [%Iu]", rhs - lhs);
            }

            return median_value;
        };

        const auto median_split = split_section(begin, end);
        const double median = find_median(median_split);

        const auto lhs_split = split_section(begin, std::get<0>(median_split) + 1);
        const double lower_quartile = find_median(lhs_split);

        const auto rhs_split = split_section(std::get<1>(median_split), end);
        const double higher_quartile = find_median(rhs_split);

        return std::make_tuple(
            lower_quartile,
            median,
            higher_quartile);
    }
}
