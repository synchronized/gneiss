// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/value_result.hpp"
#include <gneiss/engine/core/meta/capabilities/enumeration.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/object_type.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/meta/views/sequence_view.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/core/serialization/value.hpp>
#include <gneiss/engine/core/base/string_name.hpp>
#include <stdexcept>
namespace e = gneiss;
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
namespace gneiss::meta {
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
  if (m::registration::type<sample>(registry).name("conflict").commit() !=
          e::error::invalid_argument ||
      m::registration::type<sample>(registry).guid(id).commit() != e::error::invalid_argument)
    return false;
  auto builder = m::registration::type<external_model>(registry);
  {
    std::string name = "external_model";
    builder.name(name).guid(id).schema_version(3).property<&external_model::items>(
        gneiss::property_id{1}, "items");
  }
  if (e::string_name::find("external_model"))
    return false;
  if (builder.commit().failed() || builder.commit() != e::error::invalid_state ||
      m::freeze(registry).failed())
    return false;
  auto type = m::type_of<external_model>(registry);
  if (!type || type.name() != "external_model" || type.guid() != id || type.schema_version() != 3)
    return false;
  const auto pooled = e::string_name::find("external_model");
  if (!pooled || type.name().data() != pooled->view().data() ||
      type.template capability<gneiss::meta::cap::object>().find_property(
          "not_registered_property") ||
      e::string_name::find("not_registered_property"))
    return false;
  external_model model{{{1.0F, 2.0F}}};
  auto root = m::view(registry, model);
  auto member = root.template capability<gneiss::meta::cap::object>()->member("items");
  if (!member)
    return false;
  auto sequence = member->as_sequence();
  if (!sequence || sequence->size() != 2 || sequence->at(2).error() != e::error::not_found)
    return false;
  auto item = sequence->at(0);
  if (!item || item->assign(4.0F).failed() || model.items.values[0] != 4.0F)
    return false;
  e::serialization::write_context context{registry};
  e::serialization::read_context read_context{registry};
  auto encoded = e::serialization::json::encode_bytes(context, model);
  if (!encoded)
    return false;
  auto decoded = e::serialization::json::decode<external_model>(read_context, *encoded);
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
      m::registration::type<runtime_child>(registry)
          .property<&runtime_child::number>(gneiss::property_id{1}, "number")
          .commit()
          .failed() ||
      m::registration::type<runtime_parent>(registry)
          .property<&runtime_parent::child>(gneiss::property_id{1}, "child")
          .commit()
          .failed() ||
      m::registration::type<runtime_other>(registry)
          .property<&runtime_other::number>(gneiss::property_id{1}, "number")
          .commit()
          .failed() ||
      m::registration::type<runtime_poly>(registry)
          .property<&runtime_poly::number>(gneiss::property_id{1}, "number")
          .metadata(gneiss::serialization::reference_policy{.polymorphic = true})
          .commit()
          .failed() ||
      m::registration::type<runtime_child>(registry).commit() != e::error::invalid_argument ||
      m::freeze(registry).failed())
    return false;
  auto type = m::type_of<runtime_parent>(registry);
  if (!type || type.guid() || type.name() != "runtime_parent" || m::find_type(registry, e::guid{}))
    return false;
  runtime_parent object;
  runtime_other other;
  auto root = m::view(registry, object);
  auto foreign = m::view(registry, other);
  auto child = root.template capability<gneiss::meta::cap::object>()->member("child");
  if (!child)
    return false;
  auto number = child->template capability<gneiss::meta::cap::object>()->member("number");
  auto field = child->type().template capability<gneiss::meta::cap::object>().find_property(
      gneiss::property_id{1});
  if (!number || !field || number->assign(7.0F).failed() || object.child.number != 7 ||
      field.get(foreign).error() != e::error::invalid_argument)
    return false;
  e::serialization::write_context context{registry};
  e::serialization::read_context read_context{registry};
  auto encoded = e::serialization::json::encode_bytes(context, object);
  if (!encoded)
    return false;
  auto decoded = e::serialization::json::decode<runtime_parent>(read_context, *encoded);
  if (!decoded || decoded->child.number != 7 ||
      e::serialization::capture_value(context, object).error().error != e::error::unsupported)
    return false;
  auto poly = e::make_ref<runtime_poly>();
  auto rejected = e::serialization::json::encode_bytes(context, poly);
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
  if (m::registration::type<sample>(registry)
              .metadata(display_metadata{"first"})
              .metadata(display_metadata{"duplicate"})
              .commit() != e::error::invalid_argument ||
      m::registration::type<sample>(registry)
              .property<&sample::speed>(gneiss::property_id{1}, "speed")(
                  display_metadata{"first"}, display_metadata{"duplicate"})
              .commit() != e::error::invalid_argument)
    return false;
  display_metadata label{"original"};
  auto builder = m::registration::type<sample>(registry);
  builder.metadata(label);
  auto speed = builder.property<&sample::speed>(gneiss::property_id{1}, "speed");
  // 后续字段使存储扩容，早先代理仍须写入原字段。
  for (std::uint32_t id = 2; id != 40; ++id)
    builder.property<&sample::speed>(gneiss::property_id{id}, "speed_" + std::to_string(id));
  speed(display_metadata{"Speed"});
  label.label = "changed";
  if (builder.commit().failed() || m::freeze(registry).failed())
    return false;
  auto type = m::type_of<sample>(registry);
  if (!type)
    return false;
  auto field =
      type.template capability<gneiss::meta::cap::object>().find_property(gneiss::property_id{1});
  const auto* type_data = type.extension<display_metadata>();
  const auto* field_data = field ? field.extension<display_metadata>() : nullptr;
  if (!type_data || type_data->label != "original" || !field_data || field_data->label != "Speed" ||
      type.extension<int>() || field.extension<int>())
    return false;
  registry.reset();
  // 类型和属性句柄继续保活不可变元数据。
  return type.extension<display_metadata>() == type_data &&
         field.extension<display_metadata>() == field_data;
}
bool registration_exception_contract() {
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry)
    return false;
  enum class mode { one };
  bool cleaned = false;
  struct cleanup {
    bool& flag;
    ~cleanup() { flag = true; }
  };
  try {
    m::registration::type<sample>(registry).extend([&]<class>() -> e::result<int> {
      cleanup guard{cleaned};
      throw std::bad_alloc{};
    });
    return false;
  } catch (const std::bad_alloc&) {
  }
  if (!cleaned || m::type_of<sample>(registry).registered())
    return false;
  try {
    m::registration::enumeration<mode>(registry).extend(
        []<class>() -> e::result<int> { throw std::runtime_error("extension fixture"); });
    return false;
  } catch (const std::runtime_error&) {
  }
  if (m::type_of<mode>(registry).registered())
    return false;
  // 预期错误仍通过 commit 返回，不变成致命异常。
  return m::registration::type<sample>(registry)
             .extend([]<class>() -> e::result<int> { return e::error::invalid_argument; })
             .commit() == e::error::invalid_argument;
}
// 名称暂存不入池，提交后不再依赖 builder 或调用方字符串。
bool pooled_enum_names() {
  namespace m = e::meta;
  enum class named_mode { first, second };
  auto registry = m::create_registry();
  if (!registry)
    return false;
  {
    auto builder = m::registration::enumeration<named_mode>(registry);
    builder.name("pooled_enum_names");
    std::string name = "pooled_enum_first";
    builder.value(name, named_mode::first);
    name = "pooled_enum_second";
    builder.value(name, named_mode::second);
    name.clear();
    if (e::string_name::find("pooled_enum_first") || builder.commit().failed())
      return false;
  }
  if (m::freeze(registry).failed())
    return false;
  auto type = m::type_of<named_mode>(registry);
  auto entries = type.capability<gneiss::meta::cap::enumeration>().entries();
  const auto pooled = e::string_name::find("pooled_enum_first");
  return entries.size() == 2 && pooled && entries[0].name == "pooled_enum_first" &&
         entries[0].name.data() == pooled->view().data() && entries[1].name == "pooled_enum_second";
}

int main() {
  if (!pooled_enum_names())
    return 16;
  if (!registration_exception_contract())
    return 90;
  if (!extension_registration())
    return 14;
  if (!runtime_only_registration())
    return 13;
  if (!explicit_registration())
    return 12;
  auto registry = gneiss::meta::create_registry();
  if (e::type_identity<const sample&>::guid() != sample::type_guid ||
      e::type_identity<sample>::name() != "sample" ||
      e::type_identity<const float&>::guid() != e::type_identity<float>::guid())
    return 2;
  if (gneiss::meta::register_type<sample>(
          registry, {.schema_version = 1, .name = "conflict"}) != e::error::invalid_argument)
    return 3;
  if (gneiss::meta::registration::type<sample>(registry)
          .property<&sample::speed>(gneiss::property_id{1}, "speed")
          .readonly<&sample::active>(gneiss::property_id{1}, "active")
          .commit() != e::error::invalid_argument)
    return 4;
  // 前一次失败不得留下类型；名称由 builder 拥有。
  auto builder = gneiss::meta::registration::type<const sample&>(registry);
  {
    std::string name = "speed";
    builder.property<&sample::get_speed, &sample::set_speed>(gneiss::property_id{1}, name);
  }
  builder.readonly<&sample::active>(gneiss::property_id{2}, "active")
      .property<&sample::speed>(gneiss::property_id{3}, "raw_speed");
  if (builder.commit().failed() || builder.commit() != e::error::invalid_state ||
      gneiss::meta::freeze(registry).failed())
    return 5;
  sample object;
  auto reflected = gneiss::meta::view(registry, object).capability<gneiss::meta::cap::object>();
  auto speed = reflected->member("speed");
  auto raw_speed = reflected->member("raw_speed");
  if (!speed || !raw_speed)
    return 6;
  e::value output;
  if (gneiss::test_support::assign_value(speed->read_value(), output).failed() ||
      std::get<float>(output.payload) != 1)
    return 6;
  if (speed->assign(e::value{2.0F}).failed() || object.speed != 2)
    return 7;
  if (speed->assign(e::value{-1.0F}) != e::error::invalid_argument || object.speed != 2)
    return 8;
  if (reflected->member("active").error() == e::error::success || !object.active)
    return 9;
  if (raw_speed->assign(e::value{3.0F}).failed() || object.speed != 3)
    return 10;
  if (speed->assign(e::value{true}) != e::error::invalid_argument || object.speed != 3)
    return 11;
  return 0;
}
