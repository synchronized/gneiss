// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/result_conversion.hpp"
#include <limits>
#include <type_traits>
using gneiss::engine::result;
using gneiss::engine::detail::from_c_result;
using gneiss::engine::detail::to_c_result;
static_assert(sizeof(result) == sizeof(std::int32_t));
static_assert(std::is_trivially_copyable_v<result>);
static_assert(std::is_standard_layout_v<result>);
static_assert(!std::is_convertible_v<result, gneiss_result>);
static_assert(!std::is_convertible_v<gneiss_result, result>);
static_assert(!std::is_convertible_v<result, bool>);
static_assert(result{} == result::unknown);
static_assert(result::success.ok() && result::not_ready.failed());
static_assert(from_c_result(1).ok());
static_assert(to_c_result(from_c_result(-999)) == -999);
static_assert(to_c_result(from_c_result(std::numeric_limits<std::int32_t>::min())) ==
              std::numeric_limits<std::int32_t>::min());
static_assert(to_c_result(from_c_result(std::numeric_limits<std::int32_t>::max())) ==
              std::numeric_limits<std::int32_t>::max());
int main() {
  return result::dependency_failed.message() == "dependency failed" &&
                 result::success.message() == "success" &&
                 result::invalid_argument.message() == "invalid argument" &&
                 result{-999}.message() == "unrecognized result"
             ? 0
             : 1;
}
