// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/reflection/reflection_types.hpp"
#include <gneiss/engine/reflection.h>
#include <limits>

// 仅作值布局适配，不包含 SDK 所有者或调用 C ABI。
namespace gneiss {
[[nodiscard]] constexpr gneiss_type_id to_native(const type_id& value) noexcept {
  gneiss_type_id native{};
  std::ranges::copy(value.bytes, native.bytes);
  return native;
}
[[nodiscard]] constexpr type_id from_native(const gneiss_type_id& value) noexcept {
  type_id native{};
  std::ranges::copy(value.bytes, native.bytes.begin());
  return native;
}
[[nodiscard]] constexpr gneiss_property_target to_native(property_target value) noexcept {
  return {.context = value.context, .object = value.object};
}
[[nodiscard]] constexpr property_target from_native(gneiss_property_target value) noexcept {
  return {.context = value.context, .object = value.object};
}
/** 按 variant 的活动类型转换，长度过大时保留原输出。 */
[[nodiscard]] inline result to_native(const property_value& value,
                                      gneiss_property_value& output) noexcept;
/** C 值必须具有有效布局与文本指针；失败保留原输出。 */
[[nodiscard]] inline result from_native(const gneiss_property_value& value,
                                        property_value& output) noexcept;
[[nodiscard]] inline result to_native(const property_value& value,
                                      gneiss_property_value& output) noexcept {
  gneiss_property_value native = GNEISS_PROPERTY_VALUE_INIT;
  native.kind = static_cast<std::uint32_t>(value.kind());
  switch (value.kind()) {
  case property_kind::invalid:
    return result::invalid_argument;
  case property_kind::boolean:
    native.payload.bool_value = static_cast<std::uint8_t>((*std::get_if<bool>(&value.payload)));
    break;
  case property_kind::int64:
    native.payload.int64_value = (*std::get_if<std::int64_t>(&value.payload));
    break;
  case property_kind::uint64:
    native.payload.uint64_value = (*std::get_if<std::uint64_t>(&value.payload));
    break;
  case property_kind::float32:
    native.payload.float32_value = (*std::get_if<float>(&value.payload));
    break;
  case property_kind::float64:
    native.payload.float64_value = (*std::get_if<double>(&value.payload));
    break;
  case property_kind::string: {
    const auto text = (*std::get_if<property_string>(&value.payload));
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
      return result::invalid_argument;
    }
    native.payload.string_value = {
        .data = text.data(),
        .length = static_cast<std::uint32_t>(text.size()),
    };
    break;
  }
  case property_kind::type_id:
    native.payload.type_id_value = to_native((*std::get_if<type_id>(&value.payload)));
    break;
  case property_kind::vec3: {
    const auto v = (*std::get_if<property_vec3>(&value.payload));
    native.payload.vec3_value = {.x = v.x, .y = v.y, .z = v.z};
    break;
  }
  case property_kind::quaternion: {
    const auto v = (*std::get_if<property_quaternion>(&value.payload));
    native.payload.quaternion_value = {.x = v.x, .y = v.y, .z = v.z, .w = v.w};
    break;
  }
  }
  output = native;
  return result::success;
}

[[nodiscard]] inline result from_native(const gneiss_property_value& value,
                                        property_value& output) noexcept {
  if (value.struct_size < sizeof(gneiss_property_value)) {
    return result::invalid_argument;
  }
  property_value native;
  switch (value.kind) {
  case GNEISS_PROPERTY_KIND_BOOL:
    if (value.payload.bool_value > 1) {
      return result::invalid_argument;
    }
    native.payload = value.payload.bool_value != 0;
    break;
  case GNEISS_PROPERTY_KIND_INT64:
    native.payload = value.payload.int64_value;
    break;
  case GNEISS_PROPERTY_KIND_UINT64:
    native.payload = value.payload.uint64_value;
    break;
  case GNEISS_PROPERTY_KIND_FLOAT32:
    native.payload = value.payload.float32_value;
    break;
  case GNEISS_PROPERTY_KIND_FLOAT64:
    native.payload = value.payload.float64_value;
    break;
  case GNEISS_PROPERTY_KIND_STRING: {
    const auto text = value.payload.string_value;
    if (text.length != 0 && text.data == nullptr) {
      return result::invalid_argument;
    }
    native.payload =
        text.data == nullptr ? property_string{} : property_string{text.data, text.length};
    break;
  }
  case GNEISS_PROPERTY_KIND_TYPE_ID:
    native.payload = from_native(value.payload.type_id_value);
    break;
  case GNEISS_PROPERTY_KIND_VEC3: {
    const auto v = value.payload.vec3_value;
    native.payload = property_vec3{.x = v.x, .y = v.y, .z = v.z};
    break;
  }
  case GNEISS_PROPERTY_KIND_QUATERNION: {
    const auto v = value.payload.quaternion_value;
    native.payload = property_quaternion{.x = v.x, .y = v.y, .z = v.z, .w = v.w};
    break;
  }
  default:
    return result::unsupported;
  }
  output = native;
  return result::success;
}

} // namespace gneiss
