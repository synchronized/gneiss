// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/core/serialization/serialization.hpp"
#include <cstdio>
#include <future>
#include <limits>
namespace e = gneiss::engine;
namespace m = e::meta;
#define CHECK(expression)                                                                          \
  do {                                                                                             \
    if (!(expression)) {                                                                           \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                                \
      return 1;                                                                                    \
    }                                                                                              \
  } while (false)
struct child {
  static constexpr std::string_view type_name = "container_child";
  static constexpr e::guid type_guid = e::guid::from_bytes({95, 1});
  float speed = 2;
  static e::error validate(const child& v) {
    return v.speed > 0 ? e::error::success : e::error::invalid_argument;
  }
};
struct custom {
  static constexpr std::string_view type_name = "container_custom";
  static constexpr e::guid type_guid = e::guid::from_bytes({95, 3});
  float number = 11;
};
e::result<void*, gneiss::engine::serialization::diagnostic>
encode_custom(const e::serialization::context&, e::serialization::writer& output, const custom&) {
  auto root = output.object();
  if (!root || !output.member(root, "custom", output.string("yes")))
    return gneiss::engine::serialization::diagnostic{.error = e::error::out_of_memory};
  return root;
}
e::result<custom, gneiss::engine::serialization::diagnostic>
decode_custom(const e::serialization::context&, e::serialization::reader&, const void*) {
  return custom{42};
}
struct optional_description {
  static constexpr std::string_view type_name = "optional_description";
  static constexpr e::guid type_guid = e::guid::from_bytes({95, 4});
  std::optional<child> item;
  std::optional<float> number{3};
  std::optional<std::vector<float>> values;
  std::optional<e::guid> identifier;
  const std::optional<float>& get_number() const { return number; }
  e::error set_number(const std::optional<float>& v) {
    if (v && *v < 0)
      return e::error::invalid_argument;
    number = v;
    return e::error::success;
  }
};
struct tree {
  static constexpr std::string_view type_name = "container_tree";
  static constexpr e::guid type_guid = e::guid::from_bytes({95, 2});
  std::vector<child> items{child{}};
  std::map<std::string, std::vector<float>> groups{{"old", {1}}};
  std::unordered_map<std::string, std::string> labels;
  std::vector<float> maybe;
  std::vector<float> readonly{4};
  std::vector<tree> children;
  std::vector<custom> custom_items{custom{}};
  inline static int alive = 0;
  tree() { ++alive; }
  tree(const tree& other)
      : items(other.items), groups(other.groups), labels(other.labels), maybe(other.maybe),
        readonly(other.readonly), children(other.children), custom_items(other.custom_items) {
    ++alive;
  }
  tree(tree&& other) noexcept
      : items(std::move(other.items)), groups(std::move(other.groups)),
        labels(std::move(other.labels)), maybe(std::move(other.maybe)),
        readonly(std::move(other.readonly)), children(std::move(other.children)),
        custom_items(std::move(other.custom_items)) {
    ++alive;
  }
  tree& operator=(const tree&) = default;
  tree& operator=(tree&&) noexcept = default;
  ~tree() { --alive; }
};
e::error register_tree(const m::registry_ref& r) {
  return m::registration<tree>{r}
      .property<&tree::items>(m::property_id{1}, "items")
      .property<&tree::groups>(m::property_id{2}, "groups")
      .property<&tree::labels>(m::property_id{3}, "labels")
      .property<&tree::maybe>(m::property_id{4}, "maybe")
      .property<&tree::readonly>(m::property_id{5}, "readonly", m::property_access::read_only)
      .property<&tree::children>(m::property_id{6}, "children")
      .property<&tree::custom_items>(m::property_id{7}, "custom_items")
      .commit();
}
std::span<const std::byte> bytes(std::string_view s) {
  return std::as_bytes(std::span{s.data(), s.size()});
}
template <class T>
concept writable_sequence = requires(T v) { v.resize(1); };
static_assert(!writable_sequence<m::sequence_view> && writable_sequence<m::mutable_sequence_view>);
int main() try {
  auto r = m::create_registry();
  CHECK(r);
  CHECK(register_tree(*r).ok());
  CHECK(m::freeze(*r) == e::error::not_found);
  CHECK(m::registration<child>{*r}
            .property<&child::speed>(m::property_id{1}, "speed")
            .extend(gneiss::engine::serialization::validation<&child::validate>)
            .commit()
            .ok());
  CHECK((m::registration<custom>{*r}
             .extend(gneiss::engine::serialization::codec<&encode_custom, &decode_custom>)
             .commit()
             .ok()));
  CHECK((m::registration<optional_description>{*r}
             .property<&optional_description::item>(m::property_id{1}, "item")
             .property<&optional_description::get_number, &optional_description::set_number>(
                 m::property_id{2}, "number")
             .property<&optional_description::values>(m::property_id{3}, "values")
             .property<&optional_description::identifier>(m::property_id{4}, "identifier")
             .commit()
             .ok()));
  CHECK(m::freeze(*r).ok());
  optional_description optional;
  auto optional_root = m::view(*r, optional);
  CHECK(optional_root);
  CHECK(optional_root->member("number")->as_optional().error() == e::error::unsupported);
  CHECK(*optional_root->as_const().member("number")->as_optional()->has_value());
  auto rejected_number = gneiss::engine::serialization::json::deserialize<optional_description>(
      gneiss::engine::serialization::context{*r}, bytes(R"({"number":-1})"));
  CHECK(!rejected_number && rejected_number.error().path == "/number");
  auto slot = optional_root->member("item")->as_optional();
  CHECK(slot && !*slot->has_value());
  CHECK(slot->get().error() == e::error::not_found);
  CHECK(optional_root->member("item")->read_value().error() == e::error::unsupported);
  CHECK(slot->ensure() && optional.item.has_value());
  CHECK(slot->get()->member("speed")->assign(e::value{4.0f}).ok());
  auto optional_json = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*r}, optional);
  CHECK(optional_json);
  auto optional_copy = gneiss::engine::serialization::json::deserialize<optional_description>(
      gneiss::engine::serialization::context{*r}, *optional_json);
  CHECK(optional_copy && optional_copy->item->speed == 4);
  optional_copy->item->speed = 9;
  CHECK(optional.item->speed == 4);
  CHECK(slot->reset().ok() && !optional.item);
  auto optional_default = gneiss::engine::serialization::json::deserialize<optional_description>(
      gneiss::engine::serialization::context{*r}, bytes("{}"));
  CHECK(optional_default && optional_default->number == 3 && !optional_default->item);
  auto optional_null = gneiss::engine::serialization::json::deserialize<optional_description>(
      gneiss::engine::serialization::context{*r}, bytes(R"({"number":null})"));
  CHECK(optional_null && !optional_null->number);
  auto optional_bad = gneiss::engine::serialization::json::deserialize<optional_description>(
      gneiss::engine::serialization::context{*r}, bytes(R"({"item":{"speed":-1}})"));
  CHECK(!optional_bad && optional_bad.error().path == "/item");
  optional.values.emplace(std::initializer_list<float>{1, 2});
  optional_json = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*r}, optional);
  CHECK(optional_json);
  optional_copy = gneiss::engine::serialization::json::deserialize<optional_description>(
      gneiss::engine::serialization::context{*r}, *optional_json);
  CHECK(optional_copy && optional_copy->values == optional.values);
  optional.identifier.emplace();
  CHECK(!gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*r}, optional));
  tree source;
  auto root = m::view(*r, source);
  CHECK(root);
  auto info = m::find_property(*r, tree::type_guid, m::property_id{1});
  CHECK(info && info->type().kind() == m::type_kind::sequence && !info->type().guid());
  auto items = root->member("items");
  CHECK(items);
  CHECK(items->read_value().error() == e::error::unsupported);
  CHECK(items->as_associative().error() == e::error::unsupported);
  auto seq = items->as_sequence();
  CHECK(seq && *seq->size() == 1);
  CHECK(seq->at(2).error() == e::error::not_found);
  CHECK(seq->resize(2).ok());
  auto object = seq->at(1);
  CHECK(object);
  CHECK(object->member("speed")->assign(e::value{5.0F}).ok());
  CHECK(source.items[1].speed == 5);
  CHECK(root->member("readonly").error() == e::error::unsupported);
  auto readonly = root->as_const().member("readonly")->as_sequence();
  CHECK(readonly);
  CHECK(std::get<float>(readonly->at(0)->read_value()->payload) == 4);
  auto groups = root->member("groups")->as_associative();
  CHECK(groups);
  auto inserted = groups->insert("a/b~");
  CHECK(inserted);
  auto numbers = inserted->as_sequence();
  CHECK(numbers && numbers->resize(2).ok());
  CHECK(numbers->at(1)->assign(e::value{8.0F}).ok());
  CHECK(numbers->at(0)->assign(e::value{true}) == e::error::invalid_argument);
  CHECK(numbers->at(0)->assign(e::value{std::numeric_limits<float>::infinity()}) ==
        e::error::invalid_argument);
  CHECK(groups->find("missing").error() == e::error::not_found);
  CHECK(groups->erase("old").ok() && groups->erase("old") == e::error::not_found);
  CHECK(groups->insert(std::string("bad\0key", 7)).error() == e::error::invalid_argument);
  CHECK(groups->insert("a/b~")->as_sequence()->at(1)->read_value()->kind() ==
        e::value_kind::float32);
  auto maybe = root->member("maybe")->as_sequence();
  CHECK(maybe && maybe->resize(1).ok() && source.maybe.size() == 1);
  source.labels = {{"z", "last"}, {"a", "first"}};
  auto encoded = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*r}, source);
  CHECK(encoded);
  const std::string json(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  CHECK(json.find("\"labels\":{\"a\":\"first\",\"z\":\"last\"}") != std::string::npos);
  CHECK(json.find("readonly") == std::string::npos);
  auto restored = gneiss::engine::serialization::json::deserialize<tree>(
      gneiss::engine::serialization::context{*r}, *encoded);
  CHECK(restored);
  CHECK(restored->items.size() == 2 && restored->items[0].speed == 2 &&
        restored->items[1].speed == 5);
  CHECK(restored->custom_items.size() == 1 && restored->custom_items[0].number == 42);
  CHECK(restored->groups == source.groups && restored->labels == source.labels &&
        restored->maybe == source.maybe);
  auto empty = gneiss::engine::serialization::json::deserialize<tree>(
      gneiss::engine::serialization::context{*r}, bytes(R"({"items":[],"groups":{},"maybe":[]})"));
  CHECK(empty && empty->items.empty() && empty->groups.empty() && empty->maybe.empty());
  auto defaults = gneiss::engine::serialization::json::deserialize<tree>(
      gneiss::engine::serialization::context{*r}, bytes("{}"));
  CHECK(defaults && defaults->items.size() == 1 && defaults->groups.contains("old"));
  const auto alive_before_failure = tree::alive;
  auto bad = gneiss::engine::serialization::json::deserialize<tree>(
      gneiss::engine::serialization::context{*r}, bytes(R"({"groups":{"a/b~":[true]}})"));
  CHECK(!bad && bad.error().path == "/groups/a~1b~0/0");
  bad = gneiss::engine::serialization::json::deserialize<tree>(
      gneiss::engine::serialization::context{*r}, bytes(R"({"items":[{"speed":-1}]})"));
  CHECK(!bad && bad.error().path == "/items/0");
  bad = gneiss::engine::serialization::json::deserialize<tree>(
      gneiss::engine::serialization::context{*r}, bytes(R"({"labels":{"x":"a","x":"b"}})"));
  CHECK(!bad && bad.error().path == "/labels/x");
  bad = gneiss::engine::serialization::json::deserialize<tree>(
      gneiss::engine::serialization::context{*r}, bytes(R"({"items":{}})"));
  CHECK(!bad && bad.error().path == "/items");
  CHECK(tree::alive == alive_before_failure);
  source.items[0].speed = -1;
  CHECK(!gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*r}, source));
  source.items[0].speed = 2;
  std::string deep = "{}";
  for (int i = 0; i < 35; ++i)
    deep = "{\"children\":[" + deep + "]}";
  CHECK(!gneiss::engine::serialization::json::deserialize<tree>(
      gneiss::engine::serialization::context{*r}, bytes(deep)));
  CHECK(tree::alive == alive_before_failure);
  // 读任务期间没有修改目标；视图本身不提供线程同步。
  auto future = std::async(std::launch::async, [view = root->as_const()] {
    return view.member("items")->as_sequence()->size();
  });
  auto count = future.get();
  CHECK(count && *count == source.items.size());
  // 根视图销毁后子视图仍持有注册表；目标对象 source 仍存活。
  *root = {};
  r->reset();
  CHECK(*seq->size() == 2);
  return 0;
} catch (...) {
  return 99;
}
