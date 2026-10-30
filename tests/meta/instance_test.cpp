// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registration.hpp"
#include <future>
namespace e = gneiss::engine;
namespace m = e::meta;
struct settings {
  static constexpr std::string_view type_name = "instance_settings";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 1});
  float speed = 1;
  float get_speed() const { return speed; }
  e::error set_speed(float input) {
    if (input < 0)
      return e::error::invalid_argument;
    speed = input;
    return e::error::success;
  }
};
struct object {
  static constexpr std::string_view type_name = "instance_object";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 2});
  settings config;
  std::string label = "old";
  std::string get_label() const { return label + "!"; }
  void set_label(const std::string& input) { label = input; }
  float readonly() const { return config.speed * 2; }
};
template <class T>
concept can_borrow = requires(T&& value) { m::instance::from(std::forward<T>(value)); };
template <class T>
concept can_mutate = requires(T&& value) { m::mutable_instance::from(std::forward<T>(value)); };
static_assert(can_borrow<object&> && can_borrow<const object&>);
static_assert(!can_borrow<object> && !can_borrow<const object>);
static_assert(can_mutate<object&> && !can_mutate<const object&> && !can_mutate<object>);
static_assert(!std::is_constructible_v<m::mutable_instance, m::instance>);
int main() try {
  auto created = m::create_registry();
  if (!created)
    return 1;
  auto registry = *created;
  if (m::type_builder<settings>{registry}
          .property<&settings::speed>(m::property_id{1}, "speed")
          .choices({{"invalid", e::value{std::uint64_t{0}}}})
          .commit() != e::error::invalid_argument)
    return 29;
  object source;
  if (m::instance::from(registry, source).error() != e::error::not_ready)
    return 2;
  if (m::type_builder<settings>{registry}
          .property<&settings::get_speed, &settings::set_speed>(m::property_id{1}, "speed")
          .commit()
          .failed() ||
      m::type_builder<object>{registry}
          .property<&object::config>(m::property_id{1}, "config")
          .property<&object::get_label, &object::set_label>(m::property_id{2}, "label")
          .readonly<&object::readonly>(m::property_id{3}, "computed")
          .property<&object::config>(m::property_id{4}, "readonly_config",
                                     m::property_flags::read_only)
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return 3;
  auto root = m::instance::from(registry, std::as_const(source));
  if (!root || root->kind() != m::type_kind::object || root->guid() != object::type_guid ||
      root->type_id() != e::runtime_type_of<object>())
    return 4;
  auto entries = root->properties();
  if (!entries || entries->size() != 4)
    return 5;
  auto config = root->property("config");
  if (!config || config->guid() != settings::type_guid || config->kind() != m::type_kind::object)
    return 6;
  auto speed = config->property(m::property_id{1});
  if (!speed || speed->kind() != m::type_kind::scalar ||
      speed->scalar_kind() != e::value_kind::float32 ||
      std::get<float>(speed->read_value()->payload) != 1)
    return 7;
  if (speed->property("x").error() != e::error::unsupported ||
      root->property("missing").error() != e::error::not_found ||
      root->read_value().error() != e::error::unsupported ||
      speed->object_data().error() != e::error::unsupported)
    return 8;
  auto writable = m::mutable_instance::from(registry, source);
  if (!writable)
    return 9;
  auto editable_config = writable->property("config");
  if (!editable_config)
    return 10;
  auto editable_speed = editable_config->property("speed");
  if (!editable_speed || editable_speed->write_value(e::value{3.0F}).failed() ||
      source.config.speed != 3)
    return 11;
  if (editable_speed->write_value(e::value{-1.0F}) != e::error::invalid_argument ||
      source.config.speed != 3 ||
      editable_speed->write_value(e::value{true}) != e::error::invalid_argument)
    return 12;
  if (writable->property("computed").error() != e::error::unsupported)
    return 13;
  if (writable->property("readonly_config").error() != e::error::unsupported)
    return 28;
  auto computed = writable->as_const().property("computed");
  if (!computed || std::get<float>(computed->read_value()->payload) != 6)
    return 14;
  auto label = root->property("label");
  if (!label)
    return 15;
  auto snapshot = label->read_value();
  if (!snapshot || std::get<std::string>(snapshot->payload) != "old!")
    return 16;
  source.label = "new";
  if (std::get<std::string>(snapshot->payload) != "old!" ||
      std::get<std::string>(label->read_value()->payload) != "new!")
    return 17;
  auto edit_label = writable->property("label");
  if (!edit_label || edit_label->write_value(e::value{std::string("written")}).failed() ||
      source.label != "written")
    return 18;
  settings replacement{9};
  if (editable_config->assign_object(e::runtime_type_of<object>(), &replacement) !=
          e::error::invalid_argument ||
      editable_config->assign_object(e::runtime_type_of<settings>(), &replacement).failed() ||
      source.config.speed != 9)
    return 19;
  if (m::instance{}.read_value().error() != e::error::invalid_state ||
      m::mutable_instance{}.write_value(e::value{1.0F}) != e::error::invalid_state)
    return 20;
  {
    settings other{7};
    auto borrowed = m::instance::from(registry, other);
    if (!borrowed || editable_config->assign(*borrowed).failed() || source.config.speed != 7 ||
        other.speed != 7 || editable_config->assign(*root) != e::error::invalid_argument ||
        editable_speed->assign(*borrowed) != e::error::unsupported ||
        m::mutable_instance{}.assign(*borrowed) != e::error::invalid_state)
      return 25;
    object copy;
    auto destination = m::mutable_instance::from(registry, copy);
    if (!destination || destination->assign(*root).failed() || copy.label != source.label ||
        destination->assign(destination->as_const()).failed())
      return 26;
    copy.label = "independent";
    if (source.label == copy.label)
      return 27;
    source.config.speed = 9;
  }
  // 同步只读访问；视图没有创建或拥有 source。
  auto task = std::async(std::launch::async,
                         [speed] { return std::get<float>(speed->read_value()->payload); });
  if (std::get<float>(speed->read_value()->payload) != 9 || task.get() != 9)
    return 21;
  auto independent = m::create_registry();
  if (!independent || m::freeze(*independent).failed() ||
      m::instance::from(*independent, source).error() != e::error::not_found)
    return 22;
  // 返回的子视图延长元数据寿命，不依赖根视图对象自身。
  auto weak = std::weak_ptr<m::type_registry_state>(registry);
  created->reset();
  registry.reset();
  *root = {};
  *config = {};
  *writable = {};
  *editable_config = {};
  *editable_speed = {};
  *computed = {};
  *label = {};
  *edit_label = {};
  if (weak.expired() || std::get<float>(speed->read_value()->payload) != 9)
    return 23;
  *speed = {};
  if (!weak.expired())
    return 24;
  return 0;
} catch (...) {
  return 99;
}
