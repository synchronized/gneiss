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
  const std::array values{value{},
                          value{true},
                          value{std::int64_t{-1}},
                          value{std::uint64_t{1}},
                          value{1.0F},
                          value{1.0},
                          value{std::string{}},
                          value{gneiss::engine::guid{}},
                          value{math::vector2{}},
                          value{math::vector3{}},
                          value{math::vector4{}},
                          value{math::quaternion{}},
                          value{math::matrix3{1}},
                          value{math::matrix4{1}},
                          value{ref<meta::object>{}}};
  for (std::size_t i = 0; i < values.size(); ++i)
    if (values[i].kind() != value::kind_index[i])
      return 4;
  static_assert(static_cast<unsigned>(value_kind::vec2) == 20);
  static_assert(static_cast<unsigned>(value_kind::object) == 50);
  // 精确比较保留类型和四元数符号，不把同一旋转视为相同存储值。
  for (const auto& item : values)
    if (item != item)
      return 6;
  const value rotation{math::quaternion_xyzw(0.1F, 0.2F, 0.3F, 0.9F)};
  if (value{1.0F} == value{1.0} ||
      rotation == value{math::quaternion_xyzw(-0.1F, -0.2F, -0.3F, -0.9F)} ||
      value{math::vector3{1, 2, 3}} == value{math::vector3{1, 2, 4}} || original == moved)
    return 7;
  // 通用容器不施加反射边界的有限值或文本编码要求。
  value raw{std::string("a\0b", 3)};
  value infinity{std::numeric_limits<float>::infinity()};
  return std::get<std::string>(raw.payload).size() == 3 && infinity.kind() == value_kind::float32
             ? 0
             : 5;
}
