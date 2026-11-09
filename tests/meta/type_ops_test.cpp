// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/policy.hpp"
#include <cstdio>

namespace e = gneiss::engine;
namespace m = e::meta;
#define CHECK(expression)                                                                          \
  do {                                                                                             \
    if (!(expression)) {                                                                           \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                                \
      return 1;                                                                                    \
    }                                                                                              \
  } while (false)

struct numbers {
  std::vector<float> values{1, 2};
  bool fail_resize = false;
};
struct readonly_numbers {
  std::array<float, 2> values{3, 4};
};
template <bool Mutable> struct dictionary {
  std::map<std::string, float> values{{"z", 2.0F}, {"a", 1.0F}};
};

namespace gneiss::engine::meta {
template <> struct sequence_traits<numbers> {
  static constexpr bool enabled = true;
  using element = float;
  static std::size_t size(const numbers& v) { return v.values.size(); }
  static const float* at(const numbers& v, std::size_t i) { return &v.values[i]; }
  static float* at_mut(numbers& v, std::size_t i) { return &v.values[i]; }
  static void resize(numbers& v, std::size_t n) {
    if (v.fail_resize)
      throw std::bad_alloc{};
    v.values.resize(n);
  }
};
template <> struct sequence_traits<readonly_numbers> {
  static constexpr bool enabled = true;
  using element = float;
  static std::size_t size(const readonly_numbers& v) { return v.values.size(); }
  static const float* at(const readonly_numbers& v, std::size_t i) { return &v.values[i]; }
};
template <bool Mutable> struct associative_traits<dictionary<Mutable>> {
  static constexpr bool enabled = true;
  using element = float;
  static std::size_t size(const dictionary<Mutable>& v) { return v.values.size(); }
  static std::vector<std::string> keys(const dictionary<Mutable>&) { return {"z", "a"}; }
  static const float* find(const dictionary<Mutable>& v, std::string_view k) {
    auto it = v.values.find(std::string(k));
    return it == v.values.end() ? nullptr : &it->second;
  }
  static float* find_mut(dictionary<Mutable>& v, std::string_view k)
    requires Mutable
  {
    auto it = v.values.find(std::string(k));
    return it == v.values.end() ? nullptr : &it->second;
  }
  static float* insert(dictionary<Mutable>& v, std::string_view k)
    requires Mutable
  {
    return &v.values.try_emplace(std::string(k)).first->second;
  }
  static bool erase(dictionary<Mutable>& v, std::string_view k)
    requires Mutable
  {
    return v.values.erase(std::string(k)) != 0;
  }
  static void clear(dictionary<Mutable>& v)
    requires Mutable
  {
    v.values.clear();
  }
};
}

struct document {
  static constexpr std::string_view type_name = "ops_document";
  static constexpr e::guid type_guid = e::guid::from_bytes({96, 21});
  numbers sequence;
  dictionary<true> mapping;
};
struct non_default {
  static constexpr std::string_view type_name = "ops_non_default";
  static constexpr e::guid type_guid = e::guid::from_bytes({96, 22});
  explicit non_default(float v) : number(v) {}
  float number;
};

struct throwing_value {
  static constexpr std::string_view type_name = "ops_throwing_value";
  static constexpr e::guid type_guid = e::guid::from_bytes({96, 23});
  throwing_value() = default;
  throwing_value(throwing_value&&) { throw 1; }
  throwing_value& operator=(throwing_value&&) = default;
};
using throwing_choice = std::variant<int, throwing_value>;
using choice = std::variant<std::int32_t, std::string>;
using reordered_choice = std::variant<std::string, std::int32_t>;
namespace gneiss::engine::meta {
template <> struct variant_case_traits<throwing_choice> {
  static constexpr std::array<std::string_view, 2> names{"number", "throwing"};
};
template <> struct variant_case_traits<choice> {
  static constexpr std::array<std::string_view, 2> names{"number", "text"};
};
template <> struct variant_case_traits<reordered_choice> {
  static constexpr std::array<std::string_view, 2> names{"text", "number"};
};
}

struct variant_document {
  static constexpr std::string_view type_name = "ops_variant_document";
  static constexpr e::guid type_guid = e::guid::from_bytes({96, 24});
  std::vector<choice> entries;
  std::optional<choice> selected;
};

int main() {
  auto registry = m::create_registry();
  CHECK(registry);
  CHECK(m::registration<document>{*registry}
            .property<&document::sequence>(m::property_id{1}, "sequence")
            .property<&document::mapping>(m::property_id{2}, "mapping")
            .commit()
            .ok());
  CHECK(m::registration<non_default>{*registry}
            .property<&non_default::number>(m::property_id{1}, "number")
            .commit()
            .ok());
  CHECK(m::registration<variant_document>{*registry}
            .property<&variant_document::entries>(m::property_id{1}, "entries")
            .property<&variant_document::selected>(m::property_id{2}, "selected")
            .commit()
            .ok());
  CHECK(m::registration<throwing_value>{*registry}.commit().ok());
  CHECK(m::freeze(*registry).ok());
  document source;
  auto view = m::view(*registry, source);
  CHECK(view);
  auto sequence = view->member("sequence")->as_sequence();
  CHECK(sequence && sequence->resize(3).ok());
  CHECK(sequence->at(2)->assign(e::value{9.0F}).ok());
  CHECK(sequence->at(3).error() == e::error::not_found);
  source.sequence.fail_resize = true;
  CHECK(sequence->resize(4) == e::error::out_of_memory);
  CHECK(source.sequence.values.size() == 3);
  source.sequence.fail_resize = false;
  auto mapping = view->member("mapping")->as_associative();
  CHECK(mapping && *mapping->keys() == std::vector<std::string>({"a", "z"}));
  CHECK(mapping->find("missing").error() == e::error::not_found);
  auto encoded = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*registry}, source);
  CHECK(encoded);
  auto decoded = gneiss::engine::serialization::json::deserialize<document>(
      gneiss::engine::serialization::context{*registry}, *encoded);
  CHECK(decoded && decoded->sequence.values == source.sequence.values &&
        decoded->mapping.values == source.mapping.values);
  CHECK(mapping->insert("added")->assign(e::value{5.0F}).ok());
  CHECK(mapping->erase("added").ok());
  CHECK(mapping->clear().ok() && source.mapping.values.empty());

  readonly_numbers readonly;
  auto readonly_view = m::view(*registry, readonly);
  CHECK(readonly_view);
  auto readable = readonly_view->as_const().as_sequence();
  CHECK(readable && std::get<float>(readable->at(0)->read_value()->payload) == 3);
  CHECK(readonly_view->as_sequence()->at(0).error() == e::error::unsupported);
  CHECK(readonly_view->as_sequence()->resize(3) == e::error::unsupported);
  dictionary<false> readonly_mapping;
  auto map_view = m::view(*registry, readonly_mapping);
  CHECK(map_view && map_view->as_const().as_associative()->find("a"));
  CHECK(map_view->as_associative()->find("a").error() == e::error::unsupported);
  CHECK(map_view->as_associative()->insert("b").error() == e::error::unsupported);
  CHECK(map_view->as_associative()->erase("a") == e::error::unsupported);
  CHECK(map_view->as_associative()->clear() == e::error::unsupported);

  std::vector<non_default> values;
  values.emplace_back(7.0F);
  auto values_view = m::view(*registry, values);
  CHECK(values_view && values_view->as_sequence()->at(0)->member("number"));
  CHECK(values_view->as_sequence()->resize(2) == e::error::unsupported);
  std::map<std::string, non_default> non_default_map;
  non_default_map.try_emplace("x", 7.0F);
  auto non_default_map_view = m::view(*registry, non_default_map);
  CHECK(non_default_map_view && non_default_map_view->as_associative()->find("x"));
  CHECK(non_default_map_view->as_associative()->insert("y").error() == e::error::unsupported);
  std::array<non_default, 1> fixed{non_default{8.0F}};
  auto fixed_view = m::view(*registry, fixed);
  CHECK(fixed_view && fixed_view->as_sequence()->at(0));
  CHECK(fixed_view->as_sequence()->resize(2) == e::error::invalid_argument);
  choice selected{std::int32_t{42}};
  auto selected_instance = m::view(*registry, selected);
  CHECK(selected_instance);
  auto variant = selected_instance->as_variant();
  CHECK(variant && variant->alternative_count() == 2 && *variant->index() == 0);
  CHECK(variant->alternative(0) && variant->alternative(2).error() == e::error::invalid_argument);
  auto choice_bytes = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*registry}, selected);
  CHECK(choice_bytes);
  auto reordered = gneiss::engine::serialization::json::deserialize<reordered_choice>(
      gneiss::engine::serialization::context{*registry}, *choice_bytes);
  CHECK(reordered && reordered->index() == 1 && std::get<1>(*reordered) == 42);
  CHECK(variant->select(1).ok());
  CHECK(variant->value()->assign(e::value{std::string{"hello"}}).ok());
  CHECK(std::get<1>(selected) == "hello");
  CHECK(variant->select(2) == e::error::invalid_argument && *variant->index() == 1);
  auto text_bytes = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*registry}, selected);
  CHECK(text_bytes);
  auto text_copy = gneiss::engine::serialization::json::deserialize<choice>(
      gneiss::engine::serialization::context{*registry}, *text_bytes);
  CHECK(text_copy && *text_copy == selected);
  for (std::string_view invalid : {R"({"$kind":"missing","$content":1})", R"({"$kind":"number"})",
                                   R"({"$kind":"number","$content":"wrong"})",
                                   R"({"$kind":"number","$content":1,"extra":2})"}) {
    auto decoded_choice = gneiss::engine::serialization::json::deserialize<choice>(
        gneiss::engine::serialization::context{*registry},
        std::as_bytes(std::span(invalid.data(), invalid.size())));
    CHECK(!decoded_choice && decoded_choice.error().error == e::error::invalid_argument);
  }
  std::variant<int, double> unlabelled{3};
  auto unlabelled_view = m::view(*registry, unlabelled);
  CHECK(unlabelled_view && unlabelled_view->as_variant()->select(1).ok());
  auto unlabelled_bytes = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*registry}, unlabelled);
  CHECK(!unlabelled_bytes && unlabelled_bytes.error().error == e::error::unsupported);
  std::variant<int, non_default> cannot_construct{4};
  auto non_default_variant = m::view(*registry, cannot_construct);
  CHECK(non_default_variant);
  CHECK(non_default_variant->as_variant()->select(1) == e::error::unsupported);
  CHECK(std::get<0>(cannot_construct) == 4);
  // 原生类型能力与注册独立；未注册对象可以借用，但不能伪装成空对象持久化。
  auto empty_registry = m::create_registry();
  CHECK(empty_registry && m::freeze(*empty_registry).ok());
  auto lazy = m::view(*empty_registry, cannot_construct);
  CHECK(lazy && lazy->as_variant()->value()->read_value()->kind() == e::value_kind::int64);
  cannot_construct.emplace<1>(1.0F);
  auto unknown_object = lazy->as_variant()->value();
  CHECK(unknown_object && unknown_object->kind() == m::type_kind::object);
  CHECK(unknown_object->as_const().properties().error() == e::error::not_found);
  CHECK(m::type_of<decltype(cannot_construct)>(*empty_registry));
  auto rejected_object = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*empty_registry}, std::get<1>(cannot_construct));
  CHECK(!rejected_object);
  std::vector<non_default> unknown_elements;
  unknown_elements.emplace_back(1.0F);
  auto unknown_sequence = m::type_of<decltype(unknown_elements)>(*empty_registry);
  CHECK(unknown_sequence && unknown_sequence.element_type());
  auto unknown_view = m::view(*empty_registry, unknown_elements);
  CHECK(unknown_view && unknown_view->as_sequence()->at(0));
  throwing_choice throwing_source{std::in_place_index<1>};
  throwing_choice valueless{1};
  try {
    valueless = std::move(throwing_source);
    CHECK(false);
  } catch (int) {
  }
  auto valueless_instance = m::view(*registry, valueless);
  CHECK(valueless_instance);
  auto valueless_view = valueless_instance->as_variant();
  CHECK(valueless_view && valueless_view->valueless());
  CHECK(valueless_view->index().error() == e::error::invalid_state);
  CHECK(valueless_view->value().error() == e::error::invalid_state);
  auto failed_bytes = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*registry}, valueless);
  CHECK(!failed_bytes && failed_bytes.error().error == e::error::invalid_state);
  CHECK(valueless_view->select(0).ok() && !valueless_view->valueless());
  variant_document nested{{choice{std::int32_t{7}}, choice{std::string{"text"}}},
                          choice{std::string{"selected"}}};
  auto nested_bytes = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*registry}, nested);
  CHECK(nested_bytes);
  auto nested_copy = gneiss::engine::serialization::json::deserialize<variant_document>(
      gneiss::engine::serialization::context{*registry}, *nested_bytes);
  CHECK(nested_copy && nested_copy->entries == nested.entries &&
        nested_copy->selected == nested.selected);
  return 0;
}
