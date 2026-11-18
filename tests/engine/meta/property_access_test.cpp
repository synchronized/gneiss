// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/throws.hpp"
#include "../../support/value_result.hpp"
#include <array>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <gneiss/engine/core/meta/registration.hpp>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using gneiss::error;
constexpr gneiss::guid camera_type{gneiss::guid::from_bytes(
    {0x21, 0x6a, 0x90, 0x31, 0x45, 0xe4, 0x47, 0x41, 0xa5, 0xd2, 0xb8, 0x11, 0x6d, 0x61, 0x42, 1})};
constexpr gneiss::guid float_type{gneiss::guid::from_bytes({1})};
constexpr gneiss::guid string_type{gneiss::guid::from_bytes({2})};
struct camera_fixture {
  float field_of_view = 60.0F;
  std::string_view label = "主相机";
  error read_error = error::success;
  bool wrong_kind = false;
  bool fail_get = false;
  bool fail_allocate = false;
  bool fail_set = false;
};
error get_field_of_view(void*, const void* object, gneiss::value& output) {
  const auto& camera = *static_cast<const camera_fixture*>(object);
  if (camera.read_error.failed())
    return camera.read_error;
  if (camera.wrong_kind) {
    output = gneiss::value{true};
    return error::success;
  }
  if (camera.fail_allocate)
    throw std::bad_alloc{};
  if (camera.fail_get)
    throw std::runtime_error("getter probe");
  output.payload = camera.field_of_view;
  return error::success;
}
error set_field_of_view(void*, void* object, const gneiss::value& input) {
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
error get_label(void*, const void* object, gneiss::value& output) {
  output.payload = std::string{static_cast<const camera_fixture*>(object)->label};
  return error::success;
}
error get_malformed(void*, const void*, gneiss::value& output) {
  output.payload = std::numeric_limits<float>::infinity();
  return error::success;
}
}
int main(int argc, char** argv) try {
  const std::string_view mode = argc == 2 ? argv[1] : "";
  if (!mode.empty()) {
    // 保留 Engine DLL 的断言诊断，仅拦截 abort，避免 Windows 崩溃弹窗。
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::signal(SIGABRT, [](int) { std::_Exit(86); });
    if (mode == "empty-write") {
      (void)gneiss::meta::instance{}.assign(gneiss::value{1.0F});
      return 90;
    }
    if (mode == "empty-instance") {
      (void)gneiss::meta::const_instance{}.read_value();
      return 90;
    }
  }
  namespace reflection = gneiss;
  camera_fixture fixture;
  const std::array fields{
      gneiss::meta::property_definition{gneiss::meta::property_id{1}, float_type,
                                                gneiss::meta::property_access::read_write,
                                                "field_of_view"},
      gneiss::meta::property_definition{gneiss::meta::property_id{2}, string_type,
                                                gneiss::meta::property_access::read_only,
                                                "label"},
      gneiss::meta::property_definition{gneiss::meta::property_id{3}, float_type,
                                                gneiss::meta::property_access::read_write,
                                                "unbound"},
      gneiss::meta::property_definition{gneiss::meta::property_id{4}, float_type,
                                                gneiss::meta::property_access::read_write,
                                                "malformed"}};
  gneiss::meta::registry_ref registry;
  if (gneiss::test_support::assign_value(gneiss::meta::create_registry(), registry)
          .failed() ||
      gneiss::meta::register_type(registry, {camera_type, {}, 1, "camera", fields})
          .failed())
    return 1;
  const gneiss::meta::property_binding field_of_view{
      .kind = gneiss::value_kind::float32,
      .getter = get_field_of_view,
      .setter = set_field_of_view,
      .user_data = nullptr};
  const gneiss::meta::property_binding label{.kind = gneiss::value_kind::string,
                                                     .getter = get_label,
                                                     .setter = {},
                                                     .user_data = nullptr};
  auto invalid_read_only = label;
  invalid_read_only.setter = set_field_of_view;
  if (gneiss::meta::bind_property(registry, camera_type,
                                          gneiss::meta::property_id{1}, field_of_view)
          .failed() ||
      gneiss::meta::bind_property(registry, camera_type,
                                          gneiss::meta::property_id{1}, field_of_view)
          .failed() ||
      gneiss::meta::bind_property(registry, camera_type,
                                          gneiss::meta::property_id{2},
                                          invalid_read_only) != error::invalid_argument ||
      gneiss::meta::bind_property(registry, camera_type,
                                          gneiss::meta::property_id{2}, label)
          .failed() ||
      gneiss::meta::bind_property(
          registry, camera_type, gneiss::meta::property_id{4},
          {.kind = gneiss::value_kind::float32, .getter = get_malformed, .setter = {}})
          .failed() ||
      gneiss::meta::freeze(registry).failed() ||
      gneiss::meta::bind_property(registry, camera_type,
                                          gneiss::meta::property_id{3},
                                          field_of_view) != error::invalid_state)
    return 2;
  if (mode == "empty-source") {
    auto target = gneiss::meta::view(registry, fixture.field_of_view);
    if (!target)
      return 91;
    (void)target->assign(gneiss::meta::const_instance{});
    return 90;
  }
  if (mode == "wrong-kind") {
    fixture.wrong_kind = true;
    (void)gneiss::meta::read_property(registry, camera_type, gneiss::meta::property_id{1},
                                      &fixture);
    return 90;
  }
  gneiss::meta::property info;
  if (!(info = gneiss::meta::find_property(registry, camera_type,
                                                   gneiss::meta::property_id{1})) ||
      info.type().scalar_kind() != gneiss::value_kind::float32 ||
      info.capabilities() != (gneiss::meta::property_capabilities::readable |
                              gneiss::meta::property_capabilities::writable))
    return 3;
  // 同一绑定访问两个独立对象，不把对象地址存入 user_data。
  camera_fixture another;
  another.field_of_view = 45.0F;
  gneiss::value independent;
  if (gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, camera_type,
                                              gneiss::meta::property_id{1}, &another),
          independent)
          .failed() ||
      std::get<float>(independent.payload) != 45.0F)
    return 14;
  independent.payload = 75.0F;
  if (gneiss::meta::write_property(
          registry, camera_type, gneiss::meta::property_id{1}, &another, independent)
          .failed() ||
      another.field_of_view != 75.0F || fixture.field_of_view != 60.0F ||
      gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, camera_type,
                                              gneiss::meta::property_id{1}, nullptr),
          independent) != error::invalid_argument)
    return 15;
  auto* const target = &fixture;
  gneiss::value value;
  if (gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, camera_type,
                                              gneiss::meta::property_id{1}, target),
          value)
          .failed() ||
      value.kind() != gneiss::value_kind::float32 ||
      std::abs(std::get<float>(value.payload) - 60) > 0.001F)
    return 4;
  value.payload = 90.0F;
  if (gneiss::meta::write_property(registry, camera_type,
                                           gneiss::meta::property_id{1}, target, value)
          .failed() ||
      std::abs(fixture.field_of_view - 90) > 0.001F)
    return 5;
  value.payload = -1.0F;
  if (gneiss::meta::write_property(registry, camera_type,
                                           gneiss::meta::property_id{1}, target,
                                           value) != error::invalid_argument ||
      std::abs(fixture.field_of_view - 90) > 0.001F)
    return 6;
  value.payload = 90.0F;
  if (gneiss::meta::write_property(registry, camera_type,
                                           gneiss::meta::property_id{1}, nullptr,
                                           value) != error::invalid_argument)
    return 7;
  value.payload = std::uint64_t{90};
  if (gneiss::meta::write_property(registry, camera_type,
                                           gneiss::meta::property_id{1}, target,
                                           value) != error::invalid_argument)
    return 8;
  if (gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, camera_type, gneiss::meta::property_id{2}, target),
          value)
          .failed() ||
      value.kind() != gneiss::value_kind::string ||
      std::get<std::string>(value.payload).size() != 9 ||
      gneiss::meta::write_property(registry, camera_type, gneiss::meta::property_id{2}, target,
                                   value) != error::unsupported ||
      gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, camera_type, gneiss::meta::property_id{3}, target),
          value) != error::unsupported ||
      gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, camera_type, gneiss::meta::property_id{4}, target),
          value) != error::invalid_argument ||
      value.kind() != gneiss::value_kind::string ||
      std::get<std::string>(value.payload) != "主相机")
    return 9;
  {
    const float invalid_scalar = std::numeric_limits<float>::infinity();
    auto scalar = gneiss::meta::view(registry, invalid_scalar);
    if (!scalar || scalar->read_value().error() != error::invalid_argument)
      return 20;
  }
  fixture.read_error = error::not_ready;
  auto unavailable =
      gneiss::meta::read_property(registry, camera_type, gneiss::meta::property_id{1}, target);
  if (unavailable || unavailable.error() != error::not_ready)
    return 19;
  fixture.read_error = error::success;
  // 回调异常向外传播；该夹具在抛出前不修改输出和目标。
  fixture.fail_get = true;
  if (!gneiss::test_support::throws<std::runtime_error>([&] {
        (void)(gneiss::test_support::assign_value(
            gneiss::meta::read_property(registry, camera_type,
                                                gneiss::meta::property_id{1}, target),
            value));
      }) ||
      value.kind() != gneiss::value_kind::string ||
      std::get<std::string>(value.payload) != "主相机")
    return 10;
  fixture.fail_get = false;
  fixture.fail_set = true;
  value.payload = 100.0F;
  if (!gneiss::test_support::throws<std::runtime_error>([&] {
        (void)(gneiss::meta::write_property(
            registry, camera_type, gneiss::meta::property_id{1}, target, value));
      }) ||
      std::abs(fixture.field_of_view - 90) > 0.001F)
    return 11;
  // 原生快照拥有文本，不依赖 getter 后续返回值及 Registry 生命周期。
  auto snapshot = gneiss::meta::read_property(registry, camera_type,
                                                      gneiss::meta::property_id{2}, target);
  if (!snapshot)
    return 12;
  fixture.label = "changed";
  if (gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, camera_type,
                                              gneiss::meta::property_id{2}, target),
          value)
          .failed() ||
      std::get<std::string>(snapshot->payload) != "主相机" ||
      std::get<std::string>(value.payload) != "changed")
    return 13;
  fixture.fail_allocate = true;
  gneiss::value preserved{std::string{"preserved"}};
  if (!gneiss::test_support::throws<std::bad_alloc>([&] {
        (void)(gneiss::test_support::assign_value(
            gneiss::meta::read_property(registry, camera_type,
                                                gneiss::meta::property_id{1}, target),
            preserved));
      }) ||
      std::get<std::string>(preserved.payload) != "preserved")
    return 16;
  value.payload = 120.0F;
  if (!gneiss::test_support::throws<std::bad_alloc>([&] {
        (void)(gneiss::meta::write_property(
            registry, camera_type, gneiss::meta::property_id{1}, target, value));
      }) ||
      fixture.field_of_view != 90.0F)
    return 17;
  const std::weak_ptr lifetime = registry;
  registry.reset();
  if (lifetime.expired() || info.type().scalar_kind() != gneiss::value_kind::float32)
    return 18;
  info = {};
  return lifetime.expired() &&
                 gneiss::test_support::assign_value(
                     gneiss::meta::read_property(
                         registry, camera_type, gneiss::meta::property_id{1}, target),
                     value) == error::invalid_handle &&
                 std::get<std::string>(snapshot->payload) == "主相机"
             ? 0
             : 14;
} catch (...) {
  return 99;
}
