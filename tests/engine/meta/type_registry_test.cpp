// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/throws.hpp"
#include "../../support/value_result.hpp"
#include <gneiss/engine/core/math/camera.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/builtin_registration.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

struct reflected_object {};
struct other_object {};
struct unregistered_object {};

static_assert(sizeof(gneiss::meta::property) ==
              sizeof(gneiss::meta::registry_ref) + sizeof(void*));

static_assert(sizeof(gneiss::meta::type) ==
              sizeof(gneiss::meta::registry_ref) + 2 * sizeof(void*));

static_assert(
    std::same_as<decltype(gneiss::meta::type_of<int>()), gneiss::meta::type>);

// 默认表可能首次分配；查询可能调用自定义能力，不应提前 terminate。
static_assert(!noexcept(gneiss::meta::default_registry()));
static_assert(!noexcept(gneiss::meta::type_of<reflected_object>()));
static_assert(!noexcept(gneiss::meta::registration::type<reflected_object>()));
static_assert(noexcept(gneiss::meta::type{}.id()));
static_assert(noexcept(gneiss::meta::type{}.name()));

bool typed_registration() {
  namespace e = gneiss;
  using e::error;
  gneiss::meta::registry_ref registry;
  if (gneiss::test_support::assign_value(gneiss::meta::create_registry(), registry)
          .failed())
    return false;
  const e::guid object_guid{e::guid::from_bytes({0x30})};
  const e::guid other_guid{e::guid::from_bytes({0x10})};
  const e::guid data_guid{e::guid::from_bytes({0x20})};
  const gneiss::meta::type_definition definition{
      .guid = object_guid, .schema_version = 1, .name = "Object", .properties = {}};
  gneiss::meta::type info;
  if (!(info = gneiss::meta::type_of<reflected_object>(registry)) ||
      info.guid() != e::guid{} ||
      gneiss::meta::register_type<reflected_object>(registry, definition).failed())
    return false;
  const auto before_freeze = info;
  if (!before_freeze || before_freeze.registered() || !before_freeze.name().empty() ||
      gneiss::meta::type{} || gneiss::meta::type{}.registered())
    return false;
  auto alternate = definition;
  alternate.guid = other_guid;
  if (gneiss::meta::register_type<const reflected_object&>(registry, alternate) !=
          error::invalid_argument ||
      gneiss::meta::register_type<other_object>(registry, definition) !=
          error::invalid_argument ||
      gneiss::meta::register_type<reflected_object>(registry, definition) !=
          error::invalid_argument ||
      gneiss::meta::register_type<void>(registry, alternate) != error::invalid_argument)
    return false;
  alternate.type_id = e::runtime_type_of<reflected_object>();
  if (gneiss::meta::register_type<other_object>(registry, alternate) !=
      error::invalid_argument)
    return false;
  alternate.type_id = {};
  if (gneiss::meta::register_type<other_object>(registry, alternate).failed())
    return false;
  alternate.guid = data_guid;
  if (gneiss::meta::register_type(registry, alternate).failed() ||
      gneiss::meta::freeze(registry).failed())
    return false;
  if (before_freeze.registered() || !before_freeze.name().empty())
    return false;
  std::uint32_t count = 0;
  if (gneiss::test_support::assign_value(gneiss::meta::type_count(registry), count)
          .failed() ||
      count != 3 || !(info = gneiss::meta::type_of<const reflected_object&>(registry)) ||
      !info || !info.registered() || info.guid() != object_guid ||
      !(info = gneiss::meta::find_type(registry, object_guid)) || info.name() != "Object" ||
      !(info = gneiss::meta::type_of<other_object>(registry)) ||
      info.guid() != other_guid ||
      gneiss::test_support::assign_value(gneiss::meta::type_at(registry, 1), info)
          .failed() ||
      !info.registered() || info.guid() != data_guid)
    return false;
  if (gneiss::meta::find_type(registry, e::type_id{}) || info.guid() != data_guid ||
      !(info = gneiss::meta::type_of<unregistered_object>(registry)) ||
      info.id() != e::runtime_type_of<unregistered_object>() || info.registered() || info.guid() ||
      gneiss::meta::register_type<reflected_object>(registry, definition) !=
          error::invalid_state)
    return false;
  std::array<bool, 4> queried{};
  std::array<std::thread, 4> readers;
  for (std::size_t i = 0; i < readers.size(); ++i) {
    readers[i] = std::thread([&, i] {
      gneiss::meta::type metadata;
      queried[i] = (metadata = gneiss::meta::type_of<reflected_object>(registry)) &&
                   metadata.guid() == object_guid;
    });
  }
  for (auto& reader : readers)
    reader.join();
  for (bool success : queried)
    if (!success)
      return false;
  registry.reset();
  return (info = gneiss::meta::type_of<reflected_object>(registry)) && !info.registered() &&
         info.id() == e::runtime_type_of<reflected_object>();
}

bool math_registration() {
  namespace e = gneiss;
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry || gneiss::serialization::register_builtin_types(*registry).failed() ||
      m::freeze(*registry).failed())
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
  if (!decoded || decoded->fov != settings.fov || decoded->near_plane != settings.near_plane ||
      decoded->far_plane != settings.far_plane)
    return false;
  const std::string invalid = R"({"fov":1,"near":10,"far":1})";
  auto unchecked = e::serialization::json::decode<e::math::camera_settings>(
      read_context, std::as_bytes(std::span{invalid.data(), invalid.size()}));
  e::math::transform value;
  auto status = m::write_property(*registry, e::math::transform::type_guid, m::property_id{3},
                                  &value, e::value{e::math::vector3{0, 1, 1}});
  // 通用解码只恢复字段；相机采用边界显式检查透视范围。
  return unchecked && !unchecked->valid() &&
         status == e::error::invalid_argument && value.scale.x == 1;
}
bool frozen_lookup() {
  namespace e = gneiss;
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
// 冻结时扩展能力查询抛异常，不能伪装为普通注册错误；临时状态不发布。
bool freeze_exception_contract() {
  namespace e = gneiss;
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry)
    return false;
  static bool fail = true;
  static const m::sequence_ops sequence{
      .element = []() -> const m::type_ops& {
        if (fail)
          throw std::runtime_error{"type capability"};
        return m::detail::ops_of<float>();
      },
      .size = [](const void*) -> std::size_t { return 0; },
      .at = [](const void*, std::size_t) -> const void* { return nullptr; }};
  static const m::type_ops access{.id = e::runtime_type_of<std::vector<float>>(),
                                  .category = &sequence};
  const m::property_definition field{.id = m::property_id{1}, .name = "values"};
  const m::type_definition definition{.guid = e::guid::from_bytes({0x79}),
                                      .schema_version = 1,
                                      .name = "freeze_failure",
                                      .properties = {&field, 1}};
  const m::property_binding binding{.object_type = access.id,
                                    .object_getter = [](const void* p) { return p; },
                                    .access = &access};
  if (m::register_type(*registry, definition, {&binding, 1}).failed())
    return false;
  if (!gneiss::test_support::throws<std::runtime_error>([&] { (void)m::freeze(*registry); }))
    return false;
  if (*m::is_frozen(*registry))
    return false;
  fail = false;
  if (m::freeze(*registry).failed() || !*m::is_frozen(*registry))
    return false;
  fail = true;
  // 查询自定义元素能力时抛异常不能变成“未找到”，也不能在视图 noexcept 内终止。
  auto property = m::find_type(*registry, definition.guid).find_property(field.id);
  if (!property || !gneiss::test_support::throws<std::runtime_error>([&] {
        (void)property.type().element_type();
      }) ||
      !gneiss::test_support::throws<std::runtime_error>(
          [&] { (void)m::find_type(*registry, e::runtime_type_of<unregistered_object>()); }))
    return false;
  fail = false;
  return property.type().element_type() &&
         !m::find_type(*registry, e::runtime_type_of<unregistered_object>());
}
int main() {
  if (!freeze_exception_contract())
    return 33;
  if (!frozen_lookup())
    return 32;
  if (!math_registration())
    return 30;
  auto empty = gneiss::meta::create_registry();
  if (!empty)
    return 30;
  auto unfrozen = gneiss::meta::is_frozen(*empty);
  auto unavailable = gneiss::meta::type_count(*empty);
  auto invalid_registry = gneiss::meta::is_frozen({});
  if (!unfrozen || *unfrozen || unavailable ||
      unavailable.error() != gneiss::error::not_ready || invalid_registry ||
      invalid_registry.error() != gneiss::error::invalid_handle)
    return 31;

  if (!typed_registration())
    return 20;

  using gneiss::error;
  constexpr gneiss::guid object_type{gneiss::guid::from_bytes({0x20})};
  constexpr gneiss::guid scalar_type{gneiss::guid::from_bytes({0x10})};
  const gneiss::meta::property_definition field{
      gneiss::meta::property_id{1}, scalar_type,
      gneiss::meta::property_access::read_write, "value"};
  const std::array duplicate_fields{field, field};
  gneiss::meta::type_definition object{object_type, {}, 1, "Object", {&field, 1}};
  gneiss::meta::type_definition scalar{scalar_type, {}, 1, "Scalar", {}};
  gneiss::meta::registry_ref registry;
  bool frozen = true;
  std::uint32_t count = 99;
  if (gneiss::test_support::assign_value(gneiss::meta::create_registry(), registry)
          .failed() ||
      gneiss::test_support::assign_value(gneiss::meta::is_frozen(registry), frozen)
          .failed() ||
      frozen ||
      gneiss::test_support::assign_value(gneiss::meta::type_count(registry), count) !=
          error::not_ready ||
      count != 99 || gneiss::meta::register_type(registry, object).failed() ||
      gneiss::meta::register_type(registry, scalar).failed() ||
      gneiss::meta::register_type(registry, object) != error::invalid_argument)
    return 1;
  auto invalid = object;
  invalid.properties = duplicate_fields;
  if (gneiss::meta::register_type(registry, invalid) != error::invalid_argument)
    return 2;
  invalid = object;
  invalid.schema_version = 2;
  if (gneiss::meta::register_type(registry, invalid) != error::invalid_argument)
    return 3;
  invalid = scalar;
  invalid.name = "\xC0\xAF";
  if (gneiss::meta::register_type(registry, invalid) != error::invalid_argument)
    return 4;
  invalid.guid = {};
  invalid.name = "nil";
  if (gneiss::meta::register_type(registry, invalid) != error::invalid_argument)
    return 5;
  std::int64_t stored = 2;
  const gneiss::meta::property_binding binding{
      .kind = gneiss::value_kind::int64,
      .getter = [](void*, const void* object, gneiss::value& output) -> error {
        output.payload = *static_cast<const std::int64_t*>(object);
        return error::success;
      },
      .setter = [](void*, void* object, const gneiss::value& value) -> error {
        const auto next = std::get<std::int64_t>(value.payload);
        if (next < -10 || next > 10)
          return error::invalid_argument;
        *static_cast<std::int64_t*>(object) = next;
        return error::success;
      },
      .user_data = nullptr};
  if (gneiss::meta::bind_property(registry, object_type,
                                          gneiss::meta::property_id{1}, binding)
          .failed() ||
      gneiss::meta::freeze(registry).failed() ||
      gneiss::meta::freeze(registry).failed() ||
      gneiss::test_support::assign_value(gneiss::meta::is_frozen(registry), frozen)
          .failed() ||
      !frozen || gneiss::meta::register_type(registry, object) != error::invalid_state ||
      gneiss::test_support::assign_value(gneiss::meta::type_count(registry), count)
          .failed() ||
      count != 2)
    return 6;
  gneiss::meta::type info;
  gneiss::meta::property metadata;
  if (gneiss::test_support::assign_value(gneiss::meta::type_at(registry, 0), info)
          .failed() ||
      info.guid() != scalar_type ||
      !(info = gneiss::meta::find_type(registry, object_type)) || info.name() != "Object" ||
      info.schema_version() != 1 || info.properties().size() != 1 ||
      !(metadata = gneiss::meta::find_property(registry, object_type,
                                                       gneiss::meta::property_id{1})) ||
      metadata.name() != "value" ||
      gneiss::meta::find_property(registry, object_type,
                                          gneiss::meta::property_id{2}))
    return 7;
  gneiss::value value;
  if (gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, object_type,
                                              gneiss::meta::property_id{1}, &stored),
          value)
          .failed() ||
      std::get<std::int64_t>(value.payload) != 2)
    return 8;
  value.payload = std::int64_t{7};
  if (gneiss::meta::write_property(registry, object_type,
                                           gneiss::meta::property_id{1}, &stored, value)
          .failed() ||
      stored != 7)
    return 9;
  value.payload = std::int64_t{11};
  if (gneiss::meta::write_property(registry, object_type,
                                           gneiss::meta::property_id{1}, &stored,
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
                 gneiss::test_support::assign_value(gneiss::meta::type_count(registry),
                                                    count) == error::invalid_handle
             ? 0
             : 11;
}
