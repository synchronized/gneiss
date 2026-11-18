// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/throws.hpp"
#include "../../support/value_result.hpp"
#include <array>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
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
      gneiss::meta::property_definition{gneiss::meta::property_id{4}, float_type,
                                                gneiss::meta::property_access::read_write,
                                                "malformed"}};
  auto registry = gneiss::meta::create_registry();
  const gneiss::meta::property_binding field_of_view{
      .kind = gneiss::value_kind::float32,
      .getter = get_field_of_view,
      .setter = set_field_of_view,
      .user_data = nullptr};
  const gneiss::meta::property_binding label{.kind = gneiss::value_kind::string,
                                                     .getter = get_label,
                                                     .setter = {},
                                                     .user_data = nullptr};
  std::array bindings{
      field_of_view, label,
      gneiss::meta::property_binding{.kind = gneiss::value_kind::float32, .getter = get_malformed}};
  bindings[1].setter = set_field_of_view;
  if (gneiss::meta::register_type(registry, {camera_type, {}, 1, "camera", fields}, bindings) !=
      error::invalid_argument)
    return 2;
  bindings[1] = label;
  // 上次提交失败不留下部分类型；相同身份可以完整重试。
  if (gneiss::meta::register_type(registry, {camera_type, {}, 1, "camera", fields}, bindings)
          .failed() ||
      gneiss::meta::freeze(registry).failed() ||
      gneiss::meta::register_type(registry, {camera_type, {}, 1, "camera", fields}, bindings) !=
          error::invalid_state)
    return 2;
  const std::weak_ptr lifetime = registry;
  gneiss::value snapshot;
  {
    auto root = gneiss::meta::instance::from_object(gneiss::meta::find_type(registry, camera_type),
                                                    &fixture);
    if (!root)
      return 3;
    auto object = root->capability<gneiss::meta::cap::object>();
    if (!object)
      return 4;
    auto fov = object->member("field_of_view");
    auto readonly = root->as_const().capability<gneiss::meta::cap::object>();
    auto label = readonly->member("label");
    auto malformed = readonly->member("malformed");
    if (!fov || !label || !malformed)
      return 5;
    if (mode == "empty-source") {
      (void)fov->assign(gneiss::meta::const_instance{});
      return 90;
    }
    if (mode == "wrong-kind") {
      fixture.wrong_kind = true;
      (void)fov->read_value();
      return 90;
    }
    if (gneiss::meta::instance::from_object(root->type(), nullptr).error() !=
            error::invalid_argument ||
        std::get<float>(fov->read_value()->payload) != 60.0F ||
        fov->assign(gneiss::value{90.0F}).failed() || fixture.field_of_view != 90.0F ||
        fov->assign(gneiss::value{-1.0F}) != error::invalid_argument ||
        fov->assign(gneiss::value{std::uint64_t{90}}) != error::invalid_argument ||
        fixture.field_of_view != 90.0F || object->member("label").error() != error::unsupported ||
        readonly->member("unbound").error() != error::not_found ||
        malformed->read_value().error() != error::invalid_argument)
      return 6;
    // 同一字段描述可以绑定不同宿主；字段实例不把宿主写入共享访问器。
    camera_fixture another;
    another.field_of_view = 45.0F;
    auto other_root = gneiss::meta::instance::from_object(root->type(), &another);
    auto other_field = other_root->capability<gneiss::meta::cap::object>()->member("field_of_view");
    if (!other_field || std::get<float>(other_field->read_value()->payload) != 45.0F ||
        other_field->assign(gneiss::value{75.0F}).failed() || another.field_of_view != 75.0F ||
        fixture.field_of_view != 90.0F)
      return 7;
    const float invalid_scalar = std::numeric_limits<float>::infinity();
    auto scalar = gneiss::meta::view(registry, invalid_scalar);
    if (!scalar || scalar->read_value().error() != error::invalid_argument)
      return 8;
    fixture.read_error = error::not_ready;
    if (fov->read_value().error() != error::not_ready)
      return 9;
    fixture.read_error = error::success;
    fixture.fail_get = true;
    if (!gneiss::test_support::throws<std::runtime_error>([&] { (void)fov->read_value(); }))
      return 10;
    fixture.fail_get = false;
    fixture.fail_set = true;
    if (!gneiss::test_support::throws<std::runtime_error>(
            [&] { (void)fov->assign(gneiss::value{100.0F}); }) ||
        fixture.field_of_view != 90.0F)
      return 11;
    fixture.fail_set = false;
    auto saved = label->read_value();
    if (!saved)
      return 12;
    snapshot = *saved;
    fixture.label = "changed";
    if (std::get<std::string>(snapshot.payload) != "主相机" ||
        std::get<std::string>(label->read_value()->payload) != "changed")
      return 13;
    fixture.fail_allocate = true;
    if (!gneiss::test_support::throws<std::bad_alloc>([&] { (void)fov->read_value(); }) ||
        !gneiss::test_support::throws<std::bad_alloc>(
            [&] { (void)fov->assign(gneiss::value{120.0F}); }) ||
        fixture.field_of_view != 90.0F)
      return 14;
    fixture.fail_allocate = false;
    // 注册表由字段实例保活；原始表引用释放后，绑定仍可读写。
    registry.reset();
    if (lifetime.expired() || std::get<float>(fov->read_value()->payload) != 90.0F ||
        fov->assign(gneiss::value{100.0F}).failed() || fixture.field_of_view != 100.0F)
      return 15;
  }
  return lifetime.expired() && std::get<std::string>(snapshot.payload) == "主相机" ? 0 : 16;

} catch (...) {
  return 99;
}
