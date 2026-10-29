// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include "engine/core/meta/registration.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using gneiss::engine::error;
constexpr gneiss::engine::guid camera_type{gneiss::engine::guid::from_bytes(
    {0x21, 0x6a, 0x90, 0x31, 0x45, 0xe4, 0x47, 0x41, 0xa5, 0xd2, 0xb8, 0x11, 0x6d, 0x61, 0x42, 1})};
constexpr gneiss::engine::guid float_type{gneiss::engine::guid::from_bytes({1})};
constexpr gneiss::engine::guid string_type{gneiss::engine::guid::from_bytes({2})};
struct camera_fixture {
  float field_of_view = 60.0F;
  std::string_view label = "主相机";
  bool fail_get = false;
  bool fail_allocate = false;
  bool fail_set = false;
};
error get_field_of_view(void*, const void* object, gneiss::engine::value& output) {
  const auto& camera = *static_cast<const camera_fixture*>(object);
  if (camera.fail_allocate)
    throw std::bad_alloc{};
  if (camera.fail_get)
    throw std::runtime_error("getter probe");
  output.payload = camera.field_of_view;
  return error::success;
}
error set_field_of_view(void*, void* object, const gneiss::engine::value& input) {
  const auto value = std::get<float>(input.payload);
  if (value < 1 || value > 179)
    return error::invalid_argument;
  auto& camera = *static_cast<camera_fixture*>(object);
  if (camera.fail_allocate)
    throw std::bad_alloc{};
  if (camera.fail_set)
    throw std::runtime_error("setter probe");
  camera.field_of_view = value;
  return error::success;
}
error get_label(void*, const void* object, gneiss::engine::value& output) {
  output.payload = std::string{static_cast<const camera_fixture*>(object)->label};
  return error::success;
}
error get_malformed(void*, const void*, gneiss::engine::value& output) {
  output.payload = std::numeric_limits<float>::infinity();
  return error::success;
}
}
int main() try {
  namespace reflection = gneiss::engine;
  camera_fixture fixture;
  const std::array fields{
      gneiss::engine::meta::property_definition{gneiss::engine::meta::property_id{1}, float_type,
                                                gneiss::engine::meta::property_flags::none,
                                                "field_of_view"},
      gneiss::engine::meta::property_definition{gneiss::engine::meta::property_id{2}, string_type,
                                                gneiss::engine::meta::property_flags::read_only,
                                                "label"},
      gneiss::engine::meta::property_definition{gneiss::engine::meta::property_id{3}, float_type,
                                                gneiss::engine::meta::property_flags::none,
                                                "unbound"},
      gneiss::engine::meta::property_definition{gneiss::engine::meta::property_id{4}, float_type,
                                                gneiss::engine::meta::property_flags::none,
                                                "malformed"}};
  gneiss::engine::meta::registry_ref registry;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::create_registry(), registry)
          .failed() ||
      gneiss::engine::meta::register_type(registry, {camera_type, {}, 1, "camera", fields})
          .failed())
    return 1;
  const gneiss::engine::meta::property_binding field_of_view{
      .kind = gneiss::engine::value_kind::float32,
      .getter = get_field_of_view,
      .setter = set_field_of_view,
      .user_data = nullptr};
  const gneiss::engine::meta::property_binding label{.kind = gneiss::engine::value_kind::string,
                                                     .getter = get_label,
                                                     .setter = {},
                                                     .user_data = nullptr};
  auto invalid_read_only = label;
  invalid_read_only.setter = set_field_of_view;
  if (gneiss::engine::meta::bind_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{1}, field_of_view)
          .failed() ||
      gneiss::engine::meta::bind_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{1}, field_of_view)
          .failed() ||
      gneiss::engine::meta::bind_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{2},
                                          invalid_read_only) != error::invalid_argument ||
      gneiss::engine::meta::bind_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{2}, label)
          .failed() ||
      gneiss::engine::meta::bind_property(
          registry, camera_type, gneiss::engine::meta::property_id{4},
          {.kind = gneiss::engine::value_kind::float32, .getter = get_malformed, .setter = {}})
          .failed() ||
      gneiss::engine::meta::freeze(registry).failed() ||
      gneiss::engine::meta::bind_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{3},
                                          field_of_view) != error::invalid_state)
    return 2;
  gneiss::engine::meta::property_info info;
  if (gneiss::engine::meta::describe_property(registry, camera_type,
                                              gneiss::engine::meta::property_id{1}, info)
          .failed() ||
      info.kind != gneiss::engine::value_kind::float32 ||
      info.capabilities != (gneiss::engine::meta::property_capabilities::readable |
                            gneiss::engine::meta::property_capabilities::writable))
    return 3;
  // 同一绑定访问两个独立对象，不把对象地址存入 user_data。
  camera_fixture another;
  another.field_of_view = 45.0F;
  gneiss::engine::value independent;
  if (gneiss::engine::meta::read_property(
          registry, camera_type, gneiss::engine::meta::property_id{1}, &another, independent)
          .failed() ||
      std::get<float>(independent.payload) != 45.0F)
    return 14;
  independent.payload = 75.0F;
  if (gneiss::engine::meta::write_property(
          registry, camera_type, gneiss::engine::meta::property_id{1}, &another, independent)
          .failed() ||
      another.field_of_view != 75.0F || fixture.field_of_view != 60.0F ||
      gneiss::engine::meta::read_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{1}, nullptr,
                                          independent) != error::invalid_argument)
    return 15;
  auto* const target = &fixture;
  gneiss::engine::value value;
  if (gneiss::engine::meta::read_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{1}, target, value)
          .failed() ||
      value.kind() != gneiss::engine::value_kind::float32 ||
      std::abs(std::get<float>(value.payload) - 60) > 0.001F)
    return 4;
  value.payload = 90.0F;
  if (gneiss::engine::meta::write_property(registry, camera_type,
                                           gneiss::engine::meta::property_id{1}, target, value)
          .failed() ||
      std::abs(fixture.field_of_view - 90) > 0.001F)
    return 5;
  value.payload = -1.0F;
  if (gneiss::engine::meta::write_property(registry, camera_type,
                                           gneiss::engine::meta::property_id{1}, target,
                                           value) != error::invalid_argument ||
      std::abs(fixture.field_of_view - 90) > 0.001F)
    return 6;
  value.payload = 90.0F;
  if (gneiss::engine::meta::write_property(registry, camera_type,
                                           gneiss::engine::meta::property_id{1}, nullptr,
                                           value) != error::invalid_argument)
    return 7;
  value.payload = std::uint64_t{90};
  if (gneiss::engine::meta::write_property(registry, camera_type,
                                           gneiss::engine::meta::property_id{1}, target,
                                           value) != error::invalid_argument)
    return 8;
  if (gneiss::engine::meta::read_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{2}, target, value)
          .failed() ||
      value.kind() != gneiss::engine::value_kind::string ||
      std::get<std::string>(value.payload).size() != 9 ||
      gneiss::engine::meta::write_property(registry, camera_type,
                                           gneiss::engine::meta::property_id{2}, target,
                                           value) != error::unsupported ||
      gneiss::engine::meta::read_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{3}, target,
                                          value) != error::unsupported ||
      gneiss::engine::meta::read_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{4}, target,
                                          value) != error::internal ||
      value.kind() != gneiss::engine::value_kind::string ||
      std::get<std::string>(value.payload) != "主相机")
    return 9;
  // 回调抛异常转换为内部错误；读取保留原输出，setter 不修改目标。
  fixture.fail_get = true;
  if (gneiss::engine::meta::read_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{1}, target,
                                          value) != error::internal ||
      value.kind() != gneiss::engine::value_kind::string ||
      std::get<std::string>(value.payload) != "主相机")
    return 10;
  fixture.fail_get = false;
  fixture.fail_set = true;
  value.payload = 100.0F;
  if (gneiss::engine::meta::write_property(registry, camera_type,
                                           gneiss::engine::meta::property_id{1}, target,
                                           value) != error::internal ||
      std::abs(fixture.field_of_view - 90) > 0.001F)
    return 11;
  // 原生快照拥有文本，不依赖 getter 后续返回值及 Registry 生命周期。
  gneiss::engine::value snapshot;
  if (gneiss::engine::meta::read_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{2}, target, snapshot)
          .failed())
    return 12;
  fixture.label = "changed";
  if (gneiss::engine::meta::read_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{2}, target, value)
          .failed() ||
      std::get<std::string>(snapshot.payload) != "主相机" ||
      std::get<std::string>(value.payload) != "changed")
    return 13;
  fixture.fail_allocate = true;
  gneiss::engine::value preserved{std::string{"preserved"}};
  if (gneiss::engine::meta::read_property(registry, camera_type,
                                          gneiss::engine::meta::property_id{1}, target,
                                          preserved) != error::out_of_memory ||
      std::get<std::string>(preserved.payload) != "preserved")
    return 16;
  value.payload = 120.0F;
  if (gneiss::engine::meta::write_property(registry, camera_type,
                                           gneiss::engine::meta::property_id{1}, target,
                                           value) != error::out_of_memory ||
      fixture.field_of_view != 90.0F)
    return 17;
  const std::weak_ptr lifetime = registry;
  registry.reset();
  return lifetime.expired() &&
                 gneiss::engine::meta::read_property(registry, camera_type,
                                                     gneiss::engine::meta::property_id{1}, target,
                                                     value) == error::invalid_handle &&
                 std::get<std::string>(snapshot.payload) == "主相机"
             ? 0
             : 14;
} catch (...) {
  return 99;
}
