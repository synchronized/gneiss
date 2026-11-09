// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include "engine/core/math/camera.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <thread>

struct reflected_object {};
struct other_object {};
struct unregistered_object {};

static_assert(sizeof(gneiss::engine::meta::property) ==
              sizeof(gneiss::engine::meta::registry_ref) + sizeof(void*));

static_assert(sizeof(gneiss::engine::meta::type) ==
              sizeof(gneiss::engine::meta::registry_ref) + 2 * sizeof(void*));

static_assert(
    std::same_as<decltype(gneiss::engine::meta::type_of<int>()), gneiss::engine::meta::type>);

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
  gneiss::engine::meta::type info;
  if (!(info = gneiss::engine::meta::type_of<reflected_object>(registry)) ||
      info.guid() != e::guid{} ||
      gneiss::engine::meta::register_type<reflected_object>(registry, definition).failed())
    return false;
  const auto before_freeze = info;
  if (!before_freeze || before_freeze.registered() || !before_freeze.name().empty() ||
      gneiss::engine::meta::type{} || gneiss::engine::meta::type{}.registered())
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
  if (before_freeze.registered() || !before_freeze.name().empty())
    return false;
  std::uint32_t count = 0;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::type_count(registry), count)
          .failed() ||
      count != 3 || !(info = gneiss::engine::meta::type_of<const reflected_object&>(registry)) ||
      !info || !info.registered() || info.guid() != object_guid ||
      !(info = gneiss::engine::meta::find_type(registry, object_guid)) || info.name() != "Object" ||
      !(info = gneiss::engine::meta::type_of<other_object>(registry)) ||
      info.guid() != other_guid ||
      gneiss::test_support::assign_value(gneiss::engine::meta::type_at(registry, 1), info)
          .failed() ||
      !info.registered() || info.guid() != data_guid)
    return false;
  if (gneiss::engine::meta::find_type(registry, e::type_id{}) || info.guid() != data_guid ||
      !(info = gneiss::engine::meta::type_of<unregistered_object>(registry)) ||
      info.id() != e::runtime_type_of<unregistered_object>() || info.registered() || info.guid() ||
      gneiss::engine::meta::register_type<reflected_object>(registry, definition) !=
          error::invalid_state)
    return false;
  std::array<bool, 4> queried{};
  std::array<std::thread, 4> readers;
  for (std::size_t i = 0; i < readers.size(); ++i) {
    readers[i] = std::thread([&, i] {
      gneiss::engine::meta::type metadata;
      queried[i] = (metadata = gneiss::engine::meta::type_of<reflected_object>(registry)) &&
                   metadata.guid() == object_guid;
    });
  }
  for (auto& reader : readers)
    reader.join();
  for (bool success : queried)
    if (!success)
      return false;
  registry.reset();
  return (info = gneiss::engine::meta::type_of<reflected_object>(registry)) && !info.registered() &&
         info.id() == e::runtime_type_of<reflected_object>();
}

bool math_registration() {
  namespace e = gneiss::engine;
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry || m::register_builtin_types(*registry).failed() || m::freeze(*registry).failed())
    return false;
  auto transform = m::type_of<e::math::transform>(*registry);
  auto camera = m::type_of<e::math::camera_settings>(*registry);
  if (!transform || !camera || transform.properties().size() != 3 ||
      camera.properties().size() != 3)
    return false;
  e::serialization::write_context context{*registry};
  e::serialization::read_context read_context{*registry};
  e::math::camera_settings settings{1.0F, 0.1F, 100.0F};
  auto encoded = e::serialization::json::encode_bytes(context, settings);
  if (!encoded)
    return false;
  auto decoded = e::serialization::json::decode<e::math::camera_settings>(read_context, *encoded);
  if (!decoded || decoded->fov != settings.fov || decoded->near != settings.near ||
      decoded->far != settings.far)
    return false;
  const std::string invalid = R"({"fov":1,"near":10,"far":1})";
  auto unchecked = e::serialization::json::decode<e::math::camera_settings>(
      read_context, std::as_bytes(std::span{invalid.data(), invalid.size()}));
  e::math::transform value;
  auto status = m::write_property(*registry, e::math::transform::type_guid, m::property_id{3},
                                  &value, e::value{e::math::vector3{0, 1, 1}});
  // 通用解码只恢复字段；相机采用边界显式检查透视范围。
  return unchecked && !e::math::is_valid_camera(*unchecked) &&
         status == e::error::invalid_argument && value.scale.x == 1;
}
bool frozen_lookup() {
  namespace e = gneiss::engine;
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry)
    return false;
  constexpr auto id = e::guid::literal("ae3d9e6b-f246-4f8e-bb9d-5b6c38e1d402");
  std::vector<std::string> names;
  std::vector<m::property_definition> fields;
  names.reserve(128);
  fields.reserve(128);
  // 注册顺序、ID 顺序与名称顺序各不相同。
  for (std::uint32_t i = 0; i < 128; ++i) {
    names.push_back("field_" + std::to_string(i));
    fields.push_back(
        {m::property_id{(127 - i) * 3 + 1}, id, m::property_access::read_write, names.back()});
  }
  if (m::register_type(*registry,
                       {.guid = id, .schema_version = 1, .name = "indexed", .properties = fields})
          .failed() ||
      m::find_property(*registry, id, m::property_id{1}) || m::freeze(*registry).failed() ||
      m::freeze(*registry).failed())
    return false;
  auto type = m::find_type(*registry, id);
  if (!type)
    return false;
  for (const auto& field : fields) {
    auto by_id = type.find_property(field.id);
    auto by_name = type.find_property(field.name);
    if (!by_id || !by_name || by_id.name() != field.name || by_name.id() != field.id)
      return false;
  }
  return !type.find_property(m::property_id{2}) && !type.find_property(m::property_id{999}) &&
         !type.find_property("") && !type.find_property("field_128");
}
int main() {
  if (!frozen_lookup())
    return 32;
  if (!math_registration())
    return 30;
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
      gneiss::engine::meta::property_id{1}, scalar_type,
      gneiss::engine::meta::property_access::read_write, "value"};
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
  gneiss::engine::meta::type info;
  gneiss::engine::meta::property metadata;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::type_at(registry, 0), info)
          .failed() ||
      info.guid() != scalar_type ||
      !(info = gneiss::engine::meta::find_type(registry, object_type)) || info.name() != "Object" ||
      info.schema_version() != 1 || info.properties().size() != 1 ||
      !(metadata = gneiss::engine::meta::find_property(registry, object_type,
                                                       gneiss::engine::meta::property_id{1})) ||
      metadata.name() != "value" ||
      gneiss::engine::meta::find_property(registry, object_type,
                                          gneiss::engine::meta::property_id{2}))
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
  if (lifetime.expired() || metadata.name() != "value")
    return 12;
  metadata = {};
  info = {};
  return lifetime.expired() &&
                 gneiss::test_support::assign_value(gneiss::engine::meta::type_count(registry),
                                                    count) == error::invalid_handle
             ? 0
             : 11;
}
