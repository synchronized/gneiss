// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <future>
#include <gneiss/engine/core/meta/capabilities/enumeration.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/capabilities/sequence.hpp>
#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/meta/object_type.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/meta/resolve.hpp>
#include <gneiss/engine/core/meta/resolve_path.hpp>
#include <gneiss/engine/core/serialization/builtin_registration.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/core/serialization/value.hpp>
#include <stdexcept>
namespace e = gneiss;
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
  if (!registry || gneiss::serialization::register_builtin_types(registry).failed() ||
      m::registration::type<shared_probe>(registry)
          .property<&shared_probe::number>(gneiss::property_id{1}, "number")
          .property<&shared_probe::nested>(gneiss::property_id{2}, "nested")
          .commit()
          .failed() ||
      m::registration::type<shared_holder>(registry)
          .property<&shared_holder::item>(gneiss::property_id{1}, "item")
          .property<&shared_holder::another>(gneiss::property_id{2}, "another")
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return false;
  shared_holder holder{e::make_ref<shared_probe>()};
  auto view = m::view(registry, holder);
  auto field = view.template capability<m::cap::object>()->member("item");
  if (!field || field->type().kind() != m::type_kind::reference)
    return false;
  // 显式保活独立于槽位；错误类型赋值不得改变原引用。
  auto slot = field->as_reference();
  if (!slot || view.registry() != registry ||
      slot->actual_type().id() != e::runtime_type_of<shared_probe>())
    return false;
  auto retained = slot->owner();
  auto original = holder.item;
  if (!retained || retained.get() != original.get() ||
      slot->assign_owner(m::resolve<unrelated_probe>(registry), original) !=
          e::error::invalid_argument ||
      holder.item != original)
    return false;
  slot->reset();
  if (holder.item || slot->owner() || retained.get() != original.get() ||
      slot->assign_owner(m::resolve<shared_probe>(registry), original).failed() ||
      holder.item != original)
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
  if (field->assign(wrong) != e::error::invalid_argument || holder.item->number != 19 ||
      field->assign(e::value{true}) != e::error::invalid_argument)
    return false;
  e::value empty{e::ref<m::object>{}};
  if (field->assign(empty).failed() || holder.item || field->assign(copy).failed() ||
      holder.item.get() != owner.get())
    return false;
  e::ref<const shared_probe> readonly = holder.item;
  auto const_view = m::view(registry, std::as_const(readonly));
  if (const_view.read_value().error() != e::error::unsupported)
    return false;
  // value 对象按内容恢复独立对象，普通 ref 的内联序列化仍有效。
  holder.another = copy;
  auto file = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, holder);
  auto encoded_value = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, copy);
  auto encoded_empty = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, empty);
  if (!file || !encoded_value || !encoded_empty || m::valid_scalar(copy))
    return false;
  auto restored = gneiss::serialization::json::decode<e::value>(
      gneiss::serialization::read_context{registry}, *encoded_value);
  auto restored_empty = gneiss::serialization::json::decode<e::value>(
      gneiss::serialization::read_context{registry}, *encoded_empty);
  if (!restored || !restored_empty || *restored_empty != empty)
    return false;
  auto restored_owner = std::get<e::ref<m::object>>(restored->payload);
  auto* restored_probe = dynamic_cast<shared_probe*>(restored_owner.get());
  if (!restored_probe || restored_probe->number != 19 || restored_owner == owner)
    return false;
  holder.item->nested = copy;
  auto cycle = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, copy);
  holder.item->nested = e::value{true};
  if (cycle || cycle.error().error != e::error::invalid_argument)
    return false;
  auto malformed = [&](std::string tag) {
    const std::string json =
        "{\"$kind\":\"object\",\"$content\":{\"$type_guid\":\"" + tag + "\",\"$content\":{}}}";
    return gneiss::serialization::json::decode<e::value>(
        gneiss::serialization::read_context{registry},
        std::as_bytes(std::span{json.data(), json.size()}));
  };
  auto unknown = malformed(unrelated_probe::type_guid.string());
  auto wrong_type = malformed(shared_holder::type_guid.string());
  if (unknown || unknown.error().error != e::error::not_found || wrong_type ||
      wrong_type.error().error != e::error::unsupported)
    return false;
  auto unregistered = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, wrong);
  if (unregistered || unregistered.error().error != e::error::not_found)
    return false;
  auto decoded = gneiss::serialization::json::decode<shared_holder>(
      gneiss::serialization::read_context{registry}, *file);
  if (!decoded || !decoded->item || decoded->item->number != 19 || decoded->item == holder.item)
    return false;
  const auto duplicate = std::get<e::ref<m::object>>(decoded->another.payload);
  if (duplicate.get() == decoded->item.get() ||
      dynamic_cast<shared_probe*>(duplicate.get())->number != 19)
    return false;
  std::string deep = "{\"$kind\":\"bool\",\"$content\":true}";
  for (int i = 0; i < 70; ++i)
    deep = "{\"$kind\":\"object\",\"$content\":{\"$type_guid\":\"" +
           shared_probe::type_guid.string() + "\",\"$content\":{\"number\":1,\"nested\":" + deep +
           "}}}";
  auto too_deep = gneiss::serialization::json::decode<e::value>(
      gneiss::serialization::read_context{registry},
      std::as_bytes(std::span{deep.data(), deep.size()}));
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
  const auto& registry = made;
  enum class unknown_mode { value };
  unknown_mode unknown{};
  auto unknown_view = m::view(registry, std::as_const(unknown));
  if (!unknown_view.type().capability<m::cap::enumeration>() ||
      !unknown_view.type().capability<m::cap::enumeration>().entries().empty())
    return false;
  if (m::registration::type<enum_object>(registry)
          .property<&enum_object::left>(gneiss::property_id{1}, "left")
          .property<&enum_object::right>(gneiss::property_id{2}, "right")
          .commit()
          .failed())
    return false;
  if (m::registration::enumeration<mode>(registry)
              .value("same", mode::first)
              .value("same", mode::second)
              .commit() != e::error::invalid_argument ||
      m::registration::enumeration<mode>(registry)
              .value("one", mode::first)
              .value("alias", mode::first)
              .commit() != e::error::invalid_argument)
    return false;
  auto builder = m::registration::enumeration<mode>(registry);
  std::string label = "First";
  constexpr auto mode_guid = e::guid::literal("63054283-3bd1-4b50-9167-3417bb04a85f");
  builder.name("Mode")
      .guid(mode_guid)
      .schema_version(3)
      .metadata(std::string{"mode metadata"})
      .value(label, mode::first)
      .value("Second", mode::second);
  label = "changed";
  if (builder.commit().failed())
    return false;
  // 枚举和普通类型共用身份唯一性，失败不能留下部分注册数据。
  struct collision_type {};
  if (m::registration::type<collision_type>(registry).name("collision").guid(mode_guid).commit() !=
          e::error::invalid_argument ||
      m::registration::enumeration<unknown_mode>(registry)
              .guid(mode_guid)
              .value("value", unknown_mode::value)
              .commit() != e::error::invalid_argument ||
      m::resolve<collision_type>(registry).registered() ||
      m::resolve<unknown_mode>(registry).registered())
    return false;
  constexpr auto other_guid = e::guid::literal("b48fa732-f59e-4770-8be8-e5ad255bb3a5");
  if (m::registration::type<collision_type>(registry)
          .name("collision")
          .guid(other_guid)
          .commit()
          .failed() ||
      m::registration::enumeration<unknown_mode>(registry)
              .guid(other_guid)
              .value("value", unknown_mode::value)
              .commit() != e::error::invalid_argument ||
      m::registration::enumeration<unknown_mode>(registry)
              .metadata(1)
              .metadata(2)
              .value("value", unknown_mode::value)
              .commit() != e::error::invalid_argument ||
      m::resolve<unknown_mode>(registry).registered())
    return false;
  if (builder.commit() != e::error::invalid_state ||
      m::registration::enumeration<mode>(registry).value("duplicate", mode::first).commit() !=
          e::error::invalid_argument ||
      m::freeze(registry).failed())
    return false;
  const auto enum_type = m::resolve<mode>(registry);
  const auto* extension = enum_type.extension<std::string>();
  if (!enum_type.registered() || enum_type.guid() != mode_guid || enum_type.name() != "Mode" ||
      enum_type.schema_version() != 3 || !extension || *extension != "mode metadata" ||
      m::resolve(registry, mode_guid).id() != enum_type.id())
    return false;
  auto type = m::resolve<enum_object>(registry);
  if (!type)
    return false;
  auto left_entries = type.template capability<m::cap::object>()
                          .properties()[0]
                          .type()
                          .capability<m::cap::enumeration>()
                          .entries();
  auto right_entries = type.template capability<m::cap::object>()
                           .properties()[1]
                           .type()
                           .capability<m::cap::enumeration>()
                           .entries();
  if (type.template capability<m::cap::object>().properties()[0].type().id() !=
          e::runtime_type_of<mode>() ||
      left_entries.size() != 2 || left_entries.data() != right_entries.data() ||
      left_entries[0].name != "First" || std::get<std::int64_t>(left_entries[0].data.payload) != -1)
    return false;
  if (!unknown_view.type().capability<m::cap::enumeration>().entries().empty())
    return false;
  std::vector<mode> modes{mode::first};
  auto sequence = m::view(registry, std::as_const(modes));
  if (sequence.type().is_enum())
    return false;
  auto element_type = sequence.type().capability<m::cap::sequence>().element_type();
  if (!element_type || !element_type.is_enum() || element_type.id() != e::runtime_type_of<mode>())
    return false;
  auto entries = element_type.capability<m::cap::enumeration>().entries();
  auto elements = sequence.as_sequence();
  if (entries.size() != 2 || !elements)
    return false;
  auto element = elements->at(0);
  if (!element || !element->type().is_enum() ||
      element->type().capability<m::cap::enumeration>().entries().empty())
    return false;
  if (!m::type{}.capability<m::cap::enumeration>().entries().empty())
    return false;
  enum_object object;
  auto view = m::view(registry, object);
  auto field = view.template capability<m::cap::object>()->member("left");
  if (!field || !field->type().is_enum() ||
      field->type().capability<m::cap::enumeration>().entries().empty() ||
      field->assign(e::value{std::int64_t{2}}).failed() || object.left != mode::second ||
      field->assign(e::value{std::int64_t{128}}) != e::error::invalid_argument ||
      object.left != mode::second)
    return false;
  return m::registration::enumeration<mode>(registry).value("late", mode::first).commit() ==
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
concept can_borrow = requires(T&& value) { m::view(std::as_const(std::forward<T>(value))); };
template <class T>
concept can_mutate = requires(T&& value) {
  { m::view(std::forward<T>(value)) } -> std::same_as<m::instance>;
};
static_assert(can_borrow<object&> && can_borrow<const object&>);
static_assert(!can_borrow<object> && !can_borrow<const object>);
static_assert(can_mutate<object&> && !can_mutate<const object&> && !can_mutate<object>);
static_assert(!std::is_constructible_v<m::instance, m::const_instance>);
// 实例仅保存类型句柄和访问位置，不重复缓存类型身份。
static_assert(sizeof(m::const_instance) <= sizeof(m::type) + 4 * sizeof(void*));
static_assert(sizeof(m::instance) == sizeof(m::const_instance));
template <class T>
concept can_write_instance = requires(T target, const e::value& input) { target.assign(input); };
static_assert(can_write_instance<m::instance> && !can_write_instance<m::const_instance>);
static_assert(std::same_as<m::instance, m::basic_instance<true>>);
static_assert(std::same_as<m::const_instance, m::basic_instance<false>>);
static_assert(std::same_as<decltype(m::view(std::declval<const object&>())), m::const_instance>);
static_assert(
    std::same_as<decltype(m::view(std::declval<const m::registry_ref&>(), std::declval<object&>())),
                 m::instance>);
static_assert(std::same_as<decltype(m::view(std::declval<const m::registry_ref&>(),
                                            std::declval<const object&>())),
                           m::const_instance>);

struct query_model {
  static constexpr std::string_view type_name = "query_model";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 4});
  std::vector<mode> modes;
  std::int8_t count{};
};
bool type_queries() {
  auto registry = m::create_registry();
  if (!registry || !m::resolve<float>(registry) || m::resolve(registry, query_model::type_guid))
    return false;
  if (m::registration::enumeration<mode>(registry).value("first", mode::first).commit().failed() ||
      m::registration::type<query_model>(registry)
          .property<&query_model::modes>(gneiss::property_id{1}, "modes")
          .property<&query_model::count>(gneiss::property_id{2}, "count")
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return false;
  if (m::freeze().failed())
    return false;
  auto default_float = m::resolve<float>();
  if (!default_float || !m::resolve(e::runtime_type_of<float>()) ||
      !m::resolve(default_float.guid()))
    return false;
  auto typed = m::resolve<const query_model&>(registry);
  auto by_id = m::resolve(registry, e::runtime_type_of<query_model>());
  auto by_guid = m::resolve(registry, query_model::type_guid);
  if (!typed || !by_id || !by_guid || typed != by_id || typed != by_guid)
    return false;
  auto named = typed.template capability<m::cap::object>().find_property("count");
  auto numbered = typed.template capability<m::cap::object>().find_property(gneiss::property_id{2});
  if (typed.name() != query_model::type_name || !named || !numbered ||
      named.id() != numbered.id() || named.type().id() != e::runtime_type_of<std::int8_t>() ||
      typed.template capability<m::cap::object>().find_property("missing") ||
      typed.template capability<m::cap::object>().find_property(gneiss::property_id{}) ||
      m::type{}.template capability<m::cap::object>().find_property("count") ||
      default_float.template capability<m::cap::object>().find_property("count") ||
      !m::type{}.name().empty() ||
      !m::type{}.template capability<m::cap::object>().properties().empty() ||
      !default_float.template capability<m::cap::object>().properties().empty())
    return false;
  auto props = typed.template capability<m::cap::object>().properties();
  if (props.empty() || props.size() != 2 || props[0].type().kind() != m::type_kind::sequence ||
      props[1].type().id() != e::runtime_type_of<std::int8_t>())
    return false;
  auto sequence = m::resolve(registry, e::runtime_type_of<std::vector<mode>>());
  auto element = sequence.capability<m::cap::sequence>().element_type();
  auto enumeration = m::resolve(registry, e::runtime_type_of<mode>());
  if (!element || !element.is_enum() || !enumeration ||
      element.capability<m::cap::enumeration>().entries().data() !=
          enumeration.capability<m::cap::enumeration>().entries().data())
    return false;
  auto scalar = m::resolve<float>(registry);
  if (!scalar || !m::resolve(registry, scalar.guid()) || !m::resolve<settings>(registry) ||
      m::resolve(registry, e::runtime_type_of<settings>()) || m::resolve(registry, e::guid{}) ||
      m::resolve(m::registry_ref{}, e::runtime_type_of<float>()))
    return false;
  // 类型句柄比较含注册表身份，不依赖模板静态描述的地址。
  auto isolated = m::create_registry();
  if (!isolated || m::resolve<float>(isolated) == scalar ||
      m::resolve<float>(isolated).id() != scalar.id() || scalar == typed ||
      m::type{} != m::type{} || scalar == m::type{} || m::type{}.capability<m::cap::sequence>() ||
      scalar.capability<m::cap::sequence>() || element != enumeration)
    return false;
  const auto unregistered = m::resolve<settings>(registry);
  if (!unregistered || unregistered.registered() || unregistered != m::resolve<settings>(registry))
    return false;
  auto object_capability = unregistered.capability<m::cap::object>();
  settings source{};
  auto source_view = m::view(registry, source);
  auto other_view = m::view(isolated, source);
  if (!object_capability || !object_capability.properties().empty() ||
      m::type{}.capability<m::cap::object>() || scalar.capability<m::cap::object>() ||
      !object_capability.bind(source_view) ||
      object_capability.bind(other_view).error() != e::error::invalid_argument ||
      m::cap::object{}.bind(source_view).error() != e::error::unsupported ||
      object_capability.bind(m::const_instance{}).error() != e::error::invalid_state)
    return false;
  static_assert(std::same_as<decltype(source_view.as_const().capability<m::cap::object>()),
                             e::result<m::const_object_view>>);
  static_assert(
      std::same_as<decltype(source_view.capability<m::cap::object>()), e::result<m::object_view>>);
  // 类型视图保活注册表，但不要求对象实例存活。
  std::weak_ptr<m::type_registry_state> weak = registry;
  registry.reset();
  return !weak.expired() && typed.template capability<m::cap::object>().properties().size() == 2 &&
         typed.name() == query_model::type_name && named.name() == "count";
}
// 原生描述由注册记录借用；查询过程不写入冻结注册表。
bool registered_native_queries() {
  auto created = m::create_registry();
  if (!created)
    return false;
  const auto registry = created;
  using sequence = std::array<float, 2>;
  using mapping = std::map<std::string, float>;
  using optional = std::optional<float>;
  using reference = e::ref<settings>;
  using variant = std::variant<float, int>;
  const auto before = m::resolve<sequence>(registry);
  if (m::registration::type<sequence>(registry).name("sequence").commit().failed() ||
      m::registration::type<mapping>(registry).name("mapping").commit().failed() ||
      m::registration::type<optional>(registry).name("optional").commit().failed() ||
      m::registration::type<reference>(registry).name("reference").commit().failed() ||
      m::registration::type<variant>(registry).name("variant").commit().failed())
    return false;
  const auto runtime_guid = e::guid::literal("947510ef-4f5b-4ac1-a608-a01134927f40");
  if (m::register_type(registry, {.guid = runtime_guid, .schema_version = 1, .name = "runtime"})
          .failed() ||
      m::freeze(registry).failed())
    return false;
  auto check = [&]<class T>() {
    const auto native = m::resolve<T>(registry);
    const auto lookup = m::resolve(registry, e::runtime_type_of<T>());
    if (!(native == lookup && native.registered() && lookup.registered() &&
          native.kind() == lookup.kind() && native.scalar_kind() == lookup.scalar_kind()))
      return false;
    using C = std::conditional_t<
        std::is_same_v<T, sequence>, m::cap::sequence,
        std::conditional_t<
            std::is_same_v<T, mapping>, m::cap::associative,
            std::conditional_t<std::is_same_v<T, optional>, m::cap::optional, m::cap::reference>>>;
    if constexpr (!std::is_same_v<T, variant>)
      return native.template capability<C>().element_type() ==
             lookup.template capability<C>().element_type();
    else
      return bool(native.template capability<m::cap::variant>());
  };
  if (!check.template operator()<sequence>() || !check.template operator()<mapping>() ||
      !check.template operator()<optional>() || !check.template operator()<reference>() ||
      !check.template operator()<variant>() || before.registered() ||
      !m::resolve(registry, e::runtime_type_of<sequence>())
           .capability<m::cap::sequence>()
           .is_fixed_size())
    return false;
  const auto sequence_type = m::resolve<sequence>(registry).capability<m::cap::sequence>();
  if (!sequence_type || sequence_type.element_type() != m::resolve<float>(registry) ||
      m::resolve<int>(registry).capability<m::cap::sequence>())
    return false;
  const auto runtime = m::resolve(registry, runtime_guid);
  if (!runtime || !runtime.registered() || runtime.id() || runtime.capability<m::cap::sequence>() ||
      runtime.kind() != m::type_kind::object)
    return false;
  const auto types = m::registered_types(registry);
  if (!types)
    return false;
  for (const auto entry : *types) {
    if (entry.id() == e::runtime_type_of<sequence>() &&
        (!entry.capability<m::cap::sequence>().is_fixed_size() ||
         entry.capability<m::cap::sequence>().element_type() != m::resolve<float>(registry)))
      return false;
  }
  return true;
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
  auto registry = made;
  if (m::registration::type<reference_target>(registry)
          .property<&reference_target::number>(gneiss::property_id{1}, "number")
          .property<&reference_target::next>(gneiss::property_id{2}, "next")
          .commit()
          .failed() ||
      m::registration::type<reference_owner>(registry)
          .property<&reference_owner::target>(gneiss::property_id{1}, "target")
          .property<&reference_owner::readonly>(gneiss::property_id{2}, "readonly")
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return false;
  reference_owner owner;
  auto root = m::view(registry, owner);
  auto field = root.template capability<m::cap::object>()->member("target");
  if (!field || field->type().kind() != m::type_kind::reference ||
      field->as_reference()->get().error() != e::error::not_found ||
      field->read_value().error() != e::error::unsupported)
    return false;
  auto target_type = field->type().capability<m::cap::reference>().element_type();
  if (!target_type || target_type.id() != e::runtime_type_of<reference_target>())
    return false;
  auto target = e::make_ref<reference_target>();
  auto source = m::view(registry, std::as_const(target));
  if (field->assign(source).failed() || owner.target.get() != target.get())
    return false;
  auto pointed = field->as_reference()->get();
  if (!pointed)
    return false;
  auto number = pointed->template capability<m::cap::object>()->member("number");
  if (!number || number->assign(e::value{std::int64_t{42}}).failed() || target->number != 42)
    return false;
  owner.readonly = target;
  auto readonly = root.template capability<m::cap::object>()->member("readonly");
  if (!readonly || readonly->as_reference()->get().error() != e::error::unsupported ||
      !readonly->as_const().as_reference()->get() ||
      field->assign(readonly->as_const()) != e::error::invalid_argument)
    return false;
  auto reference = field->as_reference();
  if (!reference || !reference->has_value() ||
      root.as_reference().error() != e::error::unsupported ||
      m::const_instance{}.as_reference().error() != e::error::invalid_state)
    return false;
  e::ref<reference_target> copied;
  auto copied_instance = m::view(registry, copied);
  auto copied_view = copied_instance.as_reference();
  if (!copied_view || copied_view->has_value() ||
      copied_view->assign(reference->as_const()).failed() || copied.get() != target.get() ||
      copied_view->assign(readonly->as_reference()->as_const()) != e::error::invalid_argument)
    return false;
  copied_view->reset();
  if (copied || !owner.target)
    return false;
  // 引用按值内联保存；文件内重复引用与再次加载均产生独立对象。
  auto encoded = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, owner);
  if (!encoded)
    return false;
  auto restored = gneiss::serialization::json::decode<reference_owner>(
      gneiss::serialization::read_context{registry}, *encoded);
  auto independent = gneiss::serialization::json::decode<reference_owner>(
      gneiss::serialization::read_context{registry}, *encoded);
  if (!restored || !independent || !restored->target ||
      restored->target.get() == restored->readonly.get() || restored->target->number != 42 ||
      restored->target.get() == independent->target.get())
    return false;
  target->next = target;
  auto cycle = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, owner);
  target->next.reset();
  if (cycle || cycle.error().error != e::error::invalid_argument)
    return false;
  const auto bad = [&](std::string input) {
    const auto before = reference_target::alive;
    auto result = gneiss::serialization::json::decode<reference_owner>(
        gneiss::serialization::read_context{registry},
        std::as_bytes(std::span{input.data(), input.size()}));
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
  auto decoded = gneiss::serialization::json::decode<reference_owner>(
      gneiss::serialization::read_context{registry},
      std::as_bytes(std::span{input.data(), input.size()}));
  if (!decoded || decoded->target || decoded->readonly)
    return false;
  field->as_reference()->reset();
  if (owner.target || !owner.readonly || target->number != 42)
    return false;
  if (field->as_reference()->get().error() != e::error::not_found)
    return false;
  readonly->as_reference()->reset();
  return !owner.readonly;
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
struct missing_dynamic_type : m::object {
  const void* dynamic_address() const noexcept override { return this; }

protected:
  const m::type_ops* dynamic_ops() const noexcept override { return nullptr; }
};
bool polymorphic_references() {
  auto created = m::create_registry();
  if (!created)
    return false;
  auto registry = created;
  if (m::registration::type<polymorphic_base>(registry)
          .metadata(gneiss::serialization::reference_policy{.polymorphic = true})
          .commit()
          .failed() ||
      m::registration::type<polymorphic_child>(registry)
          .base<polymorphic_base>()
          .property<&polymorphic_child::number>(gneiss::property_id{1}, "number")
          .commit()
          .failed() ||
      m::registration::type<reference_target>(registry)
          .property<&reference_target::number>(gneiss::property_id{1}, "number")
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return false;
  polymorphic_child native;
  const m::object& base = native;
  const auto detached = base.dynamic_type(m::registry_ref{});
  const auto registered = base.dynamic_type(registry);
  auto other = m::create_registry();
  if (!other || !detached || detached.registered() || !registered.registered() ||
      registered != m::resolve<polymorphic_child>(registry) ||
      base.dynamic_type(other).registered() || base.dynamic_type(other).id() != detached.id() ||
      detached.guid() != polymorphic_child::type_guid || base.dynamic_address() != &native ||
      !base.dynamic_type() || missing_dynamic_type{}.dynamic_type(registry))
    return false;
  auto capability = registered.capability<m::cap::object>();
  if (capability.base_type() != m::resolve<polymorphic_base>(registry) ||
      !capability.can_construct())
    return false;
  auto owner = capability.construct();
  if (!owner)
    return false;
  auto* child = static_cast<polymorphic_child*>(owner->get());
  auto root = m::instance::from_object(registered, child);
  auto object_view = root->capability<m::cap::object>();
  auto parent = object_view->cast(capability.base_type());
  auto object = object_view->cast(m::resolve<m::object>(registry));
  if (!parent || !object || *parent->object_data() != static_cast<polymorphic_base*>(child) ||
      *object->object_data() != static_cast<m::object*>(child) ||
      detached.capability<m::cap::object>().can_construct())
    return false;
  auto readonly = root->as_const().capability<m::cap::object>()->cast(capability.base_type());
  static_assert(std::is_same_v<decltype(readonly), e::result<m::const_instance>>);
  if (!readonly || *readonly->object_data() != static_cast<const polymorphic_base*>(child) ||
      object_view->cast(m::resolve<polymorphic_base>(other)).error() !=
          e::error::invalid_argument ||
      capability.is_a(m::resolve<int>(registry)))
    return false;
  auto input = e::make_ref<polymorphic_child>();
  input->number = 42;
  e::ref<const polymorphic_base> original = input;
  auto encoded = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, original);
  if (!encoded)
    return false;
  auto decoded = gneiss::serialization::json::decode<e::ref<const polymorphic_base>>(
      gneiss::serialization::read_context{registry}, *encoded);
  if (!decoded || !*decoded ||
      (*decoded)->dynamic_type().id() != e::runtime_type_of<polymorphic_child>() ||
      (*decoded).get() == original.get() ||
      static_cast<const polymorphic_child*>((*decoded)->dynamic_address())->number != 42)
    return false;
  auto view = m::view(registry, std::as_const(*decoded));
  auto target = view.as_reference()->get();
  if (!target || target->type().id() != e::runtime_type_of<polymorphic_child>() ||
      !target->template capability<m::cap::object>()->member("number"))
    return false;
  std::string text(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  auto position = text.find(polymorphic_child::type_guid.string());
  if (position == std::string::npos)
    return false;
  text.replace(position, 36, reference_target::type_guid.string());
  auto unrelated = gneiss::serialization::json::decode<e::ref<const polymorphic_base>>(
      gneiss::serialization::read_context{registry},
      std::as_bytes(std::span{text.data(), text.size()}));
  if (unrelated)
    return false;
  text.replace(position, 36, e::guid::from_bytes({94, 99}).string());
  auto unknown = gneiss::serialization::json::decode<e::ref<const polymorphic_base>>(
      gneiss::serialization::read_context{registry},
      std::as_bytes(std::span{text.data(), text.size()}));
  if (unknown || unknown.error().error != e::error::not_found)
    return false;
  auto before = original.get();
  auto destination = m::view(registry, original);
  if (gneiss::serialization::json::read_into(gneiss::serialization::read_context{registry},
                                             destination,
                                             std::as_bytes(std::span{text.data(), text.size()}))
          .has_value() ||
      original.get() != before)
    return false;
  e::ref<polymorphic_child> concrete = e::make_ref<polymorphic_child>();
  auto concrete_bytes = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, concrete);
  if (!concrete_bytes)
    return false;
  const std::string_view concrete_text{reinterpret_cast<const char*>(concrete_bytes->data()),
                                       concrete_bytes->size()};
  if (concrete_text.find("$type_guid") != std::string_view::npos)
    return false;
  std::vector<e::ref<const polymorphic_base>> repeated{original, original};
  auto repeated_bytes = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, repeated);
  if (!repeated_bytes)
    return false;
  auto repeated_copy = gneiss::serialization::json::decode<decltype(repeated)>(
      gneiss::serialization::read_context{registry}, *repeated_bytes);
  if (!repeated_copy || repeated_copy->size() != 2 || !(*repeated_copy)[0] ||
      !(*repeated_copy)[1] || (*repeated_copy)[0].get() == (*repeated_copy)[1].get())
    return false;
  e::ref<const polymorphic_base> empty;
  auto null = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, empty);
  if (!null)
    return false;
  auto restored_null = gneiss::serialization::json::decode<e::ref<const polymorphic_base>>(
      gneiss::serialization::read_context{registry}, *null);
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
      m::registration::type<checked_value>(registry)
          .property<&checked_value::number>(gneiss::property_id{1}, "number")
          .commit()
          .failed() ||
      m::registration::type<bound_properties>(registry)
          .property<&bound_properties::get_values, &bound_properties::set_values>(
              gneiss::property_id{1}, "values")
          .property<&bound_properties::get_child, &bound_properties::set_child>(
              gneiss::property_id{2}, "child")
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return false;
  bound_properties object;
  auto root = m::view(registry, object);
  auto values = root.template capability<m::cap::object>()->member("values"),
       child = root.template capability<m::cap::object>()->member("child");
  if (!values || !child || values->as_sequence().error() != e::error::unsupported ||
      child->object_data().error() != e::error::unsupported ||
      child->capability<m::cap::object>().error() != e::error::unsupported ||
      !values->as_const().as_sequence() ||
      !child->as_const().template capability<m::cap::object>()->member("number"))
    return false;
  auto decode = [&](const m::instance& target, std::string_view json) {
    return gneiss::serialization::json::read_into(
        gneiss::serialization::read_context{registry}, target,
        std::as_bytes(std::span{json.data(), json.size()}));
  };
  auto invalid = decode(*values, "[2,\"bad\"]");
  if (invalid.has_value() || invalid.error().path != "/1" || object.calls != 0 ||
      object.values != std::vector<std::int64_t>{1})
    return false;
  if (decode(*values, "[2,-3]").has_value() || object.calls != 1 ||
      object.values != std::vector<std::int64_t>{1})
    return false;
  if (decode(*values, "[2,3]").failed() || object.calls != 2 ||
      object.values != std::vector<std::int64_t>{2, 3})
    return false;
  if (decode(*child, R"({"number":-1})").has_value() || object.child.number != 1)
    return false;
  if (decode(*child, R"({"number":8})").failed() || object.child.number != 8)
    return false;
  std::vector<std::int64_t> next{5};
  auto source = m::view(registry, std::as_const(next));
  if (values->assign(source).failed() || object.values != next)
    return false;
  next[0] = -5;
  if (values->assign(source) != e::error::invalid_argument || object.values[0] != 5)
    return false;
  std::vector<float> wrong{1};
  if (values->assign(m::view(registry, std::as_const(wrong))) != e::error::invalid_argument)
    return false;
  auto encoded = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{registry}, object);
  if (!encoded)
    return false;
  auto copy = gneiss::serialization::json::decode<bound_properties>(
      gneiss::serialization::read_context{registry}, *encoded);
  return copy && copy->values == object.values && copy->child.number == object.child.number;
}
struct property_child : offset_base, m::object_type<property_child, shared_probe> {
  static constexpr std::string_view type_name = "property_child";
  static constexpr e::guid type_guid = e::guid::literal("925efcc2-a21f-44d6-9c12-0f0f7e383a2c");
};
bool property_handles() {
  auto register_probe = [](const m::registry_ref& registry) {
    return m::registration::type<shared_probe>(registry)
               .property<&shared_probe::number>(gneiss::property_id{1}, "number")
               .commit()
               .ok() &&
           m::registration::type<shared_holder>(registry)
               .property<&shared_holder::item>(gneiss::property_id{1}, "item")
               .commit()
               .ok() &&
           m::registration::type<property_child>(registry).base<shared_probe>().commit().ok() &&
           m::freeze(registry).ok();
  };
  auto first = m::create_registry(), second = m::create_registry();
  if (!first || !second || !register_probe(first) || !register_probe(second))
    return false;
  shared_probe probe;
  shared_holder wrong;
  auto view = m::view(first, probe);
  auto foreign = m::view(second, std::as_const(probe));
  auto unrelated = m::view(first, std::as_const(wrong));
  auto type = m::resolve<shared_probe>(first);
  if (!type)
    return false;
  auto fields = type.template capability<m::cap::object>().properties();
  if (fields.empty() || fields.size() != 1)
    return false;
  auto field = fields[0];
  if (field.declaring_type() != type ||
      field.declaring_type() == m::resolve<shared_probe>(second) || m::property{}.declaring_type())
    return false;
  auto member = field.get(view);
  if (!member || member->assign(23).failed() || probe.number != 23)
    return false;
  if (field.get(foreign).error() != e::error::invalid_argument ||
      field.get(unrelated).error() != e::error::invalid_argument || !field || m::property{} ||
      m::property{}.get(view.as_const()).error() != e::error::invalid_state)
    return false;
  {
    property_child child;
    auto derived = m::view(first, child);
    auto inherited = field.get(derived);
    if (!inherited || inherited->assign(e::value{std::int64_t{42}}).failed() || child.number != 42)
      return false;
    auto inherited_fields =
        derived.as_const().type().template capability<m::cap::object>().properties();
    if (inherited_fields.empty() || inherited_fields[0].declaring_type() != type)
      return false;
  }
  // 范围、迭代器和独立句柄均保活表，但没有持有 probe。
  std::weak_ptr<m::type_registry_state> weak = first;
  auto iterator = fields.begin();
  first = {};
  view = {};
  unrelated = {};
  type = {};
  fields = {};
  *member = {};
  if (weak.expired() || iterator->name() != "number" || field.name() != "number")
    return false;
  iterator = {};
  if (weak.expired())
    return false;
  field = {};
  return weak.expired();
}
bool capability_binding() {
  auto first = m::create_registry();
  auto second = m::create_registry();
  if (!first || !second || m::freeze(first).failed() || m::freeze(second).failed())
    return false;
  std::vector<int> values{1, 2};
  auto source = m::view(first, values);
  auto other_context = m::view(second, values);
  const int scalar = 1;
  auto other_type = m::view(first, scalar);
  const auto capability = source.type().capability<m::cap::sequence>();
  if (!capability || capability.is_fixed_size())
    return false;
  auto mutable_view = capability.bind(source);
  auto readonly_view = capability.bind(source.as_const());
  static_assert(std::is_same_v<decltype(readonly_view), e::result<m::const_sequence_view>>);
  if (!mutable_view || !readonly_view || mutable_view->resize(3).failed() ||
      readonly_view->size() != 3)
    return false;
  auto direct = source.capability<m::cap::sequence>();
  auto mismatch = capability.bind(other_type);
  auto foreign = capability.bind(other_context);
  auto missing = other_type.type().capability<m::cap::sequence>();
  auto unsupported = missing.bind(source);
  return direct && direct->size() == 3 && !mismatch &&
         mismatch.error() == e::error::invalid_argument && !foreign &&
         foreign.error() == e::error::invalid_argument && !missing && !unsupported &&
         unsupported.error() == e::error::unsupported;
}

// 移动入口处理对象内容；复制入口不应悄悄消耗不可复制的来源。
struct move_probe {
  std::unique_ptr<int> payload;
};
bool move_instances() {
  move_probe source{std::make_unique<int>(42)}, target;
  auto from = m::view(source);
  auto to = m::view(target);
  if (to.assign(from.as_const()) != e::error::unsupported || !source.payload)
    return false;
  if (to.move_from(from).failed() || source.payload || !target.payload || *target.payload != 42)
    return false;
  return to.move_from(to).ok() && target.payload && *target.payload == 42;
}
// 按值 getter 的临时对象必须在递归读取期间存活，并在失败路径释放。
struct alignas(128) computed_record {
  static constexpr std::string_view type_name = "computed_record";
  static constexpr e::guid type_guid = e::guid::from_bytes({95, 31});
  static inline int live = 0;
  int first = 1;
  int second = 2;
  computed_record() { ++live; }
  computed_record(const computed_record& other) : first(other.first), second(other.second) {
    ++live;
  }
  computed_record& operator=(const computed_record&) = default;
  ~computed_record() { --live; }
};
struct computed_owner {
  static constexpr std::string_view type_name = "computed_owner";
  static constexpr e::guid type_guid = e::guid::from_bytes({95, 32});
  computed_record data;
  std::vector<int> numbers{3, 4};
  int writes = 0;
  computed_record get_data() const { return data; }
  e::error set_data(const computed_record& next) {
    if (next.first == 13)
      throw std::runtime_error{"setter probe"};
    if (next.first < 0)
      return e::error::invalid_argument;
    data = next;
    ++writes;
    return e::error::success;
  }
  std::vector<int> get_numbers() const { return numbers; }
  move_probe get_move() const { return {std::make_unique<int>(11)}; }
  void set_move(const move_probe&) {}
  void set_numbers(const std::vector<int>& next) { numbers = next; }
};
struct computed_envelope {
  static constexpr std::string_view type_name = "computed_envelope";
  static constexpr e::guid type_guid = e::guid::from_bytes({95, 33});
  computed_owner data;
  int writes = 0;
  bool reject = false;
  computed_owner get() const { return data; }
  e::error set(const computed_owner& next) {
    if (reject)
      return e::error::invalid_argument;
    data = next;
    ++writes;
    return e::error::success;
  }
};
bool computed_properties() {
  auto registry = m::create_registry();
  if (m::registration::type<move_probe>(registry).name("move_probe").commit().failed())
    return false;
  if (m::registration::type<computed_record>(registry)
          .property<&computed_record::first>(e::property_id{1}, "first")
          .property<&computed_record::second>(e::property_id{2}, "second")
          .commit()
          .failed() ||
      m::registration::type<computed_owner>(registry)
          .property<&computed_owner::get_data, &computed_owner::set_data>(e::property_id{1}, "data")
          .property<&computed_owner::get_numbers, &computed_owner::set_numbers>(e::property_id{2},
                                                                                "numbers")
          .property<&computed_owner::get_move, &computed_owner::set_move>(
              e::property_id{3}, "move")(e::serialization::field_policy{.save = false})
          .commit()
          .failed() ||
      m::registration::type<computed_envelope>(registry)
          .property<&computed_envelope::get, &computed_envelope::set>(e::property_id{1}, "data")
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return false;
  computed_owner owner;
  owner.data.second = 42;
  auto root = m::view(registry, owner);
  auto field = root.capability<m::cap::object>()->member("data");
  auto move_only = root.capability<m::cap::object>()->member("move");
  const e::property_path move_path{e::property_id{3}, e::property_id{1}};
  if (!move_only ||
      m::write_path(root, move_path, e::value{std::int64_t{1}}) != e::error::unsupported)
    return false;
  if (!field || field->capability<m::cap::object>().error() != e::error::unsupported)
    return false;
  const e::property_path path{e::property_id{1}, e::property_id{1}};
  if (m::write_path(root, path, e::value{std::int64_t{7}}).failed() || owner.data.first != 7 ||
      owner.data.second != 42 || owner.writes != 1 || computed_record::live != 1)
    return false;
  auto read = m::read_path(root.as_const(), path);
  if (!read || *read != e::value{std::int64_t{7}} || computed_record::live != 1)
    return false;
  if (m::write_path(root, path, e::value{std::int64_t{-1}}) != e::error::invalid_argument ||
      owner.data.first != 7 || owner.writes != 1 || computed_record::live != 1)
    return false;
  try {
    (void)m::write_path(root, path, e::value{std::int64_t{13}});
    return false;
  } catch (const std::runtime_error&) {
  }
  if (computed_record::live != 1 || owner.writes != 1)
    return false;
  const e::property_path number_path{e::property_id{2}, std::size_t{1}};
  if (m::write_path(root, number_path, e::value{std::int64_t{9}}).failed() ||
      owner.numbers != std::vector<int>{3, 9})
    return false;
  if (m::write_path(root, number_path, e::value{std::int64_t{10}}, true) != e::error::unsupported ||
      owner.numbers != std::vector<int>{3, 9})
    return false;
  const e::property_path invalid_path{e::property_id{1}, e::property_id{99}};
  if (m::write_path(root, invalid_path, e::value{std::int64_t{1}}) != e::error::not_found ||
      owner.writes != 1 || computed_record::live != 1)
    return false;
  auto bytes =
      e::serialization::json::encode_bytes(e::serialization::write_context{registry}, owner);
  auto snapshot = e::serialization::capture_value(e::serialization::write_context{registry}, owner);
  if (!bytes || !snapshot || computed_record::live != 1)
    return false;
  auto restored_snapshot = e::serialization::restore_value<computed_owner>(
      e::serialization::read_context{registry}, *snapshot);
  if (!restored_snapshot || restored_snapshot->data.first != 7 ||
      restored_snapshot->data.second != 42 || restored_snapshot->numbers != owner.numbers)
    return false;
  auto restored = e::serialization::json::decode<computed_owner>(
      e::serialization::read_context{registry}, *bytes);
  if (!restored || restored->data.first != 7 || restored->data.second != 42 ||
      restored->numbers != owner.numbers)
    return false;
  const std::string_view replacement = R"({"first":8})";
  if (e::serialization::json::read_into(
          e::serialization::read_context{registry}, *field,
          std::as_bytes(std::span{replacement.data(), replacement.size()}))
          .failed() ||
      owner.data.second != 2 || owner.data.first != 8)
    return false;
  // 两层计算属性逐层写回；内层失败或外层拒绝均不发布候选。
  computed_envelope envelope;
  envelope.data.data.second = 61;
  auto outer = m::view(registry, envelope);
  const e::property_path nested{e::property_id{1}, e::property_id{1}, e::property_id{1}};
  if (m::write_path(outer, nested, e::value{std::int64_t{5}}).failed() ||
      envelope.data.data.first != 5 || envelope.data.data.second != 61 || envelope.writes != 1)
    return false;
  if (m::write_path(outer, nested, e::value{std::int64_t{-1}}) != e::error::invalid_argument ||
      envelope.data.data.first != 5 || envelope.writes != 1)
    return false;
  envelope.reject = true;
  if (m::write_path(outer, nested, e::value{std::int64_t{6}}) != e::error::invalid_argument ||
      envelope.data.data.first != 5 || envelope.writes != 1)
    return false;
  return true;
}

int main() try {
  if (!computed_properties() || computed_record::live != 0)
    return 50;
  if (!move_instances())
    return 95;
  if (!capability_binding())
    return 60;
  if (!property_handles())
    return 43;
  if (!bound_property_contract())
    return 42;
  if (!polymorphic_references())
    return 41;
  if (!reference_properties())
    return 40;
  if (!type_queries() || !registered_native_queries())
    return 31;
  if (!enum_registration())
    return 30;
  auto created = m::create_registry();
  if (!created)
    return 1;
  auto registry = created;
  object source;
  if (!m::view(registry, std::as_const(source)).type())
    return 2;
  if (m::registration::type<settings>(registry)
          .property<&settings::get_speed, &settings::set_speed>(gneiss::property_id{1}, "speed")
          .commit()
          .failed() ||
      m::registration::type<object>(registry)
          .property<&object::config>(gneiss::property_id{1}, "config")
          .property<&object::get_label, &object::set_label>(gneiss::property_id{2}, "label")
          .readonly<&object::readonly>(gneiss::property_id{3}, "computed")
          .property<&object::config>(gneiss::property_id{4}, "readonly_config",
                                     m::property_access::read_only)
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return 3;
  auto root = m::view(registry, std::as_const(source));
  if (root.type().kind() != m::type_kind::object || root.type().guid() != object::type_guid ||
      root.type().id() != e::runtime_type_of<object>())
    return 4;
  auto entries = root.type().template capability<m::cap::object>().properties();
  if (entries.empty() || entries.size() != 4)
    return 5;
  {
    auto writable_root = m::view(registry, source);
    if (!entries[2].get(root) || entries[2].get(writable_root).error() != e::error::unsupported)
      return 44;
  }
  auto config = root.template capability<m::cap::object>()->member("config");
  if (!config || config->type().guid() != settings::type_guid ||
      config->type().kind() != m::type_kind::object)
    return 6;
  auto speed = config->template capability<m::cap::object>()->member(gneiss::property_id{1});
  if (!speed || speed->type().kind() != m::type_kind::scalar ||
      speed->type().scalar_kind() != e::value_kind::float32 ||
      std::get<float>(speed->read_value()->payload) != 1)
    return 7;
  if (speed->capability<m::cap::object>().error() != e::error::unsupported ||
      root.template capability<m::cap::object>()->member("missing").error() !=
          e::error::not_found ||
      root.read_value().error() != e::error::unsupported ||
      speed->object_data().error() != e::error::unsupported)
    return 8;
  auto writable = m::view(registry, source);
  auto editable_config = writable.template capability<m::cap::object>()->member("config");
  if (!editable_config)
    return 10;
  auto editable_speed = editable_config->template capability<m::cap::object>()->member("speed");
  if (!editable_speed || editable_speed->assign(e::value{3.0F}).failed() ||
      source.config.speed != 3)
    return 11;
  if (editable_speed->assign(e::value{-1.0F}) != e::error::invalid_argument ||
      source.config.speed != 3 ||
      editable_speed->assign(e::value{true}) != e::error::invalid_argument)
    return 12;
  if (writable.template capability<m::cap::object>()->member("computed").error() !=
      e::error::unsupported)
    return 13;
  if (writable.template capability<m::cap::object>()->member("readonly_config").error() !=
      e::error::unsupported)
    return 28;
  auto computed = writable.as_const().template capability<m::cap::object>()->member("computed");
  if (!computed || std::get<float>(computed->read_value()->payload) != 6)
    return 14;
  auto label = root.template capability<m::cap::object>()->member("label");
  if (!label)
    return 15;
  auto snapshot = label->read_value();
  if (!snapshot || std::get<std::string>(snapshot->payload) != "old!")
    return 16;
  source.label = "new";
  if (std::get<std::string>(snapshot->payload) != "old!" ||
      std::get<std::string>(label->read_value()->payload) != "new!")
    return 17;
  auto edit_label = writable.template capability<m::cap::object>()->member("label");
  if (!edit_label || edit_label->assign(e::value{std::string("written")}).failed() ||
      source.label != "written")
    return 18;
  settings replacement{9};
  if (editable_config->move_from(writable) != e::error::invalid_argument ||
      editable_config->move_from(m::view(registry, replacement)).failed() ||
      source.config.speed != 9)
    return 19;
  {
    settings other{7};
    auto borrowed = m::view(registry, std::as_const(other));
    if (editable_config->assign(borrowed).failed() || source.config.speed != 7 ||
        other.speed != 7 || editable_config->assign(root) != e::error::invalid_argument ||
        editable_speed->assign(borrowed) != e::error::invalid_argument)
      return 25;
    object copy;
    auto destination = m::view(registry, copy);
    if (destination.assign(root).failed() || copy.label != source.label ||
        destination.assign(destination.as_const()).failed())
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
  if (!independent || m::freeze(independent).failed() ||
      !m::view(independent, std::as_const(source)).type())
    return 22;
  // 返回的子视图延长元数据寿命，不依赖根视图对象自身。
  auto weak = std::weak_ptr<m::type_registry_state>(registry);
  created.reset();
  registry.reset();
  root = {};
  *config = {};
  writable = {};
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
  entries = {};
  if (!weak.expired())
    return 32;
  return 0;
} catch (...) {
  return 99;
}
