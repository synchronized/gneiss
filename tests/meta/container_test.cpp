// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
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
e::result<std::vector<std::byte>, m::serialization_diagnostic> encode_custom(const custom&) {
  const std::string text = "{\"custom\":true}";
  const auto data = std::as_bytes(std::span{text.data(), text.size()});
  return std::vector<std::byte>(data.begin(), data.end());
}
e::result<custom, m::serialization_diagnostic> decode_custom(std::span<const std::byte>) {
  return custom{42};
}
struct tree {
  static constexpr std::string_view type_name = "container_tree";
  static constexpr e::guid type_guid = e::guid::from_bytes({95, 2});
  std::vector<std::optional<child>> items{child{}};
  std::map<std::string, std::vector<float>> groups{{"old", {1}}};
  std::unordered_map<std::string, std::string> labels;
  std::optional<std::vector<float>> maybe;
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
  return m::type_builder<tree>{r}
      .property<&tree::items>(m::property_id{1}, "items")
      .property<&tree::groups>(m::property_id{2}, "groups")
      .property<&tree::labels>(m::property_id{3}, "labels")
      .property<&tree::maybe>(m::property_id{4}, "maybe")
      .property<&tree::readonly>(m::property_id{5}, "readonly", m::property_flags::read_only)
      .property<&tree::children>(m::property_id{6}, "children")
      .property<&tree::custom_items>(m::property_id{7}, "custom_items")
      .serialization()
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
  CHECK(m::type_builder<child>{*r}
            .property<&child::speed>(m::property_id{1}, "speed")
            .serialization()
            .validate<&child::validate>()
            .commit()
            .ok());
  CHECK(
      (m::type_builder<custom>{*r}.serialization<&encode_custom, &decode_custom>().commit().ok()));
  CHECK(m::freeze(*r).ok());
  tree source;
  auto root = m::mutable_instance::from(*r, source);
  CHECK(root);
  auto info = m::describe_property(*r, tree::type_guid, m::property_id{1});
  CHECK(info && info->category == m::type_kind::sequence && !info->type_guid);
  auto items = root->property("items");
  CHECK(items);
  CHECK(items->read_value().error() == e::error::unsupported);
  CHECK(items->as_associative().error() == e::error::unsupported);
  auto seq = items->as_sequence();
  CHECK(seq && *seq->size() == 1);
  CHECK(seq->at(2).error() == e::error::not_found);
  CHECK(seq->resize(2).ok());
  auto optional_item = seq->at(1)->as_optional();
  CHECK(optional_item);
  CHECK(!*optional_item->has_value() && optional_item->get().error() == e::error::not_found);
  auto object = optional_item->ensure();
  CHECK(object);
  CHECK(object->property("speed")->write_value(e::value{5.0F}).ok());
  CHECK(source.items[1]->speed == 5);
  CHECK(optional_item->reset().ok()); // 从此不再访问 object 元素视图。
  CHECK(!source.items[1]);
  CHECK(root->property("readonly").error() == e::error::unsupported);
  auto readonly = root->as_const().property("readonly")->as_sequence();
  CHECK(readonly);
  CHECK(std::get<float>(readonly->at(0)->read_value()->payload) == 4);
  auto groups = root->property("groups")->as_associative();
  CHECK(groups);
  auto inserted = groups->insert("a/b~");
  CHECK(inserted);
  auto numbers = inserted->as_sequence();
  CHECK(numbers && numbers->resize(2).ok());
  CHECK(numbers->at(1)->write_value(e::value{8.0F}).ok());
  CHECK(numbers->at(0)->write_value(e::value{true}) == e::error::invalid_argument);
  CHECK(numbers->at(0)->write_value(e::value{std::numeric_limits<float>::infinity()}) ==
        e::error::invalid_argument);
  CHECK(groups->find("missing").error() == e::error::not_found);
  CHECK(groups->erase("old").ok() && groups->erase("old") == e::error::not_found);
  CHECK(groups->insert(std::string("bad\0key", 7)).error() == e::error::invalid_argument);
  CHECK(groups->insert("a/b~")->as_sequence()->at(1)->read_value()->kind() ==
        e::value_kind::float32);
  auto maybe = root->property("maybe")->as_optional();
  CHECK(maybe);
  CHECK(maybe->ensure()->as_sequence()->resize(1).ok());
  CHECK(source.maybe && source.maybe->size() == 1);
  source.labels = {{"z", "last"}, {"a", "first"}};
  auto encoded = m::serialize(*r, source);
  CHECK(encoded);
  const std::string json(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  CHECK(json.find("\"labels\":{\"a\":\"first\",\"z\":\"last\"}") != std::string::npos);
  CHECK(json.find("readonly") == std::string::npos);
  auto restored = m::deserialize<tree>(*r, *encoded);
  CHECK(restored);
  CHECK(restored->items.size() == 2 && restored->items[0]->speed == 2 && !restored->items[1]);
  CHECK(restored->custom_items.size() == 1 && restored->custom_items[0].number == 42);
  CHECK(restored->groups == source.groups && restored->labels == source.labels &&
        restored->maybe == source.maybe);
  auto empty = m::deserialize<tree>(*r, bytes(R"({"items":[],"groups":{},"maybe":null})"));
  CHECK(empty && empty->items.empty() && empty->groups.empty() && !empty->maybe);
  auto defaults = m::deserialize<tree>(*r, bytes("{}"));
  CHECK(defaults && defaults->items.size() == 1 && defaults->groups.contains("old"));
  const auto alive_before_failure = tree::alive;
  auto bad = m::deserialize<tree>(*r, bytes(R"({"groups":{"a/b~":[true]}})"));
  CHECK(!bad && bad.error().path == "/groups/a~1b~0/0");
  bad = m::deserialize<tree>(*r, bytes(R"({"items":[{"speed":-1}]})"));
  CHECK(!bad && bad.error().path == "/items/0");
  bad = m::deserialize<tree>(*r, bytes(R"({"labels":{"x":"a","x":"b"}})"));
  CHECK(!bad && bad.error().path == "/labels/x");
  bad = m::deserialize<tree>(*r, bytes(R"({"items":{}})"));
  CHECK(!bad && bad.error().path == "/items");
  CHECK(tree::alive == alive_before_failure);
  source.items[0]->speed = -1;
  CHECK(!m::serialize(*r, source));
  source.items[0]->speed = 2;
  std::string deep = "{}";
  for (int i = 0; i < 35; ++i)
    deep = "{\"children\":[" + deep + "]}";
  CHECK(!m::deserialize<tree>(*r, bytes(deep)));
  CHECK(tree::alive == alive_before_failure);
  // 读任务期间没有修改目标；视图本身不提供线程同步。
  auto future = std::async(std::launch::async, [view = root->as_const()] {
    return view.property("items")->as_sequence()->size();
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
