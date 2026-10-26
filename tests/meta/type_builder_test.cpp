// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/type_builder.hpp"
namespace e = gneiss::engine;
struct sample {
  inline static constexpr std::string_view type_name = "sample";
  inline static constexpr e::guid type_guid{e::guid::from_bytes({9, 8, 7})};
  float speed = 1;
  bool active = true;
  float get_speed() const noexcept { return speed; }
  e::result set_speed(float input) noexcept {
    if (input <= 0)
      return e::result::invalid_argument;
    speed = input;
    return e::result::success;
  }
};
int main() {
  e::registry_ref registry;
  if (e::create_native(registry).failed())
    return 1;
  if (e::type_identity<const sample&>::guid() != sample::type_guid ||
      e::type_identity<sample>::name() != "sample" ||
      e::type_identity<const float&>::guid() != e::type_identity<float>::guid())
    return 2;
  if (e::register_type<sample>(registry, {.schema_version = 1, .name = "conflict"}) !=
      e::result::invalid_argument)
    return 3;
  if (e::type_builder<sample>{registry}
          .property<&sample::speed>(gneiss::field_id{1}, "speed")
          .readonly<&sample::active>(gneiss::field_id{1}, "active")
          .commit() != e::result::invalid_argument)
    return 4;
  // 前一次失败不得留下类型；名称由 builder 拥有。
  e::type_builder<const sample&> builder{registry};
  {
    std::string name = "speed";
    builder.property<&sample::get_speed, &sample::set_speed>(gneiss::field_id{1}, name);
  }
  builder.readonly<&sample::active>(gneiss::field_id{2}, "active")
      .property<&sample::speed>(gneiss::field_id{3}, "raw_speed");
  if (builder.commit().failed() || builder.commit() != e::result::invalid_state ||
      e::freeze(registry).failed())
    return 5;
  sample object;
  e::value output;
  if (e::read_property(registry, sample::type_guid, gneiss::field_id{1}, &object, output)
          .failed() ||
      std::get<float>(output.payload) != 1)
    return 6;
  if (e::write_property(registry, sample::type_guid, gneiss::field_id{1}, &object, e::value{2.0F})
          .failed() ||
      object.speed != 2)
    return 7;
  if (e::write_property(registry, sample::type_guid, gneiss::field_id{1}, &object,
                        e::value{-1.0F}) != e::result::invalid_argument ||
      object.speed != 2)
    return 8;
  if (e::write_property(registry, sample::type_guid, gneiss::field_id{2}, &object,
                        e::value{false}) == e::result::success ||
      !object.active)
    return 9;
  if (e::write_property(registry, sample::type_guid, gneiss::field_id{3}, &object, e::value{3.0F})
          .failed() ||
      object.speed != 3)
    return 10;
  if (e::write_property(registry, sample::type_guid, gneiss::field_id{1}, &object,
                        e::value{true}) != e::result::invalid_argument ||
      object.speed != 3)
    return 11;
  return 0;
}
