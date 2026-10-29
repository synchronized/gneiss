// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include "engine/core/meta/registration.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <thread>

struct reflected_object {};
struct other_object {};

bool typed_registration() {
  namespace e = gneiss::engine;
  using e::error;
  gneiss::engine::meta::registry_ref registry;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::create_registry(), registry)
          .failed())
    return false;
  const e::guid object_guid{e::guid::from_bytes({0x30})};
  const e::guid other_guid{e::guid::from_bytes({0x10})};
  const e::guid data_guid{e::guid::from_bytes({0x20})};
  const gneiss::engine::meta::type_definition definition{
      .guid = object_guid, .schema_version = 1, .name = "Object", .properties = {}};
  gneiss::engine::meta::type_info info{.guid = data_guid};
  if (gneiss::test_support::assign_value(
          gneiss::engine::meta::describe_type<reflected_object>(registry), info) !=
          error::not_ready ||
      info.guid != data_guid ||
      gneiss::engine::meta::register_type<reflected_object>(registry, definition).failed())
    return false;
  auto alternate = definition;
  alternate.guid = other_guid;
  if (gneiss::engine::meta::register_type<const reflected_object&>(registry, alternate) !=
          error::invalid_argument ||
      gneiss::engine::meta::register_type<other_object>(registry, definition) !=
          error::invalid_argument ||
      gneiss::engine::meta::register_type<reflected_object>(registry, definition) !=
          error::invalid_argument ||
      gneiss::engine::meta::register_type<void>(registry, alternate) != error::invalid_argument)
    return false;
  alternate.type_id = e::runtime_type_of<reflected_object>();
  if (gneiss::engine::meta::register_type<other_object>(registry, alternate) !=
      error::invalid_argument)
    return false;
  alternate.type_id = {};
  if (gneiss::engine::meta::register_type<other_object>(registry, alternate).failed())
    return false;
  alternate.guid = data_guid;
  if (gneiss::engine::meta::register_type(registry, alternate).failed() ||
      gneiss::engine::meta::freeze(registry).failed())
    return false;
  std::uint32_t count = 0;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::type_count(registry), count)
          .failed() ||
      count != 3 ||
      gneiss::test_support::assign_value(
          gneiss::engine::meta::describe_type<const reflected_object&>(registry), info)
          .failed() ||
      info.guid != object_guid ||
      gneiss::test_support::assign_value(gneiss::engine::meta::describe_type(registry, object_guid),
                                         info)
          .failed() ||
      info.name != "Object" ||
      gneiss::test_support::assign_value(
          gneiss::engine::meta::describe_type<other_object>(registry), info)
          .failed() ||
      info.guid != other_guid ||
      gneiss::test_support::assign_value(gneiss::engine::meta::describe_type_at(registry, 1), info)
          .failed() ||
      info.guid != data_guid)
    return false;
  if (gneiss::test_support::assign_value(
          gneiss::engine::meta::describe_type(registry, e::type_id{}), info) != error::not_found ||
      info.guid != data_guid ||
      gneiss::test_support::assign_value(gneiss::engine::meta::describe_type<double>(registry),
                                         info) != error::not_found ||
      info.guid != data_guid ||
      gneiss::engine::meta::register_type<reflected_object>(registry, definition) !=
          error::invalid_state)
    return false;
  std::array<bool, 4> queried{};
  std::array<std::thread, 4> readers;
  for (std::size_t i = 0; i < readers.size(); ++i) {
    readers[i] = std::thread([&, i] {
      gneiss::engine::meta::type_info metadata;
      queried[i] = gneiss::test_support::assign_value(
                       gneiss::engine::meta::describe_type<reflected_object>(registry), metadata) ==
                       error::success &&
                   metadata.guid == object_guid;
    });
  }
  for (auto& reader : readers)
    reader.join();
  for (bool success : queried)
    if (!success)
      return false;
  registry.reset();
  return gneiss::test_support::assign_value(
             gneiss::engine::meta::describe_type<reflected_object>(registry), info) ==
             error::invalid_handle &&
         info.guid == data_guid;
}

int main() {
  auto empty = gneiss::engine::meta::create_registry();
  if (!empty)
    return 30;
  auto unfrozen = gneiss::engine::meta::is_frozen(*empty);
  auto unavailable = gneiss::engine::meta::type_count(*empty);
  auto invalid_registry = gneiss::engine::meta::is_frozen({});
  if (!unfrozen || *unfrozen || unavailable ||
      unavailable.error() != gneiss::engine::error::not_ready || invalid_registry ||
      invalid_registry.error() != gneiss::engine::error::invalid_handle)
    return 31;

  if (!typed_registration())
    return 20;
  namespace engine = gneiss::engine;
  using engine::error;
  constexpr engine::guid object_type{engine::guid::from_bytes({0x20})};
  constexpr engine::guid scalar_type{engine::guid::from_bytes({0x10})};
  const gneiss::engine::meta::property_definition field{
      gneiss::engine::meta::property_id{1}, scalar_type, gneiss::engine::meta::property_flags::none,
      "value"};
  const std::array duplicate_fields{field, field};
  gneiss::engine::meta::type_definition object{object_type, {}, 1, "Object", {&field, 1}};
  gneiss::engine::meta::type_definition scalar{scalar_type, {}, 1, "Scalar", {}};
  gneiss::engine::meta::registry_ref registry;
  bool frozen = true;
  std::uint32_t count = 99;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::create_registry(), registry)
          .failed() ||
      gneiss::test_support::assign_value(gneiss::engine::meta::is_frozen(registry), frozen)
          .failed() ||
      frozen ||
      gneiss::test_support::assign_value(gneiss::engine::meta::type_count(registry), count) !=
          error::not_ready ||
      count != 99 || gneiss::engine::meta::register_type(registry, object).failed() ||
      gneiss::engine::meta::register_type(registry, scalar).failed() ||
      gneiss::engine::meta::register_type(registry, object) != error::invalid_argument)
    return 1;
  auto invalid = object;
  invalid.properties = duplicate_fields;
  if (gneiss::engine::meta::register_type(registry, invalid) != error::invalid_argument)
    return 2;
  invalid = object;
  invalid.schema_version = 2;
  if (gneiss::engine::meta::register_type(registry, invalid) != error::invalid_argument)
    return 3;
  invalid = scalar;
  invalid.name = "\xC0\xAF";
  if (gneiss::engine::meta::register_type(registry, invalid) != error::invalid_argument)
    return 4;
  invalid.guid = {};
  invalid.name = "nil";
  if (gneiss::engine::meta::register_type(registry, invalid) != error::invalid_argument)
    return 5;
  std::int64_t stored = 2;
  const gneiss::engine::meta::property_binding binding{
      .kind = gneiss::engine::value_kind::int64,
      .getter = [](void*, const void* object, gneiss::engine::value& output) -> error {
        output.payload = *static_cast<const std::int64_t*>(object);
        return error::success;
      },
      .setter = [](void*, void* object, const gneiss::engine::value& value) -> error {
        const auto next = std::get<std::int64_t>(value.payload);
        if (next < -10 || next > 10)
          return error::invalid_argument;
        *static_cast<std::int64_t*>(object) = next;
        return error::success;
      },
      .user_data = nullptr};
  if (gneiss::engine::meta::bind_property(registry, object_type,
                                          gneiss::engine::meta::property_id{1}, binding)
          .failed() ||
      gneiss::engine::meta::freeze(registry).failed() ||
      gneiss::engine::meta::freeze(registry).failed() ||
      gneiss::test_support::assign_value(gneiss::engine::meta::is_frozen(registry), frozen)
          .failed() ||
      !frozen || gneiss::engine::meta::register_type(registry, object) != error::invalid_state ||
      gneiss::test_support::assign_value(gneiss::engine::meta::type_count(registry), count)
          .failed() ||
      count != 2)
    return 6;
  gneiss::engine::meta::type_info info;
  gneiss::engine::meta::property_info metadata;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::describe_type_at(registry, 0), info)
          .failed() ||
      info.guid != scalar_type ||
      gneiss::test_support::assign_value(gneiss::engine::meta::describe_type(registry, object_type),
                                         info)
          .failed() ||
      info.name != "Object" || info.schema_version != 1 || info.properties.size() != 1 ||
      gneiss::test_support::assign_value(
          gneiss::engine::meta::describe_property(registry, object_type,
                                                  gneiss::engine::meta::property_id{1}),
          metadata)
          .failed() ||
      metadata.name != "value" ||
      gneiss::test_support::assign_value(
          gneiss::engine::meta::describe_property(registry, object_type,
                                                  gneiss::engine::meta::property_id{2}),
          metadata) != error::not_found)
    return 7;
  gneiss::engine::value value;
  if (gneiss::test_support::assign_value(
          gneiss::engine::meta::read_property(registry, object_type,
                                              gneiss::engine::meta::property_id{1}, &stored),
          value)
          .failed() ||
      std::get<std::int64_t>(value.payload) != 2)
    return 8;
  value.payload = std::int64_t{7};
  if (gneiss::engine::meta::write_property(registry, object_type,
                                           gneiss::engine::meta::property_id{1}, &stored, value)
          .failed() ||
      stored != 7)
    return 9;
  value.payload = std::int64_t{11};
  if (gneiss::engine::meta::write_property(registry, object_type,
                                           gneiss::engine::meta::property_id{1}, &stored,
                                           value) != error::invalid_argument ||
      stored != 7)
    return 10;
  const std::weak_ptr lifetime = registry;
  registry.reset();
  return lifetime.expired() &&
                 gneiss::test_support::assign_value(gneiss::engine::meta::type_count(registry),
                                                    count) == error::invalid_handle
             ? 0
             : 11;
}
