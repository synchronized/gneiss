// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/registry.hpp"
#include "engine/core/serialization/diagnostic.hpp"
#include <functional>
namespace gneiss::engine::serialization {
class context;
class reader;
class writer;
/** 特殊内容编解码。未提供的方向采用普通字段遍历；回调借用上下文和后端。 */
struct content_codec {
  result<void*, diagnostic> (*encode)(const context&, writer&, const void*){};
  result<ref<void>, diagnostic> (*decode)(const context&, reader&, const void*){};
};
/** 引用位置策略，不改变对象自身的内容格式。空引用由框架处理。 */
struct reference_policy {
  result<void*, diagnostic> (*encode)(const context&, writer&, const void*){};
  result<ref<void>, diagnostic> (*decode)(const context&, reader&, const void*){};
  bool polymorphic = false;
  result<asset_reference_value> (*capture)(engine::guid, const void*){};
};
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
inline result<type_policies> find_policies(const meta::registry_ref& registry,
                                           type_id id) noexcept {
  auto type = meta::find_type(registry, id);
  return type ? result<type_policies>{policies_of(*type)} : result<type_policies>{type.error()};
}
inline result<type_policies> find_policies(type_id id) noexcept {
  auto type = meta::find_type(id);
  return type ? result<type_policies>{policies_of(*type)} : result<type_policies>{type.error()};
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
      output.encode = [](const context& ctx, writer& out,
                         const void* object) -> result<void*, diagnostic> {
        return std::invoke(Encode, ctx, out, *static_cast<const T*>(object));
      };
  }
  if constexpr (!std::is_null_pointer_v<decltype(Decode)>) {
    const auto decode = Decode;
    if (decode)
      output.decode = [](const context& ctx, reader& in,
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
