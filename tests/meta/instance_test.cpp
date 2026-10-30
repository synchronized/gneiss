// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registration.hpp"
#include <future>
namespace e = gneiss::engine;
namespace m = e::meta;
enum class mode : std::int8_t { first = -1, second = 2 };
struct enum_object {
  static constexpr std::string_view type_name = "enum_object";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 3});
  mode left = mode::first, right = mode::second;
};
bool enum_registration() {
  auto made = m::create_registry();
  if (!made)
    return false;
  const auto& registry = *made;
  enum class unknown_mode { value };
  unknown_mode unknown{};
  auto unknown_view = m::instance::from(registry, unknown);
  if (!unknown_view || unknown_view->type().enum_entries().error() != e::error::not_ready)
    return false;
  if (m::type_builder<enum_object>{registry}
          .property<&enum_object::left>(m::property_id{1}, "left")
          .property<&enum_object::right>(m::property_id{2}, "right")
          .commit()
          .failed())
    return false;
  if (m::enum_builder<mode>{registry}
              .value("same", mode::first)
              .value("same", mode::second)
              .commit() != e::error::invalid_argument ||
      m::enum_builder<mode>{registry}
              .value("one", mode::first)
              .value("alias", mode::first)
              .commit() != e::error::invalid_argument)
    return false;
  m::enum_builder<mode> builder{registry};
  std::string label = "First";
  builder.value(label, mode::first).value("Second", mode::second);
  label = "changed";
  if (builder.commit().failed() || builder.commit() != e::error::invalid_state ||
      m::enum_builder<mode>{registry}.value("duplicate", mode::first).commit() !=
          e::error::invalid_argument ||
      m::freeze(registry).failed())
    return false;
  auto type = m::describe_type<enum_object>(registry);
  if (!type || type->properties[0].type.id() != e::runtime_type_of<mode>() ||
      type->properties[0].type.enum_entries()->size() != 2 ||
      type->properties[0].type.enum_entries()->data() !=
          type->properties[1].type.enum_entries()->data() ||
      (*type->properties[0].type.enum_entries())[0].name != "First" ||
      std::get<std::int64_t>((*type->properties[0].type.enum_entries())[0].data.payload) != -1)
    return false;
  if (unknown_view->type().enum_entries().error() != e::error::not_found)
    return false;
  std::vector<mode> modes{mode::first};
  auto sequence = m::instance::from(registry, modes);
  if (!sequence || sequence->type().is_enum())
    return false;
  auto element_type = sequence->type().element_type();
  if (!element_type || !element_type->is_enum() || element_type->id() != e::runtime_type_of<mode>())
    return false;
  auto entries = element_type->enum_entries();
  auto elements = sequence->as_sequence();
  if (!entries || entries->size() != 2 || !elements)
    return false;
  auto element = elements->at(0);
  if (!element || !element->type().is_enum() || !element->type().enum_entries())
    return false;
  if (m::type{}.enum_entries().error() != e::error::invalid_state)
    return false;
  enum_object object;
  auto view = m::mutable_instance::from(registry, object);
  if (!view)
    return false;
  auto field = view->property("left");
  if (!field || !field->type().is_enum() || !field->type().enum_entries() ||
      field->write_value(e::value{std::int64_t{2}}).failed() || object.left != mode::second ||
      field->write_value(e::value{std::int64_t{128}}) != e::error::invalid_argument ||
      object.left != mode::second)
    return false;
  return m::enum_builder<mode>{registry}.value("late", mode::first).commit() ==
         e::error::invalid_state;
}
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
struct query_model {
  static constexpr std::string_view type_name = "query_model";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 4});
  std::vector<mode> modes;
  std::int8_t count{};
};
bool type_queries() {
  auto registry = m::create_registry();
  if (!registry || m::type_of<float>(*registry).error() != e::error::not_ready ||
      m::find_type(*registry, query_model::type_guid).error() != e::error::not_ready)
    return false;
  if (m::enum_builder<mode>{*registry}.value("first", mode::first).commit().failed() ||
      m::type_builder<query_model>{*registry}
          .property<&query_model::modes>(m::property_id{1}, "modes")
          .property<&query_model::count>(m::property_id{2}, "count")
          .commit()
          .failed() ||
      m::freeze(*registry).failed())
    return false;
  if (m::freeze().failed())
    return false;
  auto default_float = m::type_of<float>();
  if (!default_float || !m::find_type(e::runtime_type_of<float>()) ||
      !m::find_type(default_float->guid()))
    return false;
  auto typed = m::type_of<const query_model&>(*registry);
  auto by_id = m::find_type(*registry, e::runtime_type_of<query_model>());
  auto by_guid = m::find_type(*registry, query_model::type_guid);
  if (!typed || !by_id || !by_guid || typed->guid() != by_id->guid() ||
      typed->id() != by_guid->id())
    return false;
  auto props = typed->properties();
  if (!props || props->size() != 2 || (*props)[0].type.kind() != m::type_kind::sequence ||
      (*props)[1].type.id() != e::runtime_type_of<std::int8_t>())
    return false;
  auto sequence = m::find_type(*registry, e::runtime_type_of<std::vector<mode>>());
  auto element = sequence ? sequence->element_type() : e::result<m::type>{sequence.error()};
  auto enumeration = m::find_type(*registry, e::runtime_type_of<mode>());
  if (!element || !element->is_enum() || !enumeration ||
      element->enum_entries()->data() != enumeration->enum_entries()->data())
    return false;
  auto scalar = m::type_of<float>(*registry);
  if (!scalar || !m::find_type(*registry, scalar->guid()) ||
      m::type_of<settings>(*registry).error() != e::error::not_found ||
      m::find_type(*registry, e::runtime_type_of<settings>()).error() != e::error::not_found ||
      m::find_type(*registry, e::guid{}).error() != e::error::not_found ||
      m::find_type(m::registry_ref{}, e::runtime_type_of<float>()).error() !=
          e::error::invalid_handle)
    return false;
  // 类型视图保活注册表，但不要求对象实例存活。
  std::weak_ptr<m::type_registry_state> weak = *registry;
  registry = e::error::not_found;
  return !weak.expired() && typed->properties()->size() == 2;
}
int main() try {
  if (!type_queries())
    return 31;
  if (!enum_registration())
    return 30;
  auto created = m::create_registry();
  if (!created)
    return 1;
  auto registry = *created;
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
  if (weak.expired())
    return 24;
  // 属性快照中的类型也持有注册表，清空后最后一个元数据所有者才释放。
  entries->clear();
  if (!weak.expired())
    return 32;
  return 0;
} catch (...) {
  return 99;
}
