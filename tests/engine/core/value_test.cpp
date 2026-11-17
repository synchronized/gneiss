// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/types/value.hpp>
#include <array>
#include <limits>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_move_constructible_v<gneiss::value>);
static_assert(std::is_nothrow_move_assignable_v<gneiss::value>);

int main() {
  using namespace gneiss;
  static_assert(!is_scalar_value_kind(value_kind::invalid));
  static_assert(!is_scalar_value_kind(value_kind::object));
  static_assert(!is_scalar_value_kind(static_cast<value_kind>(49)));
  static_assert(!is_scalar_value_kind(static_cast<value_kind>(999)));
  static_assert(is_scalar_value_kind(value_kind::matrix4));
  static_assert(is_scalar_value_kind(value_kind::guid));
  sequence_value_data data{{value{std::int64_t{1}}, value{std::string{"old"}}}};
  sequence_value frozen{data};
  data.elements[0] = value{std::int64_t{2}};
  if (std::get<std::int64_t>(frozen.get().elements[0].payload) != 1)
    return 10;
  auto version = frozen;
  if (&version.get() != &frozen.get())
    return 11;
  auto updated_data = version.get();
  updated_data.elements[1] = value{std::string{"new"}};
  version = sequence_value{updated_data};
  if (version == frozen || std::get<std::string>(frozen.get().elements[1].payload) != "old")
    return 12;
  if (value{sequence_value{data}} != value{sequence_value{data}})
    return 13;
  static_assert(!is_scalar_value_kind(value_kind::record));
  static_assert(!is_scalar_value_kind(value_kind::sequence));
  const value unsafe{sequence_value{sequence_value_data{{value{ref<meta::object>{}}}}}};
  if (validate_persistent_value(unsafe) != error::unsupported)
    return 14;
  if (validate_persistent_value(value{frozen}).failed())
    return 15;
  const value duplicate{map_value{map_value_data{
      {{value{std::string{"key"}}, value{true}}, {value{std::string{"key"}}, value{false}}}}}};
  if (validate_persistent_value(duplicate) != error::invalid_argument)
    return 16;
  if (validate_persistent_value(value{frozen}, 0) != error::invalid_argument)
    return 17;
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
                          value{gneiss::guid{}},
                          value{math::vector2{}},
                          value{math::vector3{}},
                          value{math::vector4{}},
                          value{math::quaternion{}},
                          value{math::matrix3{1}},
                          value{math::matrix4{1}},
                          value{ref<meta::object>{}},
                          value{record_value{}},
                          value{sequence_value{}},
                          value{map_value{}},
                          value{optional_value{}},
                          value{variant_value{}},
                          value{asset_reference_value{}},
                          value{object_reference_value{}}};
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
