// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/meta/property.hpp>
#include <gneiss/engine/core/meta/type.hpp>
#include <gneiss/engine/core/serialization/diagnostic.hpp>

#include <functional>
#include <optional>

namespace gneiss::serialization {
class read_context;
class write_context;
class reader;
class writer;
/** 特殊内容编解码。未提供的方向采用普通字段遍历；回调借用上下文和后端。 */
struct content_codec {
  result<void*, diagnostic> (*encode)(const write_context&, writer&, const void*){};
  result<ref<void>, diagnostic> (*decode)(const read_context&, reader&, const void*){};
};
/** 引用位置策略，不改变对象自身的内容格式。空引用由框架处理。 */
struct reference_policy {
  result<void*, diagnostic> (*encode)(const write_context&, writer&, const void*){};
  result<ref<void>, diagnostic> (*decode)(const read_context&, reader&, const void*){};
  bool polymorphic = false;
  result<asset_reference_value> (*capture)(gneiss::guid, const void*){};
};
/** 字段持久化规则；不改变 Meta 的内存访问能力。 */
struct field_policy {
  bool save = true;
  bool required = false;
  bool enum_name = false;
  /** 仅覆盖直接引用字段；容器元素沿用元素类型策略。 */
  std::optional<reference_policy> reference{};
};
/** 字段规则由序列化边界校验，Meta 只保存扩展。 */
inline result<field_policy> policy_of(const meta::property& property) {
  const auto* extension = property.extension<field_policy>();
  const auto policy = extension ? *extension : field_policy{};
  if ((policy.required &&
       (!policy.save ||
        !meta::has_flags(property.capabilities(), meta::property_capabilities::writable))) ||
      (policy.enum_name && !property.type().is_enum()) ||
      (policy.reference && property.type().kind() != meta::type_kind::reference))
    return error::invalid_argument;
  return policy;
}
struct object_policy {
  bool reject_unknown_fields = false;
};
/** 显式对象约束；暂保留既有编解码与快照验证时机，领域约束将在领域入口独立调用。 */
struct validation_policy {
  diagnostic (*validate)(const void*){};
};
/** 一次查询的策略集合，不独立存储。字段覆盖由遍历调用方选择。 */
struct type_policies {
  content_codec content{};
  reference_policy reference{};
  object_policy object{};
  validation_policy validation{};
};
inline type_policies policies_of(const meta::type& type) noexcept {
  type_policies result;
  if (auto* p = type.extension<content_codec>())
    result.content = *p;
  if (auto* p = type.extension<reference_policy>())
    result.reference = *p;
  if (auto* p = type.extension<object_policy>())
    result.object = *p;
  if (auto* p = type.extension<validation_policy>())
    result.validation = *p;
  return result;
}
inline result<type_policies> find_policies(const meta::registry_ref& registry, type_id id) {
  auto frozen = meta::is_frozen(registry);
  if (!frozen)
    return frozen.error();
  if (!*frozen)
    return error::not_ready;
  auto type = meta::find_type(registry, id);
  return type ? result<type_policies>{policies_of(type)} : result<type_policies>{error::not_found};
}
inline result<type_policies> find_policies(type_id id) {
  auto frozen = meta::is_frozen();
  if (!frozen)
    return frozen.error();
  if (!*frozen)
    return error::not_ready;
  auto type = meta::find_type(id);
  return type ? result<type_policies>{policies_of(type)} : result<type_policies>{error::not_found};
}
/** 引用位置统一选择：字段显式覆盖 > 被引用类型默认 > 内嵌默认。 */
inline result<reference_policy> reference_policy_for(const meta::type& reference,
                                                     const reference_policy* override = nullptr) {
  if (reference.kind() != meta::type_kind::reference)
    return error::invalid_argument;
  auto target = reference.element_type();
  if (!target || target.kind() != meta::type_kind::object)
    return error::unsupported;
  if (override)
    return *override;
  const auto* policy = target.extension<reference_policy>();
  return policy ? *policy : reference_policy{};
}
/** 与 Meta 注册器 extend 配合；按注册对象类型生成校验适配。 */
template <auto Validate>
inline constexpr auto validation = []<class T>() -> result<validation_policy> {
  return validation_policy{[](const void* object) -> diagnostic {
    auto status = std::invoke(Validate, *static_cast<const T*>(object));
    if constexpr (std::is_same_v<decltype(status), error>)
      return {.error = status};
    else
      return status;
  }};
};
/** 类型安全特殊 codec 适配；两个方向均为空或重复登记在 commit 失败。 */
template <auto Encode, auto Decode>
inline constexpr auto codec = []<class T>() -> result<content_codec> {
  content_codec output;
  if constexpr (!std::is_null_pointer_v<decltype(Encode)>) {
    const auto encode = Encode;
    if (encode)
      output.encode = [](const write_context& ctx, writer& out,
                         const void* object) -> result<void*, diagnostic> {
        return std::invoke(Encode, ctx, out, *static_cast<const T*>(object));
      };
  }
  if constexpr (!std::is_null_pointer_v<decltype(Decode)>) {
    const auto decode = Decode;
    if (decode)
      output.decode = [](const read_context& ctx, reader& in,
                         const void* node) -> result<ref<void>, diagnostic> {
        auto decoded = std::invoke(Decode, ctx, in, node);
        if (!decoded)
          return std::move(decoded).error();
        return ref<void>{std::make_shared<T>(std::move(*decoded))};
      };
  }
  if (!output.encode && !output.decode)
    return error::invalid_argument;
  return output;
};
}
