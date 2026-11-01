// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
namespace {
namespace e = gneiss::engine;
namespace m = e::meta;
struct child {
  static constexpr std::string_view type_name = "nested_child";
  static constexpr e::guid type_guid = e::guid::from_bytes({91, 1});
  float value = 2;
};
struct parent {
  static constexpr std::string_view type_name = "nested_parent";
  static constexpr e::guid type_guid = e::guid::from_bytes({91, 2});
  static inline int alive = 0;
  parent() { ++alive; }
  ~parent() { --alive; }
  child item{7};
};
struct invalid_parent {
  static constexpr std::string_view type_name = "invalid_parent";
  static constexpr e::guid type_guid = e::guid::from_bytes({91, 3});
  child item{-1};
};
m::serialization_diagnostic check(const child& value) {
  if (value.value <= 0)
    return {.path = "/value", .error = e::error::invalid_argument};
  return {};
}
e::error register_parent(const m::registry_ref& registry) {
  return m::type_builder<parent>{registry}
      .property<&parent::item>(m::property_id{1}, "a/b")
      .serialization()
      .commit();
}
e::error register_child(const m::registry_ref& registry) {
  return m::type_builder<child>{registry}
      .property<&child::value>(m::property_id{1}, "value", m::property_flags::required)
      .serialization()
      .validate<&check>()
      .commit();
}
std::span<const std::byte> bytes(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}
e::result<std::vector<std::byte>, m::serialization_diagnostic> encode_custom(const child& value) {
  if (value.value == 13)
    return std::vector<std::byte>{std::byte{'['}, std::byte{']'}};
  auto text = bytes(R"({"custom":true})");
  return std::vector<std::byte>(text.begin(), text.end());
}
e::result<child, m::serialization_diagnostic> decode_custom(std::span<const std::byte>) {
  return child{42};
}
template <unsigned Depth> struct chain {
  // 逐层默认构造，避免 MSVC 对 65 层聚合初始化的编译器深度限制。
  chain() {}
  static constexpr std::string_view type_name = "chain";
  static constexpr e::guid type_guid =
      e::guid::from_bytes({92, static_cast<std::uint8_t>(Depth + 1)});
  chain<Depth - 1> nested;
};
template <> struct chain<0> {
  static constexpr std::string_view type_name = "chain_leaf";
  static constexpr e::guid type_guid = e::guid::from_bytes({92, 1});
  float value = 1;
};
template <unsigned Depth> e::error register_chain(const m::registry_ref& registry) {
  if constexpr (Depth == 0)
    return m::type_builder<chain<0>>{registry}
        .property<&chain<0>::value>(m::property_id{1}, "value")
        .serialization()
        .commit();
  else {
    auto status = register_chain<Depth - 1>(registry);
    if (status.failed())
      return status;
    return m::type_builder<chain<Depth>>{registry}
        .template property<&chain<Depth>::nested>(m::property_id{1}, "nested",
                                                  m::property_flags::required)
        .serialization()
        .commit();
  }
}
}
bool nested_json_contract() {
  auto registry = m::create_registry();
  if (!registry || register_parent(*registry).failed() ||
      m::freeze(*registry) != e::error::not_found || register_child(*registry).failed() ||
      m::freeze(*registry).failed())
    return false;
  auto defaults_registry = m::create_registry();
  if (!defaults_registry || register_child(*defaults_registry).failed() ||
      m::type_builder<invalid_parent>{*defaults_registry}
          .property<&invalid_parent::item>(m::property_id{1}, "item")
          .serialization()
          .commit()
          .failed() ||
      m::freeze(*defaults_registry).failed())
    return false;
  auto invalid_default = m::deserialize<invalid_parent>(*defaults_registry, bytes("{}"));
  if (invalid_default || invalid_default.error().path != "/item/value")
    return false;
  auto metadata = m::describe_type<parent>(*registry);
  if (!metadata || metadata->properties[0].type.id() != e::runtime_type_of<child>() ||
      metadata->properties[0].type.guid() != child::type_guid ||
      metadata->properties[0].type.scalar_kind() != e::value_kind::invalid)
    return false;
  {
    parent original;
    original.item.value = 12;
    if (m::read_property(*registry, parent::type_guid, m::property_id{1}, &original).error() !=
        e::error::unsupported)
      return false;
    auto encoded = m::serialize(*registry, original);
    if (!encoded)
      return false;
    auto restored = m::deserialize_object(*registry, e::runtime_type_of<parent>(), *encoded);
    if (!restored || static_cast<parent*>(restored->get())->item.value != 12)
      return false;
    original.item.value = -1;
    auto invalid = m::serialize(*registry, original);
    if (invalid || invalid.error().path != "/a~1b/value")
      return false;
  }
  if (parent::alive != 0)
    return false;
  for (const auto text : {R"({"a/b":{"value":-1}})", R"({"a/b":{"value":"bad"}})", R"({"a/b":{}})",
                          R"({"a/b":{"value":1,"value":2}})"}) {
    auto invalid = m::deserialize_object(*registry, e::runtime_type_of<parent>(), bytes(text));
    if (invalid || invalid.error().path != "/a~1b/value" || parent::alive != 0)
      return false;
  }
  auto invalid =
      m::deserialize_object(*registry, e::runtime_type_of<parent>(), bytes(R"({"a/b":null})"));
  if (invalid || invalid.error().path != "/a~1b" || parent::alive != 0)
    return false;
  {
    auto defaults = m::deserialize_object(*registry, e::runtime_type_of<parent>(), bytes("{}"));
    if (!defaults || static_cast<parent*>(defaults->get())->item.value != 7)
      return false;
  }
  auto custom = m::create_registry();
  if (!custom || register_parent(*custom).failed() ||
      m::type_builder<child>{*custom}
          .serialization<&encode_custom, &decode_custom>()
          .validate<&check>()
          .commit()
          .failed() ||
      m::freeze(*custom).failed())
    return false;
  {
    parent original;
    auto encoded = m::serialize(*custom, original);
    if (!encoded)
      return false;
    const std::string_view text{reinterpret_cast<const char*>(encoded->data()), encoded->size()};
    if (text != R"({"a/b":{"custom":true}})")
      return false;
    auto restored = m::deserialize_object(*custom, e::runtime_type_of<parent>(), *encoded);
    if (!restored || static_cast<parent*>(restored->get())->item.value != 42)
      return false;
    original.item.value = 13;
    auto bad_output = m::serialize(*custom, original);
    if (bad_output || bad_output.error().path != "/a~1b" ||
        bad_output.error().error != e::error::invalid_argument)
      return false;
  }
  if (parent::alive != 0)
    return false;
  auto deep = m::create_registry();
  if (!deep || register_chain<65>(*deep).failed() || m::freeze(*deep).failed())
    return false;
  auto allowed = m::serialize(*deep, chain<64>{});
  auto excessive = m::serialize(*deep, chain<65>{});
  if (!allowed || excessive || excessive.error().error != e::error::invalid_argument)
    return false;
  if (!m::deserialize<chain<64>>(*deep, *allowed))
    return false;
  std::string text = R"({"value":1})";
  for (int i = 0; i < 65; ++i)
    text = "{\"nested\":" + text + "}";
  auto rejected = m::deserialize<chain<65>>(*deep, bytes(text));
  return !rejected && rejected.error().error == e::error::invalid_argument;
}
