// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/throws.hpp"
#include <cstdio>
#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <stdexcept>

namespace e = gneiss;
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
  bool fail_length = false;
};
struct readonly_numbers {
  std::array<float, 2> values{3, 4};
};
template <bool Mutable> struct dictionary {
  std::map<std::string, float> values{{"z", 2.0F}, {"a", 1.0F}};
};

struct mapped_value {
  static constexpr std::string_view type_name = "mapped_value";
  static constexpr e::guid type_guid = e::guid::literal("bcf96129-a1c2-4623-93e8-7bf1a2a32e1f");
  std::int64_t number = 7;
};

struct mapped_owner {
  static constexpr std::string_view type_name = "mapped_owner";
  static constexpr e::guid type_guid = e::guid::literal("e87d2ec8-fad3-4c0a-984f-9b7a2737f378");
  mapped_value stored;
  const mapped_value& get() const { return stored; }
  e::error set(const mapped_value& candidate) {
    if (candidate.number < 0)
      return e::error::invalid_argument;
    stored = candidate;
    return e::error::success;
  }
};

namespace gneiss::meta {
template <> struct value_traits<mapped_value> {
  static constexpr bool enabled = true;
  static value read(const mapped_value& input) { return value{std::int64_t{input.number}}; }
  static error write(mapped_value& target, const value& input) {
    const auto* decoded = std::get_if<std::int64_t>(&input.payload);
    if (!decoded)
      return error::invalid_argument;
    target.number = *decoded;
    return error::success;
  }
};

template <> struct sequence_traits<numbers> {
  static constexpr bool enabled = true;
  using element = float;
  static std::size_t size(const numbers& v) { return v.values.size(); }
  static const float* at(const numbers& v, std::size_t i) { return &v.values[i]; }
  static float* at_mut(numbers& v, std::size_t i) { return &v.values[i]; }
  static void resize(numbers& v, std::size_t n) {
    if (v.fail_resize)
      throw std::bad_alloc{};
    if (v.fail_length)
      throw std::length_error{"element construction"};
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

struct represented_value {
  explicit represented_value(std::int64_t input) : number(input) {}
  std::int64_t number;
};
namespace gneiss::meta {
template <> struct representation_traits<represented_value> {
  static constexpr bool enabled = true;
  using representation_type = std::int64_t;
  static result<representation_type> encode(const represented_value& source) {
    return source.number;
  }
  static result<represented_value> decode(const representation_type& source) {
    return represented_value{source};
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
namespace gneiss::meta {
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
  CHECK(m::registration::type<document>(*registry)
            .property<&document::sequence>(m::property_id{1}, "sequence")
            .property<&document::mapping>(m::property_id{2}, "mapping")
            .commit()
            .ok());
  CHECK(m::registration::type<non_default>(*registry)
            .property<&non_default::number>(m::property_id{1}, "number")
            .commit()
            .ok());
  CHECK(m::registration::type<variant_document>(*registry)
            .property<&variant_document::entries>(m::property_id{1}, "entries")
            .property<&variant_document::selected>(m::property_id{2}, "selected")
            .commit()
            .ok());
  CHECK(m::registration::type<throwing_value>(*registry).commit().ok());
  CHECK(m::registration::type<mapped_value>(*registry)
            .property<&mapped_value::number>(m::property_id{1}, "number")
            .commit()
            .ok());
  CHECK((m::registration::type<mapped_owner>(*registry)
             .property<&mapped_owner::get, &mapped_owner::set>(m::property_id{1}, "item")
             .commit()
             .ok()));
  CHECK(m::freeze(*registry).ok());
  mapped_owner owner;
  auto owner_view = m::view(*registry, owner);
  CHECK(owner_view);
  auto item = owner_view->member("item");
  CHECK(item && item->requires_replacement());
  CHECK(item->assign(e::value{std::int64_t{-1}}) == e::error::invalid_argument);
  CHECK(owner.stored.number == 7);
  CHECK(item->assign(e::value{std::int64_t{12}}).ok());
  CHECK(owner.stored.number == 12);

  // 整体转换不改变结构类别，也不改变默认 JSON 的对象格式。
  mapped_value mapped;
  auto mapped_view = m::view(*registry, mapped);
  CHECK(mapped_view && mapped_view->kind() == m::type_kind::object);
  CHECK(mapped_view->read_value() && *mapped_view->read_value() == e::value{std::int64_t{7}});
  CHECK(mapped_view->assign(e::value{std::int64_t{9}}).ok());
  CHECK(mapped.number == 9);
  CHECK(mapped_view->assign(e::value{std::string{"wrong"}}) == e::error::invalid_argument);
  CHECK(mapped.number == 9);
  auto mapped_bytes =
      e::serialization::json::encode_bytes(e::serialization::write_context{*registry}, mapped);
  CHECK(mapped_bytes);
  auto mapped_copy = e::serialization::json::decode<mapped_value>(
      e::serialization::read_context{*registry}, *mapped_bytes);
  CHECK(mapped_copy && mapped_copy->number == 9);
  const std::string mapped_json(reinterpret_cast<const char*>(mapped_bytes->data()),
                                mapped_bytes->size());
  CHECK(mapped_json.find("number") != std::string::npos);
  // 静态表示独立于 value 转换与注册；空冻结表也可处理原生类型。
  auto native_registry = m::create_registry();
  CHECK(native_registry && m::freeze(*native_registry).ok());
  e::serialization::write_context adapted_write{*native_registry};
  e::serialization::read_context adapted_read{*native_registry};
  auto adapted_bytes = e::serialization::json::encode_bytes(adapted_write, represented_value{9});
  CHECK(adapted_bytes);
  CHECK(std::string(reinterpret_cast<const char*>(adapted_bytes->data()), adapted_bytes->size()) ==
        "9");
  auto adapted_copy =
      e::serialization::json::decode<represented_value>(adapted_read, *adapted_bytes);
  CHECK(adapted_copy && adapted_copy->number == 9);

  document source;
  auto view = m::view(*registry, source);
  CHECK(view);
  auto sequence = view->member("sequence")->as_sequence();
  CHECK(sequence && sequence->resize(3).ok());
  CHECK(sequence->at(2)->assign(e::value{9.0F}).ok());
  CHECK(sequence->at(3).error() == e::error::not_found);
  source.sequence.fail_resize = true;
  CHECK(gneiss::test_support::throws<std::bad_alloc>([&] { (void)sequence->resize(4); }));
  CHECK(source.sequence.values.size() == 3);
  source.sequence.fail_resize = false;
  source.sequence.fail_length = true;
  CHECK(gneiss::test_support::throws<std::length_error>([&] { (void)sequence->resize(4); }));
  CHECK(sequence->size() == 3);
  source.sequence.fail_length = false;
  std::vector<float> bounded{1.0F};
  auto bounded_view = m::view(*registry, bounded);
  CHECK(bounded_view);
  CHECK(bounded_view->as_sequence()->resize(bounded.max_size() + 1) == e::error::invalid_argument);
  CHECK(bounded.size() == 1);
  auto mapping = view->member("mapping")->as_associative();
  CHECK(mapping && *mapping->keys() == std::vector<std::string>({"a", "z"}));
  CHECK(mapping->find("missing").error() == e::error::not_found);
  auto encoded = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{*registry}, source);
  CHECK(encoded);
  auto decoded = gneiss::serialization::json::decode<document>(
      gneiss::serialization::read_context{*registry}, *encoded);
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
  auto choice_bytes = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{*registry}, selected);
  CHECK(choice_bytes);
  auto reordered = gneiss::serialization::json::decode<reordered_choice>(
      gneiss::serialization::read_context{*registry}, *choice_bytes);
  CHECK(reordered && reordered->index() == 1 && std::get<1>(*reordered) == 42);
  CHECK(variant->select(1).ok());
  CHECK(variant->value()->assign(e::value{std::string{"hello"}}).ok());
  CHECK(std::get<1>(selected) == "hello");
  CHECK(variant->select(2) == e::error::invalid_argument && *variant->index() == 1);
  auto text_bytes = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{*registry}, selected);
  CHECK(text_bytes);
  auto text_copy = gneiss::serialization::json::decode<choice>(
      gneiss::serialization::read_context{*registry}, *text_bytes);
  CHECK(text_copy && *text_copy == selected);
  for (std::string_view invalid : {R"({"$kind":"missing","$content":1})", R"({"$kind":"number"})",
                                   R"({"$kind":"number","$content":"wrong"})",
                                   R"({"$kind":"number","$content":1,"extra":2})"}) {
    auto decoded_choice = gneiss::serialization::json::decode<choice>(
        gneiss::serialization::read_context{*registry},
        std::as_bytes(std::span(invalid.data(), invalid.size())));
    CHECK(!decoded_choice && decoded_choice.error().error == e::error::invalid_argument);
  }
  std::variant<int, double> unlabelled{3};
  auto unlabelled_view = m::view(*registry, unlabelled);
  CHECK(unlabelled_view && unlabelled_view->as_variant()->select(1).ok());
  auto unlabelled_bytes = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{*registry}, unlabelled);
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
  CHECK(unknown_object->as_const().properties().empty());
  CHECK(m::type_of<decltype(cannot_construct)>(*empty_registry));
  auto rejected_object = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{*empty_registry}, std::get<1>(cannot_construct));
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
  auto failed_bytes = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{*registry}, valueless);
  CHECK(!failed_bytes && failed_bytes.error().error == e::error::invalid_state);
  CHECK(valueless_view->select(0).ok() && !valueless_view->valueless());
  variant_document nested{{choice{std::int32_t{7}}, choice{std::string{"text"}}},
                          choice{std::string{"selected"}}};
  auto nested_bytes = gneiss::serialization::json::encode_bytes(
      gneiss::serialization::write_context{*registry}, nested);
  CHECK(nested_bytes);
  auto nested_copy = gneiss::serialization::json::decode<variant_document>(
      gneiss::serialization::read_context{*registry}, *nested_bytes);
  CHECK(nested_copy && nested_copy->entries == nested.entries &&
        nested_copy->selected == nested.selected);
  return 0;
}
