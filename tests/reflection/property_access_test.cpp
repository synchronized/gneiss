// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/reflection/type_registry.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using gneiss::property_kind;
using gneiss::property_target;
using gneiss::property_value;
using gneiss::engine::result;
constexpr gneiss::engine::type_guid camera_type{gneiss::engine::guid::from_bytes(
    {0x21, 0x6a, 0x90, 0x31, 0x45, 0xe4, 0x47, 0x41, 0xa5, 0xd2, 0xb8, 0x11, 0x6d, 0x61, 0x42, 1})};
constexpr gneiss::engine::type_guid float_type{gneiss::engine::guid::from_bytes({1})};
constexpr gneiss::engine::type_guid string_type{gneiss::engine::guid::from_bytes({2})};
struct camera_fixture {
  float field_of_view = 60.0F;
  std::string_view label = "主相机";
  bool fail_get = false;
  bool fail_set = false;
};
result get_field_of_view(void* data, property_target target, property_value* output) {
  if (target.context != 7 || target.object != 11)
    return result::not_found;
  const auto& camera = *static_cast<camera_fixture*>(data);
  if (camera.fail_get)
    throw std::runtime_error("getter probe");
  output->payload = camera.field_of_view;
  return result::success;
}
result set_field_of_view(void* data, property_target target, const property_value* input) {
  if (target.context != 7 || target.object != 11)
    return result::not_found;
  const auto value = std::get<float>(input->payload);
  if (value < 1 || value > 179)
    return result::invalid_argument;
  auto& camera = *static_cast<camera_fixture*>(data);
  if (camera.fail_set)
    throw std::runtime_error("setter probe");
  camera.field_of_view = value;
  return result::success;
}
result get_label(void* data, property_target, property_value* output) {
  output->payload = static_cast<camera_fixture*>(data)->label;
  return result::success;
}
result get_malformed(void*, property_target, property_value* output) {
  output->payload = std::numeric_limits<float>::infinity();
  return result::success;
}
}
int main() try {
  namespace reflection = gneiss::engine;
  camera_fixture fixture;
  const std::array fields{reflection::field_definition{gneiss::field_id{1}, float_type,
                                                       gneiss::field_flags::none, "field_of_view"},
                          reflection::field_definition{gneiss::field_id{2}, string_type,
                                                       gneiss::field_flags::read_only, "label"},
                          reflection::field_definition{gneiss::field_id{3}, float_type,
                                                       gneiss::field_flags::none, "unbound"},
                          reflection::field_definition{gneiss::field_id{4}, float_type,
                                                       gneiss::field_flags::none, "malformed"}};
  reflection::registry_ref registry;
  if (reflection::create_native(registry).failed() ||
      reflection::register_type(registry, {camera_type, {}, 1, "camera", fields}).failed())
    return 1;
  const reflection::property_binding field_of_view{.kind = property_kind::float32,
                                                   .getter = get_field_of_view,
                                                   .setter = set_field_of_view,
                                                   .user_data = &fixture};
  const reflection::property_binding label{
      .kind = property_kind::string, .getter = get_label, .setter = {}, .user_data = &fixture};
  auto invalid_read_only = label;
  invalid_read_only.setter = set_field_of_view;
  if (reflection::bind_property(registry, camera_type, gneiss::field_id{1}, field_of_view)
          .failed() ||
      reflection::bind_property(registry, camera_type, gneiss::field_id{1}, field_of_view)
          .failed() ||
      reflection::bind_property(registry, camera_type, gneiss::field_id{2}, invalid_read_only) !=
          result::invalid_argument ||
      reflection::bind_property(registry, camera_type, gneiss::field_id{2}, label).failed() ||
      reflection::bind_property(
          registry, camera_type, gneiss::field_id{4},
          {.kind = property_kind::float32, .getter = get_malformed, .setter = {}})
          .failed() ||
      reflection::freeze(registry).failed() ||
      reflection::bind_property(registry, camera_type, gneiss::field_id{3}, field_of_view) !=
          result::invalid_state)
    return 2;
  gneiss::field_info info;
  if (reflection::describe_field(registry, camera_type, gneiss::field_id{1}, info).failed() ||
      info.kind != property_kind::float32 ||
      info.capabilities !=
          (gneiss::property_capabilities::readable | gneiss::property_capabilities::writable))
    return 3;
  constexpr property_target target{7, 11};
  property_value value;
  if (reflection::get_property(registry, camera_type, 1, target, value).failed() ||
      value.kind() != property_kind::float32 ||
      std::abs(std::get<float>(value.payload) - 60) > 0.001F)
    return 4;
  value.payload = 90.0F;
  if (reflection::set_property(registry, camera_type, 1, target, value).failed() ||
      std::abs(fixture.field_of_view - 90) > 0.001F)
    return 5;
  value.payload = -1.0F;
  if (reflection::set_property(registry, camera_type, 1, target, value) !=
          result::invalid_argument ||
      std::abs(fixture.field_of_view - 90) > 0.001F)
    return 6;
  value.payload = 90.0F;
  if (reflection::set_property(registry, camera_type, 1, {8, 11}, value) != result::not_found)
    return 7;
  value.payload = std::uint64_t{90};
  if (reflection::set_property(registry, camera_type, 1, target, value) != result::invalid_argument)
    return 8;
  if (reflection::get_property(registry, camera_type, 2, target, value).failed() ||
      value.kind() != property_kind::string ||
      std::get<std::string_view>(value.payload).size() != 9 ||
      reflection::set_property(registry, camera_type, 2, target, value) != result::unsupported ||
      reflection::get_property(registry, camera_type, 3, target, value) != result::unsupported ||
      reflection::get_property(registry, camera_type, 4, target, value) != result::internal ||
      value.kind() != property_kind::invalid)
    return 9;
  // 回调抛异常转换为内部错误；getter 清空输出，setter 不修改目标。
  fixture.fail_get = true;
  if (reflection::get_property(registry, camera_type, 1, target, value) != result::internal ||
      value.kind() != property_kind::invalid)
    return 10;
  fixture.fail_get = false;
  fixture.fail_set = true;
  value.payload = 100.0F;
  if (reflection::set_property(registry, camera_type, 1, target, value) != result::internal ||
      std::abs(fixture.field_of_view - 90) > 0.001F)
    return 11;
  // 原生快照拥有文本，不依赖 getter 后续返回值及 Registry 生命周期。
  reflection::property_value snapshot;
  if (reflection::read_property(registry, camera_type, gneiss::field_id{2}, target, snapshot)
          .failed())
    return 12;
  fixture.label = "changed";
  if (reflection::get_property(registry, camera_type, 2, target, value).failed() ||
      std::get<std::string>(snapshot.payload) != "主相机" ||
      std::get<std::string_view>(value.payload) != "changed")
    return 13;
  const std::weak_ptr lifetime = registry;
  registry.reset();
  return lifetime.expired() &&
                 reflection::get_property(registry, camera_type, 1, target, value) ==
                     result::invalid_handle &&
                 std::get<std::string>(snapshot.payload) == "主相机"
             ? 0
             : 14;
} catch (...) {
  return 99;
}
