// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/reflection.hpp>

#include <array>
#include <string>
#include <type_traits>
#include <utility>

namespace {
constexpr gneiss::type_id type{{0x42, 1}};
constexpr gneiss::type_id number_type{{0x43, 1}};
constexpr gneiss::field_id number{1};
constexpr gneiss::field_id label{2};
constexpr gneiss::property_target target{.context = 7, .object = 11};
struct state {
  float value = 10;
  std::string text = "native label";
  bool fail = false;
  bool malformed = false;
};
gneiss::result get_number(void* data, gneiss::property_target object,
                          gneiss::property_value& value) noexcept {
  const auto& context = *static_cast<state*>(data);
  if (object.context != target.context || object.object != target.object) {
    return gneiss::result::not_found;
  }
  if (context.fail) {
    return gneiss::result::internal;
  }
  if (context.malformed) {
    value.payload = std::monostate{};
    return gneiss::result::success;
  }
  value.payload = context.value;
  return gneiss::result::success;
}
gneiss::result set_number(void* data, gneiss::property_target object,
                          const gneiss::property_value& value) noexcept {
  if (object.context != target.context || object.object != target.object ||
      value.kind() != gneiss::property_kind::float32) {
    return gneiss::result::invalid_argument;
  }
  static_cast<state*>(data)->value = *std::get_if<float>(&value.payload);
  return gneiss::result::success;
}
gneiss::result get_label(void* data, gneiss::property_target /*object*/,
                         gneiss::property_value& value) noexcept {
  value.payload = std::string_view{static_cast<state*>(data)->text};
  return gneiss::result::success;
}
bool configure(gneiss::type_registry& registry, state& data) {
  // 描述、文本和回调表均在返回前离开作用域，Registry 必须复制必要存储。
  std::string name = "NativeType";
  std::string field_name = "Value";
  const std::array fields{
      gneiss::field_desc{.id = number, .value_type_id = number_type, .name = field_name},
      gneiss::field_desc{
          .id = label,
          .value_type_id = number_type,
          .flags = gneiss::field_flags::read_only,
          .name = "Label",
      },
  };
  const gneiss::type_desc desc{.id = type, .schema_version = 1, .name = name, .fields = fields};
  const gneiss::property_accessor_desc accessor{
      .kind = gneiss::property_kind::float32,
      .getter = get_number,
      .setter = set_number,
      .user_data = &data,
  };
  const gneiss::property_accessor_desc text{
      .kind = gneiss::property_kind::string,
      .getter = get_label,
      .user_data = &data,
  };
  if (registry.register_type(desc).failed() || registry.register_type(desc).failed() ||
      registry.bind_property(type, number, accessor).failed() ||
      registry.bind_property(type, number, accessor).failed()) {
    return false;
  }
  auto conflict = accessor;
  conflict.setter = nullptr;
  if (registry.bind_property(type, number, conflict) != gneiss::result::invalid_argument ||
      registry.bind_property(type, label, text).failed() || registry.freeze().failed()) {
    return false;
  }
  // 冻结后的重复绑定不能由包装缓存绕过底层状态检查。
  return registry.bind_property(type, number, accessor) == gneiss::result::invalid_state;
}
bool verify_metadata(const gneiss::type_registry& registry) {
  gneiss::type_info info;
  if (registry.find_type(type, info).failed() || info.name != "NativeType" ||
      info.fields.size() != 2 || info.fields[0].name != "Value" ||
      info.fields[1].flags != gneiss::field_flags::read_only) {
    return false;
  }
  const auto copy = info;
  if (copy.fields.data() == info.fields.data()) {
    return false;
  }
  const auto status = registry.find_type(gneiss::type_id(), info);
  return status.failed() && info.id == type && info.fields.size() == 2 &&
         copy.fields[0].name == "Value";
}
bool verify_values(gneiss::type_registry& registry, state& data) {
  gneiss::property_value value{.payload = 0.0F};
  if (registry.get_property(type, number, target, value).failed() ||
      std::get<float>(value.payload) != 10) {
    return false;
  }
  value.payload = 23.0F;
  if (registry.set_property(type, number, target, value).failed() || data.value != 23) {
    return false;
  }
  data.fail = true;
  if (registry.get_property(type, number, target, value) != gneiss::result::internal ||
      std::get<float>(value.payload) != 23) {
    return false;
  }
  data.fail = false;
  data.malformed = true;
  if (registry.get_property(type, number, target, value) != gneiss::result::internal ||
      std::get<float>(value.payload) != 23) {
    return false;
  }
  data.malformed = false;
  if (registry.get_property(type, label, target, value).failed() ||
      std::get<std::string_view>(value.payload) != data.text) {
    return false;
  }
  return registry.set_property(type, label, target, value) == gneiss::result::unsupported;
}
bool verify_transfer(gneiss::type_registry&& registry, state& data) {
  gneiss::type_registry moved{std::move(registry)};
  auto released = moved.release();
  if (moved || !released) {
    return false;
  }
  gneiss_property_value native = GNEISS_PROPERTY_VALUE_INIT;
  if (gneiss_type_registry_get_property(released.get(), gneiss::to_native(type), number.get(),
                                        gneiss::to_native(target), &native) != GNEISS_SUCCESS ||
      native.payload.float32_value != data.value) {
    return false;
  }
  gneiss::type_registry adopted;
  if (gneiss::type_registry::create(adopted).failed() ||
      gneiss::type_registry::adopt(std::move(released), adopted).failed()) {
    return false;
  }
  gneiss::property_value value;
  return adopted.get_property(type, number, target, value).ok() &&
         std::get<float>(value.payload) == data.value && adopted.reset().ok() &&
         adopted.get_property(type, number, target, value) == gneiss::result::invalid_handle;
}

bool verify_payload_conversion() {
  const std::array values{
      gneiss::property_value{.payload = true},
      gneiss::property_value{.payload = std::int64_t{-7}},
      gneiss::property_value{.payload = std::uint64_t{9}},
      gneiss::property_value{.payload = 2.0F},
      gneiss::property_value{.payload = 3.0},
      gneiss::property_value{.payload = std::string_view{"text"}},
      gneiss::property_value{.payload = type},
      gneiss::property_value{.payload = gneiss::property_vec3{.x = 1, .y = 2, .z = 3}},
      gneiss::property_value{.payload = gneiss::property_quaternion{}},
  };
  for (const auto& value : values) {
    gneiss_property_value native = GNEISS_PROPERTY_VALUE_INIT;
    gneiss::property_value copy;
    if (gneiss::to_native(value, native).failed() || gneiss::from_native(native, copy).failed() ||
        copy.payload != value.payload) {
      return false;
    }
  }
  gneiss_property_value invalid = GNEISS_PROPERTY_VALUE_INIT;
  invalid.kind = 999;
  gneiss::property_value output{.payload = true};
  if (gneiss::from_native(invalid, output) != gneiss::result::unsupported ||
      !std::get<bool>(output.payload)) {
    return false;
  }
  invalid.kind = GNEISS_PROPERTY_KIND_BOOL;
  invalid.payload.bool_value = 2;
  return gneiss::from_native(invalid, output) == gneiss::result::invalid_argument &&
         std::get<bool>(output.payload);
}
} // namespace

int main() try {
  static_assert(!std::is_copy_constructible_v<gneiss::released_type_registry>);
  static_assert(std::is_nothrow_move_constructible_v<gneiss::released_type_registry>);
  static_assert(!std::is_same_v<gneiss::property_value, gneiss_property_value>);
  if (!verify_payload_conversion()) {
    return 5;
  }
  state data;
  gneiss::type_registry registry;
  if (gneiss::type_registry::create(registry).failed() || !configure(registry, data)) {
    return 1;
  }
  if (!verify_metadata(registry)) {
    return 2;
  }
  if (!verify_values(registry, data)) {
    return 3;
  }
  if (!verify_transfer(std::move(registry), data)) {
    return 4;
  }
  return 0;
} catch (...) {
  return 99;
}
