// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/value.hpp"
#include <array>
#include <limits>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_move_constructible_v<gneiss::engine::value>);
static_assert(std::is_nothrow_move_assignable_v<gneiss::engine::value>);

int main() {
  using namespace gneiss::engine;
  value original;
  {
    std::string source(4096, 'x');
    original.payload = source;
    source.assign("changed");
  }
  if (original.kind() != value_kind::string ||
      std::get<std::string>(original.payload) != std::string(4096, 'x'))
    return 1;
  auto copy = original;
  std::get<std::string>(copy.payload)[0] = 'y';
  if (std::get<std::string>(original.payload)[0] != 'x')
    return 2;
  auto moved = std::move(copy);
  if (std::get<std::string>(moved.payload)[0] != 'y')
    return 3;
  // 类别序号同时供既有资产适配器使用，固定类别的顺序必须保持。
  const std::array values{
      value{},          value{true},        value{std::int64_t{-1}}, value{std::uint64_t{1}},
      value{1.0F},      value{1.0},         value{std::string{}},    value{gneiss::engine::guid{}},
      value{vector3{}}, value{quaternion{}}};
  for (std::size_t i = 0; i < values.size(); ++i)
    if (static_cast<std::size_t>(values[i].kind()) != i)
      return 4;
  // 通用容器不施加反射边界的有限值或文本编码要求。
  value raw{std::string("a\0b", 3)};
  value infinity{std::numeric_limits<float>::infinity()};
  return std::get<std::string>(raw.payload).size() == 3 && infinity.kind() == value_kind::float32
             ? 0
             : 5;
}
