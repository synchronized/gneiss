// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/serialization/context.hpp"
#include "engine/core/serialization/detail/typed_decode.hpp"
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
namespace gneiss::engine::serialization::json {
using meta::instance;
using meta::mutable_instance;
using meta::registry_ref;
/** 类型擦除入口；调用方保证对象类型。反射访问始终使用传入的冻结注册表。 */
GNEISS_API result<std::vector<std::byte>, serialization::diagnostic>
serialize_object(const context&, type_id, const void*) noexcept;
GNEISS_API result<ref<void>, serialization::diagnostic>
deserialize_object(const context&, type_id, std::span<const std::byte>) noexcept;

/** 编解码任意反射视图；解码调用方提供临时目标，失败不承诺恢复目标。 */
GNEISS_API result<std::vector<std::byte>, serialization::diagnostic>
serialize_instance(const context&, const instance&) noexcept;
GNEISS_API serialization::diagnostic deserialize_instance(const context&, const mutable_instance&,
                                                          std::span<const std::byte>) noexcept;
/** 使用视图绑定的注册表，包含独立注册表；不重新查询默认表。 */
GNEISS_API result<std::vector<std::byte>, serialization::diagnostic>
serialize_instance(const instance&) noexcept;
GNEISS_API serialization::diagnostic deserialize_instance(const mutable_instance&,
                                                          std::span<const std::byte>) noexcept;
/** 返回编码后的字节；未知类型不隐式注册。 */
template <class T>
[[nodiscard]] result<std::vector<std::byte>, serialization::diagnostic>
serialize_bytes(const context& ctx, const T& object) noexcept try {
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  auto view = instance::from(*registry, object);
  if (!view)
    return serialization::diagnostic{.error = view.error()};
  return serialize_instance(ctx, *view);
} catch (const std::bad_alloc&) {
  return serialization::diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization::diagnostic{.error = error::internal};
}
/** 解码返回独立对象；失败不向调用方暴露半成品。 */
template <class T>
[[nodiscard]] result<T, serialization::diagnostic>
deserialize(const context& ctx, std::span<const std::byte> bytes) noexcept try {
  return serialization::detail::decode_typed<T>(
      ctx, [&](const mutable_instance& view) { return deserialize_instance(ctx, view, bytes); },
      [&](type_id id) { return deserialize_object(ctx, id, bytes); });
} catch (const std::bad_alloc&) {
  return serialization::diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization::diagnostic{.error = error::internal};
}
/** 使用默认注册表，不隐式注册或冻结。 */
template <class T>
[[nodiscard]] result<std::vector<std::byte>, serialization::diagnostic>
serialize_bytes(const T& object) noexcept {
  context ctx;
  return serialize_bytes(ctx, object);
}
template <class T>
[[nodiscard]] result<T, serialization::diagnostic>
deserialize(std::span<const std::byte> bytes) noexcept {
  context ctx;
  return deserialize<T>(ctx, bytes);
}
/** 使用默认注册表的类型擦除与字段遍历入口；不隐式注册或冻结。 */
[[nodiscard]] inline result<std::vector<std::byte>, serialization::diagnostic>
serialize_object(type_id type, const void* object) noexcept {
  context ctx;
  return serialize_object(ctx, type, object);
}
[[nodiscard]] inline result<ref<void>, serialization::diagnostic>
deserialize_object(type_id type, std::span<const std::byte> bytes) noexcept {
  context ctx;
  return deserialize_object(ctx, type, bytes);
}
/** JSON 文本便捷入口；与字节入口具有相同的校验及诊断语义。 */
template <class T>
[[nodiscard]] result<T, serialization::diagnostic> deserialize(std::string_view json) noexcept {
  return deserialize<T>(std::as_bytes(std::span{json.data(), json.size()}));
}
template <class T>
[[nodiscard]] result<std::string, serialization::diagnostic> serialize(const context& ctx,
                                                                       const T& object) noexcept
    try {
  auto bytes = serialize_bytes(ctx, object);
  if (!bytes)
    return std::move(bytes).error();
  return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
} catch (const std::bad_alloc&) {
  return serialization::diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization::diagnostic{.error = error::internal};
}

/** 默认注册表文本入口；不隐式注册或冻结。 */
template <class T>
[[nodiscard]] result<std::string, serialization::diagnostic> serialize(const T& object) noexcept {
  context ctx;
  return serialize(ctx, object);
}
template <class T>
[[nodiscard]] result<T, serialization::diagnostic> deserialize(const context& ctx,
                                                               std::string_view text) noexcept {
  return deserialize<T>(ctx, std::as_bytes(std::span{text.data(), text.size()}));
}

}
