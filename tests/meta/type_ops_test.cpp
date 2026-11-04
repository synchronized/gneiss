// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
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

int main() {
  auto registry = m::create_registry();
  CHECK(registry);
  CHECK(m::type_builder<document>{*registry}
            .property<&document::sequence>(m::property_id{1}, "sequence")
            .property<&document::mapping>(m::property_id{2}, "mapping")
            .serialization()
            .commit()
            .ok());
  CHECK(m::type_builder<non_default>{*registry}
            .property<&non_default::number>(m::property_id{1}, "number")
            .commit()
            .ok());
  CHECK(m::freeze(*registry).ok());
  document source;
  auto view = m::mutable_instance::from(*registry, source);
  CHECK(view);
  auto sequence = view->property("sequence")->as_sequence();
  CHECK(sequence && sequence->resize(3).ok());
  CHECK(sequence->at(2)->write_value(e::value{9.0F}).ok());
  CHECK(sequence->at(3).error() == e::error::not_found);
  source.sequence.fail_resize = true;
  CHECK(sequence->resize(4) == e::error::out_of_memory);
  CHECK(source.sequence.values.size() == 3);
  source.sequence.fail_resize = false;
  auto mapping = view->property("mapping")->as_associative();
  CHECK(mapping && *mapping->keys() == std::vector<std::string>({"a", "z"}));
  CHECK(mapping->find("missing").error() == e::error::not_found);
  auto encoded = m::serialize(*registry, source);
  CHECK(encoded);
  auto decoded = m::deserialize<document>(*registry, *encoded);
  CHECK(decoded && decoded->sequence.values == source.sequence.values &&
        decoded->mapping.values == source.mapping.values);
  CHECK(mapping->insert("added")->write_value(e::value{5.0F}).ok());
  CHECK(mapping->erase("added").ok());
  CHECK(mapping->clear().ok() && source.mapping.values.empty());

  readonly_numbers readonly;
  auto readonly_view = m::mutable_instance::from(*registry, readonly);
  CHECK(readonly_view);
  auto readable = readonly_view->as_const().as_sequence();
  CHECK(readable && std::get<float>(readable->at(0)->read_value()->payload) == 3);
  CHECK(readonly_view->as_sequence()->at(0).error() == e::error::unsupported);
  CHECK(readonly_view->as_sequence()->resize(3) == e::error::unsupported);
  dictionary<false> readonly_mapping;
  auto map_view = m::mutable_instance::from(*registry, readonly_mapping);
  CHECK(map_view && map_view->as_const().as_associative()->find("a"));
  CHECK(map_view->as_associative()->find("a").error() == e::error::unsupported);
  CHECK(map_view->as_associative()->insert("b").error() == e::error::unsupported);
  CHECK(map_view->as_associative()->erase("a") == e::error::unsupported);
  CHECK(map_view->as_associative()->clear() == e::error::unsupported);

  std::vector<non_default> values;
  values.emplace_back(7.0F);
  auto values_view = m::mutable_instance::from(*registry, values);
  CHECK(values_view && values_view->as_sequence()->at(0)->property("number"));
  CHECK(values_view->as_sequence()->resize(2) == e::error::unsupported);
  std::map<std::string, non_default> non_default_map;
  non_default_map.try_emplace("x", 7.0F);
  auto non_default_map_view = m::mutable_instance::from(*registry, non_default_map);
  CHECK(non_default_map_view && non_default_map_view->as_associative()->find("x"));
  CHECK(non_default_map_view->as_associative()->insert("y").error() == e::error::unsupported);
  std::array<non_default, 1> fixed{non_default{8.0F}};
  auto fixed_view = m::mutable_instance::from(*registry, fixed);
  CHECK(fixed_view && fixed_view->as_sequence()->at(0));
  CHECK(fixed_view->as_sequence()->resize(2) == e::error::invalid_argument);
  return 0;
}
