// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include <limits>
#include <type_traits>
using gneiss::engine::result;
static_assert(sizeof(result) == sizeof(std::int32_t));
static_assert(std::is_trivially_copyable_v<result>);
static_assert(std::is_standard_layout_v<result>);
static_assert(!std::is_convertible_v<result, std::int32_t>);
static_assert(!std::is_convertible_v<std::int32_t, result>);
static_assert(!std::is_convertible_v<result, bool>);
static_assert(result{} == result::unknown);
static_assert(result::success.ok() && result::not_ready.failed());
static_assert(result{1}.ok());
static_assert(result{-999}.code() == -999);
static_assert(result{std::numeric_limits<std::int32_t>::min()}.code() ==
              std::numeric_limits<std::int32_t>::min());
static_assert(result{std::numeric_limits<std::int32_t>::max()}.code() ==
              std::numeric_limits<std::int32_t>::max());
int main() {
  return result::dependency_failed.message() == "dependency failed" &&
                 result::success.message() == "success" &&
                 result::invalid_argument.message() == "invalid argument" &&
                 result{-999}.message() == "unrecognized result"
             ? 0
             : 1;
}
