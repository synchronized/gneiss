// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/reflection/type_registry.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <thread>

struct reflected_object {};
struct other_object {};

bool typed_registration() {
  namespace e = gneiss::engine;
  using e::result;
  e::registry_ref registry;
  if (e::create_native(registry).failed())
    return false;
  const e::guid object_guid{e::guid::from_bytes({0x30})};
  const e::guid other_guid{e::guid::from_bytes({0x10})};
  const e::guid data_guid{e::guid::from_bytes({0x20})};
  const e::type_definition definition{
      .guid = object_guid, .schema_version = 1, .name = "Object", .fields = {}};
  gneiss::type_info info{.guid = data_guid};
  if (e::describe_type<reflected_object>(registry, info) != result::not_ready ||
      info.guid != data_guid || e::register_type<reflected_object>(registry, definition).failed())
    return false;
  auto alternate = definition;
  alternate.guid = other_guid;
  if (e::register_type<const reflected_object&>(registry, alternate) != result::invalid_argument ||
      e::register_type<other_object>(registry, definition) != result::invalid_argument ||
      e::register_type<reflected_object>(registry, definition) != result::invalid_argument ||
      e::register_type<void>(registry, alternate) != result::invalid_argument)
    return false;
  alternate.type_id = e::runtime_type_of<reflected_object>();
  if (e::register_type<other_object>(registry, alternate) != result::invalid_argument)
    return false;
  alternate.type_id = {};
  if (e::register_type<other_object>(registry, alternate).failed())
    return false;
  alternate.guid = data_guid;
  if (e::register_type(registry, alternate).failed() || e::freeze(registry).failed())
    return false;
  std::uint32_t count = 0;
  if (e::type_count(registry, count).failed() || count != 3 ||
      e::describe_type<const reflected_object&>(registry, info).failed() ||
      info.guid != object_guid || e::describe_type(registry, object_guid, info).failed() ||
      info.name != "Object" || e::describe_type<other_object>(registry, info).failed() ||
      info.guid != other_guid || e::describe_type_at(registry, 1, info).failed() ||
      info.guid != data_guid)
    return false;
  if (e::describe_type(registry, e::type_id{}, info) != result::not_found ||
      info.guid != data_guid || e::describe_type<double>(registry, info) != result::not_found ||
      info.guid != data_guid ||
      e::register_type<reflected_object>(registry, definition) != result::invalid_state)
    return false;
  std::array<bool, 4> queried{};
  std::array<std::thread, 4> readers;
  for (std::size_t i = 0; i < readers.size(); ++i) {
    readers[i] = std::thread([&, i] {
      gneiss::type_info metadata;
      queried[i] = e::describe_type<reflected_object>(registry, metadata) == result::success &&
                   metadata.guid == object_guid;
    });
  }
  for (auto& reader : readers)
    reader.join();
  for (bool success : queried)
    if (!success)
      return false;
  registry.reset();
  return e::describe_type<reflected_object>(registry, info) == result::invalid_handle &&
         info.guid == data_guid;
}

int main() {
  if (!typed_registration())
    return 20;
  namespace engine = gneiss::engine;
  using engine::result;
  constexpr engine::guid object_type{engine::guid::from_bytes({0x20})};
  constexpr engine::guid scalar_type{engine::guid::from_bytes({0x10})};
  const engine::field_definition field{gneiss::field_id{1}, scalar_type, gneiss::field_flags::none,
                                       "value"};
  const std::array duplicate_fields{field, field};
  engine::type_definition object{object_type, {}, 1, "Object", {&field, 1}};
  engine::type_definition scalar{scalar_type, {}, 1, "Scalar", {}};
  engine::registry_ref registry;
  bool frozen = true;
  std::uint32_t count = 99;
  if (engine::create_native(registry).failed() || engine::is_frozen(registry, frozen).failed() ||
      frozen || engine::type_count(registry, count) != result::not_ready || count != 99 ||
      engine::register_type(registry, object).failed() ||
      engine::register_type(registry, scalar).failed() ||
      engine::register_type(registry, object) != result::invalid_argument)
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
  invalid.guid = {};
  invalid.name = "nil";
  if (engine::register_type(registry, invalid) != result::invalid_argument)
    return 5;
  std::int64_t stored = 2;
  const engine::property_binding binding{
      .kind = gneiss::engine::value_kind::int64,
      .getter = [](void*, const void* object, gneiss::engine::value& output) -> result {
        output.payload = *static_cast<const std::int64_t*>(object);
        return result::success;
      },
      .setter = [](void*, void* object, const gneiss::engine::value& value) -> result {
        const auto next = std::get<std::int64_t>(value.payload);
        if (next < -10 || next > 10)
          return result::invalid_argument;
        *static_cast<std::int64_t*>(object) = next;
        return result::success;
      },
      .user_data = nullptr};
  if (engine::bind_property(registry, object_type, gneiss::field_id{1}, binding).failed() ||
      engine::freeze(registry).failed() || engine::freeze(registry).failed() ||
      engine::is_frozen(registry, frozen).failed() || !frozen ||
      engine::register_type(registry, object) != result::invalid_state ||
      engine::type_count(registry, count).failed() || count != 2)
    return 6;
  gneiss::type_info info;
  gneiss::field_info metadata;
  if (engine::describe_type_at(registry, 0, info).failed() || info.guid != scalar_type ||
      engine::describe_type(registry, object_type, info).failed() || info.name != "Object" ||
      info.schema_version != 1 || info.fields.size() != 1 ||
      engine::describe_field(registry, object_type, gneiss::field_id{1}, metadata).failed() ||
      metadata.name != "value" ||
      engine::describe_field(registry, object_type, gneiss::field_id{2}, metadata) !=
          result::not_found)
    return 7;
  gneiss::engine::value value;
  if (engine::get_property(registry, object_type, 1, &stored, value).failed() ||
      std::get<std::int64_t>(value.payload) != 2)
    return 8;
  value.payload = std::int64_t{7};
  if (engine::set_property(registry, object_type, 1, &stored, value).failed() || stored != 7)
    return 9;
  value.payload = std::int64_t{11};
  if (engine::set_property(registry, object_type, 1, &stored, value) != result::invalid_argument ||
      stored != 7)
    return 10;
  const std::weak_ptr lifetime = registry;
  registry.reset();
  return lifetime.expired() && engine::type_count(registry, count) == result::invalid_handle ? 0
                                                                                             : 11;
}
