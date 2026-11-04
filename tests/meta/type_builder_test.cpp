// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include "engine/core/meta/registration.hpp"
namespace e = gneiss::engine;
struct sample {
  inline static constexpr std::string_view type_name = "sample";
  inline static constexpr e::guid type_guid{e::guid::from_bytes({9, 8, 7})};
  float speed = 1;
  bool active = true;
  float get_speed() const noexcept { return speed; }
  e::error set_speed(float input) noexcept {
    if (input <= 0)
      return e::error::invalid_argument;
    speed = input;
    return e::error::success;
  }
};
int main() {
  gneiss::engine::meta::registry_ref registry;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::create_registry(), registry)
          .failed())
    return 1;
  if (e::type_identity<const sample&>::guid() != sample::type_guid ||
      e::type_identity<sample>::name() != "sample" ||
      e::type_identity<const float&>::guid() != e::type_identity<float>::guid())
    return 2;
  if (gneiss::engine::meta::register_type<sample>(
          registry, {.schema_version = 1, .name = "conflict"}) != e::error::invalid_argument)
    return 3;
  if (gneiss::engine::meta::type_builder<sample>{registry}
          .property<&sample::speed>(gneiss::engine::meta::property_id{1}, "speed")
          .readonly<&sample::active>(gneiss::engine::meta::property_id{1}, "active")
          .commit() != e::error::invalid_argument)
    return 4;
  // 前一次失败不得留下类型；名称由 builder 拥有。
  gneiss::engine::meta::type_builder<const sample&> builder{registry};
  {
    std::string name = "speed";
    builder.property<&sample::get_speed, &sample::set_speed>(gneiss::engine::meta::property_id{1},
                                                             name);
  }
  builder.readonly<&sample::active>(gneiss::engine::meta::property_id{2}, "active")
      .property<&sample::speed>(gneiss::engine::meta::property_id{3}, "raw_speed");
  if (builder.commit().failed() || builder.commit() != e::error::invalid_state ||
      gneiss::engine::meta::freeze(registry).failed())
    return 5;
  sample object;
  e::value output;
  if (gneiss::test_support::assign_value(
          gneiss::engine::meta::read_property(registry, sample::type_guid,
                                              gneiss::engine::meta::property_id{1}, &object),
          output)
          .failed() ||
      std::get<float>(output.payload) != 1)
    return 6;
  if (gneiss::engine::meta::write_property(registry, sample::type_guid,
                                           gneiss::engine::meta::property_id{1}, &object,
                                           e::value{2.0F})
          .failed() ||
      object.speed != 2)
    return 7;
  if (gneiss::engine::meta::write_property(registry, sample::type_guid,
                                           gneiss::engine::meta::property_id{1}, &object,
                                           e::value{-1.0F}) != e::error::invalid_argument ||
      object.speed != 2)
    return 8;
  if (gneiss::engine::meta::write_property(registry, sample::type_guid,
                                           gneiss::engine::meta::property_id{2}, &object,
                                           e::value{false}) == e::error::success ||
      !object.active)
    return 9;
  if (gneiss::engine::meta::write_property(registry, sample::type_guid,
                                           gneiss::engine::meta::property_id{3}, &object,
                                           e::value{3.0F})
          .failed() ||
      object.speed != 3)
    return 10;
  if (gneiss::engine::meta::write_property(registry, sample::type_guid,
                                           gneiss::engine::meta::property_id{1}, &object,
                                           e::value{true}) != e::error::invalid_argument ||
      object.speed != 3)
    return 11;
  return 0;
}
