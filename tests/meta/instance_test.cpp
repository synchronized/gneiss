// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
#include <future>
namespace e = gneiss::engine;
namespace m = e::meta;
struct object_padding {
  virtual ~object_padding() = default;
  int padding = 1;
};
struct shared_probe : object_padding, m::object_type<shared_probe> {
  static constexpr std::string_view type_name = "shared_probe";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 20});
  int number = 7;
  e::value nested{true};
};
struct unrelated_probe : m::object_type<unrelated_probe> {
  static constexpr std::string_view type_name = "unrelated_probe";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 21});
};
struct shared_holder {
  static constexpr std::string_view type_name = "shared_holder";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 22});
  e::ref<shared_probe> item;
  e::value another{true};
};
bool object_values() {
  {
    auto owner = e::make_ref<shared_probe>();
    std::weak_ptr<shared_probe> weak = owner.shared();
    e::value first{e::ref<m::object>{owner}};
    e::value second = first;
    owner.reset();
    first = e::value{};
    if (weak.expired() || second.kind() != e::value_kind::object)
      return false;
    e::value empty{e::ref<m::object>{}};
    if (empty == e::value{} || empty == second || empty.kind() != e::value_kind::object)
      return false;
    second = e::value{};
    if (!weak.expired())
      return false;
  }

  auto registry = m::create_registry();
  if (!registry || m::register_builtin_types(*registry).failed() ||
      m::type_builder<shared_probe>{*registry}
          .property<&shared_probe::number>(m::property_id{1}, "number")
          .property<&shared_probe::nested>(m::property_id{2}, "nested")
          .serialization()
          .commit()
          .failed() ||
      m::type_builder<shared_holder>{*registry}
          .property<&shared_holder::item>(m::property_id{1}, "item")
          .property<&shared_holder::another>(m::property_id{2}, "another")
          .serialization()
          .commit()
          .failed() ||
      m::freeze(*registry).failed())
    return false;
  shared_holder holder{e::make_ref<shared_probe>()};
  auto view = m::mutable_instance::from(*registry, holder);
  if (!view)
    return false;
  auto field = view->property("item");
  if (!field || field->kind() != m::type_kind::reference)
    return false;
  auto read = field->read_value();
  if (!read || read->kind() != e::value_kind::object)
    return false;
  auto copy = *read;
  auto owner = std::get<e::ref<m::object>>(copy.payload);
  if (owner.get() != holder.item.get())
    return false;
  holder.item->number = 19;
  if (dynamic_cast<shared_probe*>(owner.get())->number != 19)
    return false;
  e::value wrong{e::ref<m::object>{e::make_ref<unrelated_probe>()}};
  if (field->write_value(wrong) != e::error::invalid_argument || holder.item->number != 19 ||
      field->write_value(e::value{true}) != e::error::invalid_argument)
    return false;
  e::value empty{e::ref<m::object>{}};
  if (field->write_value(empty).failed() || holder.item || field->write_value(copy).failed() ||
      holder.item.get() != owner.get())
    return false;
  e::ref<const shared_probe> readonly = holder.item;
  auto const_view = m::instance::from(*registry, readonly);
  if (!const_view || const_view->read_value().error() != e::error::unsupported)
    return false;
  // value 对象按内容恢复独立对象，普通 ref 的内联序列化仍有效。
  holder.another = copy;
  auto file = m::serialize(*registry, holder);
  auto encoded_value = m::serialize(*registry, copy);
  auto encoded_empty = m::serialize(*registry, empty);
  if (!file || !encoded_value || !encoded_empty || m::valid_scalar(copy))
    return false;
  auto restored = m::deserialize<e::value>(*registry, *encoded_value);
  auto restored_empty = m::deserialize<e::value>(*registry, *encoded_empty);
  if (!restored || !restored_empty || *restored_empty != empty)
    return false;
  auto restored_owner = std::get<e::ref<m::object>>(restored->payload);
  auto* restored_probe = dynamic_cast<shared_probe*>(restored_owner.get());
  if (!restored_probe || restored_probe->number != 19 || restored_owner == owner)
    return false;
  holder.item->nested = copy;
  auto cycle = m::serialize(*registry, copy);
  holder.item->nested = e::value{true};
  if (cycle || cycle.error().error != e::error::invalid_argument)
    return false;
  auto malformed = [&](std::string tag) {
    const std::string json =
        "{\"kind\":\"object\",\"value\":{\"$type_guid\":\"" + tag + "\",\"$content\":{}}}";
    return m::deserialize<e::value>(*registry, std::as_bytes(std::span{json.data(), json.size()}));
  };
  auto unknown = malformed(unrelated_probe::type_guid.string());
  auto wrong_type = malformed(shared_holder::type_guid.string());
  if (unknown || unknown.error().error != e::error::not_found || wrong_type ||
      wrong_type.error().error != e::error::unsupported)
    return false;
  auto unregistered = m::serialize(*registry, wrong);
  if (unregistered || unregistered.error().error != e::error::not_found)
    return false;
  auto decoded = m::deserialize<shared_holder>(*registry, *file);
  if (!decoded || !decoded->item || decoded->item->number != 19 || decoded->item == holder.item)
    return false;
  const auto duplicate = std::get<e::ref<m::object>>(decoded->another.payload);
  if (duplicate.get() == decoded->item.get() ||
      dynamic_cast<shared_probe*>(duplicate.get())->number != 19)
    return false;
  std::string deep = "{\"kind\":\"bool\",\"value\":true}";
  for (int i = 0; i < 70; ++i)
    deep = "{\"kind\":\"object\",\"value\":{\"$type_guid\":\"" + shared_probe::type_guid.string() +
           "\",\"$content\":{\"number\":1,\"nested\":" + deep + "}}}";
  auto too_deep =
      m::deserialize<e::value>(*registry, std::as_bytes(std::span{deep.data(), deep.size()}));
  return !too_deep && too_deep.error().error == e::error::invalid_argument;
}
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
struct reference_target {
  inline static int alive = 0;
  reference_target() { ++alive; }
  ~reference_target() { --alive; }
  reference_target(const reference_target&) = delete;
  reference_target& operator=(const reference_target&) = delete;
  static constexpr std::string_view type_name = "reference_target";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 20});
  int number = 7;
  e::ref<reference_target> next;
};
struct reference_owner {
  static constexpr std::string_view type_name = "reference_owner";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 21});
  e::ref<reference_target> target;
  e::ref<const reference_target> readonly;
};
bool reference_properties() {
  auto made = m::create_registry();
  if (!made)
    return false;
  auto registry = *made;
  if (m::type_builder<reference_target>{registry}
          .property<&reference_target::number>(m::property_id{1}, "number")
          .property<&reference_target::next>(m::property_id{2}, "next")
          .serialization()
          .commit()
          .failed() ||
      m::type_builder<reference_owner>{registry}
          .property<&reference_owner::target>(m::property_id{1}, "target")
          .property<&reference_owner::readonly>(m::property_id{2}, "readonly")
          .serialization()
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return false;
  reference_owner owner;
  auto root = m::mutable_instance::from(registry, owner);
  if (!root)
    return false;
  auto field = root->property("target");
  if (!field || field->kind() != m::type_kind::reference ||
      field->as_reference()->get().error() != e::error::not_found ||
      field->read_value().error() != e::error::unsupported)
    return false;
  auto target_type = field->type().element_type();
  if (!target_type || target_type->id() != e::runtime_type_of<reference_target>())
    return false;
  auto target = e::make_ref<reference_target>();
  auto source = m::instance::from(registry, target);
  if (!source || field->assign(*source).failed() || owner.target.get() != target.get())
    return false;
  auto pointed = field->as_reference()->get();
  if (!pointed)
    return false;
  auto number = pointed->property("number");
  if (!number || number->write_value(e::value{std::int64_t{42}}).failed() || target->number != 42)
    return false;
  owner.readonly = target;
  auto readonly = root->property("readonly");
  if (!readonly || readonly->as_reference()->get().error() != e::error::unsupported ||
      !readonly->as_const().as_reference()->get() ||
      field->assign(readonly->as_const()) != e::error::invalid_argument)
    return false;
  auto reference = field->as_reference();
  if (!reference || !reference->has_value() || !*reference->has_value() ||
      root->as_reference().error() != e::error::unsupported ||
      m::instance{}.as_reference().error() != e::error::invalid_state)
    return false;
  e::ref<reference_target> copied;
  auto copied_instance = m::mutable_instance::from(registry, copied);
  if (!copied_instance)
    return false;
  auto copied_view = copied_instance->as_reference();
  if (!copied_view || *copied_view->has_value() ||
      copied_view->assign(reference->as_const()).failed() || copied.get() != target.get() ||
      copied_view->assign(readonly->as_reference()->as_const()) != e::error::invalid_argument ||
      copied_view->reset().failed() || copied || !owner.target)
    return false;
  // 引用按值内联保存；文件内重复引用与再次加载均产生独立对象。
  auto encoded = m::serialize(registry, owner);
  if (!encoded)
    return false;
  auto restored = m::deserialize<reference_owner>(registry, *encoded);
  auto independent = m::deserialize<reference_owner>(registry, *encoded);
  if (!restored || !independent || !restored->target ||
      restored->target.get() == restored->readonly.get() || restored->target->number != 42 ||
      restored->target.get() == independent->target.get())
    return false;
  target->next = target;
  auto cycle = m::serialize(registry, owner);
  target->next.reset();
  if (cycle || cycle.error().error != e::error::invalid_argument)
    return false;
  const auto bad = [&](std::string input) {
    const auto before = reference_target::alive;
    auto result = m::deserialize<reference_owner>(
        registry, std::as_bytes(std::span{input.data(), input.size()}));
    return !result && reference_target::alive == before;
  };
  if (!bad(R"({"target":3})") || !bad(R"({"target":{"number":"wrong"}})") ||
      !bad(R"({"target":{"next":[]}})"))
    return false;
  const std::string_view encoded_text{reinterpret_cast<const char*>(encoded->data()),
                                      encoded->size()};
  if (encoded_text.find("$objects") != std::string_view::npos ||
      encoded_text.find("$ref") != std::string_view::npos ||
      encoded_text.find("$type_guid") != std::string_view::npos)
    return false;
  const std::string input = R"({"target":null,"readonly":null})";
  auto decoded = m::deserialize<reference_owner>(
      registry, std::as_bytes(std::span{input.data(), input.size()}));
  if (!decoded || decoded->target || decoded->readonly)
    return false;
  if (field->as_reference()->reset().failed() || owner.target || !owner.readonly ||
      target->number != 42)
    return false;
  if (field->as_reference()->get().error() != e::error::not_found)
    return false;
  return readonly->as_reference()->reset().ok() && !owner.readonly;
}
struct polymorphic_base : m::object {
  static constexpr std::string_view type_name = "polymorphic_base";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 30});
};
struct offset_base {
  virtual ~offset_base() = default;
  int padding = 0;
};
struct polymorphic_child : offset_base, m::object_type<polymorphic_child, polymorphic_base> {
  static constexpr std::string_view type_name = "polymorphic_child";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 31});
  std::int64_t number = 7;
};
bool polymorphic_references() {
  auto created = m::create_registry();
  if (!created)
    return false;
  auto registry = *created;
  if (m::type_builder<polymorphic_base>{registry}.polymorphic().commit().failed() ||
      m::type_builder<polymorphic_child>{registry}
          .base<polymorphic_base>()
          .property<&polymorphic_child::number>(m::property_id{1}, "number")
          .serialization()
          .commit()
          .failed() ||
      m::type_builder<reference_target>{registry}
          .property<&reference_target::number>(m::property_id{1}, "number")
          .serialization()
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return false;
  auto input = e::make_ref<polymorphic_child>();
  input->number = 42;
  e::ref<const polymorphic_base> original = input;
  auto encoded = m::serialize(registry, original);
  if (!encoded)
    return false;
  auto decoded = m::deserialize<e::ref<const polymorphic_base>>(registry, *encoded);
  if (!decoded || !*decoded ||
      (*decoded)->dynamic_type() != e::runtime_type_of<polymorphic_child>() ||
      (*decoded).get() == original.get() ||
      static_cast<const polymorphic_child*>((*decoded)->dynamic_address())->number != 42)
    return false;
  auto view = m::instance::from(registry, *decoded);
  if (!view)
    return false;
  auto target = view->as_reference()->get();
  if (!target || target->type().id() != e::runtime_type_of<polymorphic_child>() ||
      !target->property("number"))
    return false;
  std::string text(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  auto position = text.find(polymorphic_child::type_guid.string());
  if (position == std::string::npos)
    return false;
  text.replace(position, 36, reference_target::type_guid.string());
  auto unrelated = m::deserialize<e::ref<const polymorphic_base>>(
      registry, std::as_bytes(std::span{text.data(), text.size()}));
  if (unrelated)
    return false;
  text.replace(position, 36, e::guid::from_bytes({94, 99}).string());
  auto unknown = m::deserialize<e::ref<const polymorphic_base>>(
      registry, std::as_bytes(std::span{text.data(), text.size()}));
  if (unknown || unknown.error().error != e::error::not_found)
    return false;
  auto before = original.get();
  auto destination = m::mutable_instance::from(registry, original);
  if (!destination ||
      m::deserialize_instance(registry, *destination,
                              std::as_bytes(std::span{text.data(), text.size()}))
          .error.ok() ||
      original.get() != before)
    return false;
  e::ref<polymorphic_child> concrete = e::make_ref<polymorphic_child>();
  auto concrete_bytes = m::serialize(registry, concrete);
  if (!concrete_bytes)
    return false;
  const std::string_view concrete_text{reinterpret_cast<const char*>(concrete_bytes->data()),
                                       concrete_bytes->size()};
  if (concrete_text.find("$type_guid") != std::string_view::npos)
    return false;
  std::vector<e::ref<const polymorphic_base>> repeated{original, original};
  auto repeated_bytes = m::serialize(registry, repeated);
  if (!repeated_bytes)
    return false;
  auto repeated_copy = m::deserialize<decltype(repeated)>(registry, *repeated_bytes);
  if (!repeated_copy || repeated_copy->size() != 2 || !(*repeated_copy)[0] ||
      !(*repeated_copy)[1] || (*repeated_copy)[0].get() == (*repeated_copy)[1].get())
    return false;
  e::ref<const polymorphic_base> empty;
  auto null = m::serialize(registry, empty);
  if (!null)
    return false;
  auto restored_null = m::deserialize<e::ref<const polymorphic_base>>(registry, *null);
  return restored_null && !*restored_null;
}
struct checked_value {
  static constexpr std::string_view type_name = "checked_value";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 40});
  std::int64_t number = 1;
};
struct bound_properties {
  static constexpr std::string_view type_name = "bound_properties";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 41});
  std::vector<std::int64_t> values{1};
  checked_value child;
  int calls = 0;
  const auto& get_values() const { return values; }
  const auto& get_child() const { return child; }
  e::error set_values(const std::vector<std::int64_t>& input) {
    ++calls;
    for (auto value : input)
      if (value < 0)
        return e::error::invalid_argument;
    auto copy = input;
    values.swap(copy);
    return e::error::success;
  }
  e::error set_child(const checked_value& input) {
    ++calls;
    if (input.number < 0)
      return e::error::invalid_argument;
    child = input;
    return e::error::success;
  }
};
bool bound_property_contract() {
  auto registry = m::create_registry();
  if (!registry ||
      m::type_builder<checked_value>{*registry}
          .property<&checked_value::number>(m::property_id{1}, "number")
          .serialization()
          .commit()
          .failed() ||
      m::type_builder<bound_properties>{*registry}
          .property<&bound_properties::get_values, &bound_properties::set_values>(m::property_id{1},
                                                                                  "values")
          .property<&bound_properties::get_child, &bound_properties::set_child>(m::property_id{2},
                                                                                "child")
          .serialization()
          .commit()
          .failed() ||
      m::freeze(*registry).failed())
    return false;
  bound_properties object;
  auto root = m::mutable_instance::from(*registry, object);
  if (!root)
    return false;
  auto values = root->property("values"), child = root->property("child");
  if (!values || !child || values->as_sequence().error() != e::error::unsupported ||
      child->object_data().error() != e::error::unsupported ||
      child->property("number").error() != e::error::unsupported ||
      !values->as_const().as_sequence() || !child->as_const().property("number"))
    return false;
  auto decode = [&](const m::mutable_instance& target, std::string_view json) {
    return m::deserialize_instance(*registry, target,
                                   std::as_bytes(std::span{json.data(), json.size()}));
  };
  auto invalid = decode(*values, "[2,\"bad\"]");
  if (invalid.error.ok() || invalid.path != "/1" || object.calls != 0 ||
      object.values != std::vector<std::int64_t>{1})
    return false;
  if (decode(*values, "[2,-3]").error.ok() || object.calls != 1 ||
      object.values != std::vector<std::int64_t>{1})
    return false;
  if (decode(*values, "[2,3]").error.failed() || object.calls != 2 ||
      object.values != std::vector<std::int64_t>{2, 3})
    return false;
  if (decode(*child, R"({"number":-1})").error.ok() || object.child.number != 1)
    return false;
  if (decode(*child, R"({"number":8})").error.failed() || object.child.number != 8)
    return false;
  std::vector<std::int64_t> next{5};
  auto source = m::instance::from(*registry, next);
  if (!source || values->assign(*source).failed() || object.values != next)
    return false;
  next[0] = -5;
  if (values->assign(*source) != e::error::invalid_argument || object.values[0] != 5)
    return false;
  std::vector<float> wrong{1};
  if (values->assign(*m::instance::from(*registry, wrong)) != e::error::invalid_argument)
    return false;
  auto encoded = m::serialize(*registry, object);
  if (!encoded)
    return false;
  auto copy = m::deserialize<bound_properties>(*registry, *encoded);
  return copy && copy->values == object.values && copy->child.number == object.child.number;
}
int main() try {
  if (!bound_property_contract())
    return 42;
  if (!polymorphic_references())
    return 41;
  if (!reference_properties())
    return 40;
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
