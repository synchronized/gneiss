// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
using gneiss::engine::error;
static_assert(sizeof(error) == sizeof(std::int32_t));
static_assert(std::is_trivially_copyable_v<error>);
static_assert(std::is_standard_layout_v<error>);
static_assert(!std::is_convertible_v<error, std::int32_t>);
static_assert(!std::is_convertible_v<std::int32_t, error>);
static_assert(!std::is_convertible_v<error, bool>);
static_assert(!std::is_constructible_v<bool, error>);
static_assert(error{} == error::unknown);
static_assert(error::success.ok() && error::not_ready.failed());
static_assert(error{1}.ok());
static_assert(error{-999}.code() == -999);
static_assert(error{std::numeric_limits<std::int32_t>::min()}.code() ==
              std::numeric_limits<std::int32_t>::min());
static_assert(error{std::numeric_limits<std::int32_t>::max()}.code() ==
              std::numeric_limits<std::int32_t>::max());
using gneiss::engine::result;
struct move_only {
  inline static int alive = 0;
  explicit move_only(int v) : value(v) { ++alive; }
  move_only(move_only&& other) noexcept : value(other.value) { ++alive; }
  move_only(const move_only&) = delete;
  ~move_only() { --alive; }
  int value;
};
static_assert(!std::is_default_constructible_v<result<move_only>>);
static_assert(!std::is_copy_constructible_v<result<move_only>>);
static_assert(std::is_move_constructible_v<result<move_only>>);
int main() {
  {
    result<move_only> failed = error::io;
    if (failed || failed.error() != error::io || move_only::alive != 0)
      return 2;
    result<move_only> success = move_only{42};
    if (!success || success->value != 42 || move_only::alive != 1)
      return 3;
    auto moved = std::move(success);
    if (!moved || moved->value != 42)
      return 4;
  }
  if (move_only::alive != 0)
    return 5;
  result<std::unique_ptr<int>> owned = std::make_unique<int>(7);
  auto pointer = *std::move(owned);
  if (!pointer || *pointer != 7)
    return 6;
  result<int, std::string> diagnostic = std::string("decode failed");
  if (diagnostic || diagnostic.error() != "decode failed")
    return 7;
  result<void> done;
  result<void> failed = error::cancelled;
  if (!done || failed || failed.error() != error::cancelled)
    return 8;

  return error::dependency_failed.message() == "dependency failed" &&
                 error::success.message() == "success" &&
                 error::cancelled.message() == "operation cancelled" &&
                 error::invalid_argument.message() == "invalid argument" &&
                 error{-999}.message() == "unrecognized result"
             ? 0
             : 1;
}
