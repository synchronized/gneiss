// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include "engine/core/meta/object_type.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/sequence_view.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/core/serialization/value.hpp"
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
// 无静态身份的外部类型，通过注册器补充名称和 GUID。
struct custom_sequence {
  std::vector<float> values;
};
struct external_model {
  custom_sequence items;
};
namespace gneiss::engine::meta {
template <> struct sequence_traits<custom_sequence> {
  static constexpr bool enabled = true;
  using element = float;
  static std::size_t size(const custom_sequence& value) { return value.values.size(); }
  static const float* at(const custom_sequence& value, std::size_t i) { return &value.values[i]; }
  static float* at_mut(custom_sequence& value, std::size_t i) { return &value.values[i]; }
  static void resize(custom_sequence& value, std::size_t n) { value.values.resize(n); }
};
}
bool explicit_registration() {
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry)
    return false;
  constexpr auto id = e::guid::literal("86c6b34e-bdc8-4bce-850c-9d574df83951");
  if (m::registration<sample>{*registry}.name("conflict").commit() != e::error::invalid_argument ||
      m::registration<sample>{*registry}.guid(id).commit() != e::error::invalid_argument)
    return false;
  m::registration<external_model> builder{*registry};
  {
    std::string name = "external_model";
    builder.name(name).guid(id).schema_version(3).property<&external_model::items>(
        m::property_id{1}, "items");
  }
  if (builder.commit().failed() || builder.commit() != e::error::invalid_state ||
      m::freeze(*registry).failed())
    return false;
  auto type = m::type_of<external_model>(*registry);
  if (!type || type.name() != "external_model" || type.guid() != id || type.schema_version() != 3)
    return false;
  external_model model{{{1.0F, 2.0F}}};
  auto root = m::view(*registry, model);
  if (!root)
    return false;
  auto member = root->member("items");
  if (!member)
    return false;
  auto sequence = member->as_sequence();
  if (!sequence || *sequence->size() != 2 || sequence->at(2).error() != e::error::not_found)
    return false;
  auto item = sequence->at(0);
  if (!item || item->assign(4.0F).failed() || model.items.values[0] != 4.0F)
    return false;
  e::serialization::context context{*registry};
  auto encoded = e::serialization::json::serialize_bytes(context, model);
  if (!encoded)
    return false;
  auto decoded = e::serialization::json::deserialize<external_model>(context, *encoded);
  return decoded && decoded->items.values == model.items.values;
}
struct runtime_child {
  static constexpr std::string_view type_name = "runtime_child";
  float number = 1;
};
struct runtime_parent {
  static constexpr std::string_view type_name = "runtime_parent";
  runtime_child child;
};
struct runtime_other {
  static constexpr std::string_view type_name = "runtime_other";
  float number = 2;
};
struct runtime_poly : e::meta::object_type<runtime_poly> {
  static constexpr std::string_view type_name = "runtime_poly";
  static constexpr e::guid type_guid{};
  float number = 3;
};
bool runtime_only_registration() {
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry ||
      m::registration<runtime_child>{*registry}
          .property<&runtime_child::number>(m::property_id{1}, "number")
          .commit()
          .failed() ||
      m::registration<runtime_parent>{*registry}
          .property<&runtime_parent::child>(m::property_id{1}, "child")
          .commit()
          .failed() ||
      m::registration<runtime_other>{*registry}
          .property<&runtime_other::number>(m::property_id{1}, "number")
          .commit()
          .failed() ||
      m::registration<runtime_poly>{*registry}
          .property<&runtime_poly::number>(m::property_id{1}, "number")
          .metadata(gneiss::engine::serialization::reference_policy{.polymorphic = true})
          .commit()
          .failed() ||
      m::registration<runtime_child>{*registry}.commit() != e::error::invalid_argument ||
      m::freeze(*registry).failed())
    return false;
  auto type = m::type_of<runtime_parent>(*registry);
  if (!type || type.guid() || type.name() != "runtime_parent" || m::find_type(*registry, e::guid{}))
    return false;
  runtime_parent object;
  runtime_other other;
  auto root = m::view(*registry, object);
  auto foreign = m::view(*registry, other);
  if (!root || !foreign)
    return false;
  auto child = root->member("child");
  if (!child)
    return false;
  auto number = child->member("number");
  auto field = child->type().find_property(m::property_id{1});
  if (!number || !field || number->assign(7.0F).failed() || object.child.number != 7 ||
      field.get(*foreign).error() != e::error::invalid_argument)
    return false;
  e::serialization::context context{*registry};
  auto encoded = e::serialization::json::serialize_bytes(context, object);
  if (!encoded)
    return false;
  auto decoded = e::serialization::json::deserialize<runtime_parent>(context, *encoded);
  if (!decoded || decoded->child.number != 7 ||
      e::serialization::capture_value(context, object).error().error != e::error::unsupported)
    return false;
  auto poly = e::make_ref<runtime_poly>();
  auto rejected = e::serialization::json::serialize_bytes(context, poly);
  return !rejected && rejected.error().error == e::error::unsupported;
}
struct display_metadata {
  std::string label;
};
bool extension_registration() {
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry)
    return false;
  if (m::registration<sample>{*registry}
              .metadata(display_metadata{"first"})
              .metadata(display_metadata{"duplicate"})
              .commit() != e::error::invalid_argument ||
      m::registration<sample>{*registry}
              .property<&sample::speed>(m::property_id{1}, "speed")(display_metadata{"first"},
                                                                    display_metadata{"duplicate"})
              .commit() != e::error::invalid_argument)
    return false;
  display_metadata label{"original"};
  m::registration<sample> builder{*registry};
  builder.metadata(label);
  auto speed = builder.property<&sample::speed>(m::property_id{1}, "speed");
  // 后续字段使存储扩容，早先代理仍须写入原字段。
  for (std::uint32_t id = 2; id != 40; ++id)
    builder.property<&sample::speed>(m::property_id{id}, "speed_" + std::to_string(id));
  speed(display_metadata{"Speed"});
  label.label = "changed";
  if (builder.commit().failed() || m::freeze(*registry).failed())
    return false;
  auto type = m::type_of<sample>(*registry);
  if (!type)
    return false;
  auto field = type.find_property(m::property_id{1});
  const auto* type_data = type.extension<display_metadata>();
  const auto* field_data = field ? field.extension<display_metadata>() : nullptr;
  if (!type_data || type_data->label != "original" || !field_data || field_data->label != "Speed" ||
      type.extension<int>() || field.extension<int>())
    return false;
  registry->reset();
  // 类型和属性句柄继续保活不可变元数据。
  return type.extension<display_metadata>() == type_data &&
         field.extension<display_metadata>() == field_data;
}
int main() {
  if (!extension_registration())
    return 14;
  if (!runtime_only_registration())
    return 13;
  if (!explicit_registration())
    return 12;
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
  if (gneiss::engine::meta::registration<sample>{registry}
          .property<&sample::speed>(gneiss::engine::meta::property_id{1}, "speed")
          .readonly<&sample::active>(gneiss::engine::meta::property_id{1}, "active")
          .commit() != e::error::invalid_argument)
    return 4;
  // 前一次失败不得留下类型；名称由 builder 拥有。
  gneiss::engine::meta::registration<const sample&> builder{registry};
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
