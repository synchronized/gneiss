// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/reflection/type_registry.hpp"
#include <array>
#include <cstdint>
#include <string>

int main() {
  namespace engine = gneiss::engine;
  using engine::result;
  constexpr engine::type_guid object_type{engine::guid::from_bytes({0x20})};
  constexpr engine::type_guid scalar_type{engine::guid::from_bytes({0x10})};
  const engine::field_definition field{1, scalar_type, 0, "value"};
  const std::array duplicate_fields{field, field};
  engine::type_definition object{object_type, 1, "Object", {&field, 1}};
  engine::type_definition scalar{scalar_type, 1, "Scalar", {}};
  engine::registry_ref registry;
  bool frozen = true;
  std::uint32_t count = 99;
  if (engine::create_native(registry).failed() || engine::is_frozen(registry, frozen).failed() ||
      frozen || engine::type_count(registry, count) != result::not_ready || count != 99 ||
      engine::register_type(registry, object).failed() ||
      engine::register_type(registry, scalar).failed() ||
      engine::register_type(registry, object).failed())
    return 1;
  auto invalid = object;
  invalid.fields = duplicate_fields;
  if (engine::register_type(registry, invalid) != result::invalid_argument)
    return 2;
  invalid = object;
  invalid.schema_version = 2;
  if (engine::register_type(registry, invalid) != result::invalid_argument)
    return 3;
  invalid = scalar;
  invalid.name = "\xC0\xAF";
  if (engine::register_type(registry, invalid) != result::invalid_argument)
    return 4;
  invalid.id = {};
  invalid.name = "nil";
  if (engine::register_type(registry, invalid) != result::invalid_argument)
    return 5;
  std::int64_t stored = 2;
  const engine::property_binding binding{
      .kind = gneiss::property_kind::int64,
      .getter = [](void* data, gneiss::property_target target,
                   gneiss::property_value* output) -> result {
        if (target.context != 3 || target.object != 5)
          return result::not_found;
        output->payload = *static_cast<std::int64_t*>(data);
        return result::success;
      },
      .setter = [](void* data, gneiss::property_target target,
                   const gneiss::property_value* value) -> result {
        if (target.context != 3 || target.object != 5)
          return result::not_found;
        const auto next = std::get<std::int64_t>(value->payload);
        if (next < -10 || next > 10)
          return result::invalid_argument;
        *static_cast<std::int64_t*>(data) = next;
        return result::success;
      },
      .user_data = &stored};
  if (engine::bind_property(registry, object_type, 1, binding).failed() ||
      engine::freeze(registry).failed() || engine::freeze(registry).failed() ||
      engine::is_frozen(registry, frozen).failed() || !frozen ||
      engine::register_type(registry, object) != result::invalid_state ||
      engine::type_count(registry, count).failed() || count != 2)
    return 6;
  gneiss::type_info info;
  gneiss::field_info metadata;
  if (engine::describe_type_at(registry, 0, info).failed() || info.id != scalar_type ||
      engine::describe_type(registry, object_type, info).failed() || info.name != "Object" ||
      info.schema_version != 1 || info.fields.size() != 1 ||
      engine::describe_field(registry, object_type, gneiss::field_id{1}, metadata).failed() ||
      metadata.name != "value" ||
      engine::describe_field(registry, object_type, gneiss::field_id{2}, metadata) !=
          result::not_found)
    return 7;
  gneiss::property_value value;
  if (engine::get_property(registry, object_type, 1, {3, 5}, value).failed() ||
      std::get<std::int64_t>(value.payload) != 2)
    return 8;
  value.payload = std::int64_t{7};
  if (engine::set_property(registry, object_type, 1, {3, 5}, value).failed() || stored != 7)
    return 9;
  value.payload = std::int64_t{11};
  if (engine::set_property(registry, object_type, 1, {3, 5}, value) != result::invalid_argument ||
      stored != 7)
    return 10;
  const std::weak_ptr lifetime = registry;
  registry.reset();
  return lifetime.expired() && engine::type_count(registry, count) == result::invalid_handle ? 0
                                                                                             : 11;
}
