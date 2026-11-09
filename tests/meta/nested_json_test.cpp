// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/detail/json_adapter.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/core/serialization/serialization.hpp"
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
gneiss::engine::serialization::diagnostic check(const child& value) {
  if (value.value <= 0)
    return {.path = "/value", .error = e::error::invalid_argument};
  return {};
}
e::error register_parent(const m::registry_ref& registry) {
  return m::registration<parent>{registry}
      .property<&parent::item>(m::property_id{1}, "a/b")
      .commit();
}
e::error register_child(const m::registry_ref& registry) {
  return m::registration<child>{registry}
      .property<&child::value>(m::property_id{1}, "value")(
          gneiss::engine::serialization::field_policy{.required = true})
      .extend(gneiss::engine::serialization::validation<&check>)
      .commit();
}
std::span<const std::byte> bytes(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}
e::result<void*, gneiss::engine::serialization::diagnostic>
encode_custom(const e::serialization::write_context&, e::serialization::writer& output,
              const child& value) {
  if (value.value == 13)
    return output.array();
  auto root = output.object();
  auto flag = output.scalar(e::value{true});
  if (!flag || !output.member(root, "custom", *flag))
    return gneiss::engine::serialization::diagnostic{.error = e::error::out_of_memory};
  return root;
}
e::result<child, gneiss::engine::serialization::diagnostic>
decode_custom(const e::serialization::read_context&, e::serialization::reader&, const void*) {
  return child{42};
}
// 通用回调重入 JSON 入口仍沿用上下文限制。
e::result<void*, gneiss::engine::serialization::diagnostic>
encode_reentrant(const e::serialization::write_context& ctx, e::serialization::writer& output,
                 const child& value) {
  if (value.value != 0) {
    auto nested = e::serialization::json::encode_bytes(ctx, value);
    if (!nested)
      return std::move(nested).error();
  }
  return output.object();
}
e::result<child, gneiss::engine::serialization::diagnostic>
decode_reentrant(const e::serialization::read_context& ctx, e::serialization::reader& input,
                 const void* node) {
  if (input.size(node) != 0)
    return e::serialization::json::decode<child>(ctx, bytes(R"({"again":1})"));
  return child{0};
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
    return m::registration<chain<0>>{registry}
        .property<&chain<0>::value>(m::property_id{1}, "value")
        .commit();
  else {
    auto status = register_chain<Depth - 1>(registry);
    if (status.failed())
      return status;
    return m::registration<chain<Depth>>{registry}
        .template property<&chain<Depth>::nested>(m::property_id{1}, "nested")(
            gneiss::engine::serialization::field_policy{.required = true})
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
      m::registration<invalid_parent>{*defaults_registry}
          .property<&invalid_parent::item>(m::property_id{1}, "item")
          .commit()
          .failed() ||
      m::freeze(*defaults_registry).failed())
    return false;
  auto invalid_default = gneiss::engine::serialization::json::decode<invalid_parent>(
      gneiss::engine::serialization::read_context{*defaults_registry}, bytes("{}"));
  if (invalid_default || invalid_default.error().path != "/item/value")
    return false;
  auto metadata = m::type_of<parent>(*registry);
  if (!metadata || metadata.properties()[0].type().id() != e::runtime_type_of<child>() ||
      metadata.properties()[0].type().guid() != child::type_guid ||
      metadata.properties()[0].type().scalar_kind() != e::value_kind::invalid)
    return false;
  {
    parent original;
    original.item.value = 12;
    if (m::read_property(*registry, parent::type_guid, m::property_id{1}, &original).error() !=
        e::error::unsupported)
      return false;
    auto encoded = gneiss::engine::serialization::json::encode_bytes(
        gneiss::engine::serialization::write_context{*registry}, original);
    if (!encoded)
      return false;
    auto restored = gneiss::engine::serialization::json::decode_object(
        gneiss::engine::serialization::read_context{*registry}, e::runtime_type_of<parent>(),
        *encoded);
    if (!restored || static_cast<parent*>(restored->get())->item.value != 12)
      return false;
    original.item.value = -1;
    auto invalid = gneiss::engine::serialization::json::encode_bytes(
        gneiss::engine::serialization::write_context{*registry}, original);
    if (invalid || invalid.error().path != "/a~1b/value")
      return false;
  }
  if (parent::alive != 0)
    return false;
  for (const auto text : {R"({"a/b":{"value":-1}})", R"({"a/b":{"value":"bad"}})", R"({"a/b":{}})",
                          R"({"a/b":{"value":1,"value":2}})"}) {
    auto invalid = gneiss::engine::serialization::json::decode_object(
        gneiss::engine::serialization::read_context{*registry}, e::runtime_type_of<parent>(),
        bytes(text));
    if (invalid || invalid.error().path != "/a~1b/value" || parent::alive != 0)
      return false;
  }
  auto invalid = gneiss::engine::serialization::json::decode_object(
      gneiss::engine::serialization::read_context{*registry}, e::runtime_type_of<parent>(),
      bytes(R"({"a/b":null})"));
  if (invalid || invalid.error().path != "/a~1b" || parent::alive != 0)
    return false;
  {
    auto defaults = gneiss::engine::serialization::json::decode_object(
        gneiss::engine::serialization::read_context{*registry}, e::runtime_type_of<parent>(),
        bytes("{}"));
    if (!defaults || static_cast<parent*>(defaults->get())->item.value != 7)
      return false;
  }
  auto custom = m::create_registry();
  if (!custom || register_parent(*custom).failed() ||
      m::registration<child>{*custom}
          .extend(gneiss::engine::serialization::codec<&encode_custom, &decode_custom>)
          .extend(gneiss::engine::serialization::validation<&check>)
          .commit()
          .failed() ||
      m::freeze(*custom).failed())
    return false;
  {
    parent original;
    auto encoded = gneiss::engine::serialization::json::encode_bytes(
        gneiss::engine::serialization::write_context{*custom}, original);
    if (!encoded)
      return false;
    const std::string_view text{reinterpret_cast<const char*>(encoded->data()), encoded->size()};
    if (text != R"({"a/b":{"custom":true}})")
      return false;
    auto restored = gneiss::engine::serialization::json::decode_object(
        gneiss::engine::serialization::read_context{*custom}, e::runtime_type_of<parent>(),
        *encoded);
    if (!restored || static_cast<parent*>(restored->get())->item.value != 42)
      return false;
    original.item.value = 13;
    auto bad_output = gneiss::engine::serialization::json::encode_bytes(
        gneiss::engine::serialization::write_context{*custom}, original);
    if (bad_output || bad_output.error().path != "/a~1b" ||
        bad_output.error().error != e::error::invalid_argument)
      return false;
  }
  if (parent::alive != 0)
    return false;
  // 默认字段遍历使用独立表的字段名和校验。
  auto node_registry = m::create_registry();
  if (!node_registry || register_parent(*node_registry).failed() ||
      m::registration<child>{*node_registry}
          .property<&child::value>(m::property_id{1}, "isolated")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .extend(gneiss::engine::serialization::validation<&check>)
          .commit()
          .failed() ||
      m::freeze(*node_registry).failed())
    return false;
  {
    auto encoded = gneiss::engine::serialization::json::encode_bytes(
        gneiss::engine::serialization::write_context{*node_registry}, parent{});
    if (!encoded)
      return false;
    const std::string_view text{reinterpret_cast<const char*>(encoded->data()), encoded->size()};
    if (text != R"({"a/b":{"isolated":7.0}})" && text != R"({"a/b":{"isolated":7}})")
      return false;
    auto restored = gneiss::engine::serialization::json::decode<parent>(
        gneiss::engine::serialization::read_context{*node_registry}, *encoded);
    if (!restored || restored->item.value != 7)
      return false;
    auto bad = gneiss::engine::serialization::json::decode<parent>(
        gneiss::engine::serialization::read_context{*node_registry},
        bytes(R"({"a/b":{"isolated":"bad"}})"));
    if (bad || bad.error().path != "/a~1b/isolated")
      return false;
    auto unknown = gneiss::engine::serialization::json::decode<child>(
        gneiss::engine::serialization::read_context{*node_registry},
        bytes(R"({"isolated":7,"unknown":1})"));
    if (unknown || unknown.error().path != "/unknown")
      return false;
    auto too_large = gneiss::engine::serialization::json::decode<child>(
        gneiss::engine::serialization::read_context{*node_registry}, bytes(std::string(65, ' ')));
    if (too_large || too_large.error().error != e::error::invalid_argument)
      return false;
    auto invalid = gneiss::engine::serialization::json::encode_bytes(
        gneiss::engine::serialization::write_context{*node_registry}, child{-1});
    if (invalid || invalid.error().path != "/value")
      return false;
  }
  // 默认字段遍历保持对象层级，不引入额外包装层。
  e::serialization::write_context one_level{*node_registry, 1};
  e::serialization::read_context read_one_level{*node_registry, 1};
  auto shallow_text = e::serialization::json::encode(one_level, parent{});
  if (!shallow_text ||
      !e::serialization::json::decode<parent>(read_one_level, std::string_view{*shallow_text}))
    return false;
  e::serialization::write_context zero_levels{*node_registry, 0};

  if (e::serialization::json::encode(zero_levels, parent{}))
    return false;
  // 注册表不匹配必须在写入调用方目标之前失败。
  child destination{19};
  auto destination_view = m::view(*node_registry, destination);
  e::serialization::write_context mismatch{*custom};
  e::serialization::read_context read_mismatch{*custom};
  if (!destination_view ||
      e::serialization::json::read_into(read_mismatch, *destination_view,
                                        bytes(R"({"isolated":8})"))
              .error()
              .error != e::error::invalid_argument ||
      destination.value != 19)
    return false;
  if (e::serialization::json::encode_instance(mismatch, destination_view->as_const()))
    return false;
  auto reentrant = m::create_registry();
  if (!reentrant ||
      m::registration<child>{*reentrant}
          .extend(gneiss::engine::serialization::codec<&encode_reentrant, &decode_reentrant>)
          .commit()
          .failed() ||
      m::freeze(*reentrant).failed())
    return false;
  e::serialization::write_context recursion{*reentrant, 2};
  e::serialization::read_context read_recursion{*reentrant, 2};
  auto recursive_write = e::serialization::json::encode_bytes(recursion, child{1});
  auto recursive_read =
      e::serialization::json::decode<child>(read_recursion, bytes(R"({"again":1})"));
  if (!recursion.property_path().empty() || !read_recursion.property_path().empty() ||
      recursive_write || recursive_read ||
      recursive_write.error().error != e::error::invalid_argument ||
      recursive_read.error().error != e::error::invalid_argument)
    return false;
  if (!e::serialization::json::encode_bytes(recursion, child{0}) ||
      !e::serialization::json::decode<child>(read_recursion, bytes("{}")) ||
      !recursion.property_path().empty() || !read_recursion.property_path().empty())
    return false;
  auto deep = m::create_registry();
  if (!deep || register_chain<65>(*deep).failed() || m::freeze(*deep).failed())
    return false;
  auto allowed = gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{*deep}, chain<64>{});
  auto excessive = gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{*deep}, chain<65>{});
  if (!allowed || excessive || excessive.error().error != e::error::invalid_argument)
    return false;
  if (!gneiss::engine::serialization::json::decode<chain<64>>(
          gneiss::engine::serialization::read_context{*deep}, *allowed))
    return false;
  std::string text = R"({"value":1})";
  for (int i = 0; i < 65; ++i)
    text = "{\"nested\":" + text + "}";
  auto rejected = gneiss::engine::serialization::json::decode<chain<65>>(
      gneiss::engine::serialization::read_context{*deep}, bytes(text));
  return !rejected && rejected.error().error == e::error::invalid_argument;
}
