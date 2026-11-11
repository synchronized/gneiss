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
namespace gneiss::serialization::json {
using meta::const_instance;
using meta::instance;
using meta::registry_ref;
/** 类型擦除入口；调用方保证对象类型。反射访问始终使用传入的冻结注册表。 */
GNEISS_API result<std::vector<std::byte>, serialization::diagnostic>
encode_object(const write_context&, type_id, const void*);
GNEISS_API result<ref<void>, serialization::diagnostic> decode_object(const read_context&, type_id,
                                                                      std::span<const std::byte>);

/** 编解码任意反射视图；解码调用方提供临时目标，失败不承诺恢复目标。 */
GNEISS_API result<std::vector<std::byte>, serialization::diagnostic>
encode_instance(const write_context&, const const_instance&);
GNEISS_API result<void, serialization::diagnostic> read_into(const read_context&, const instance&,
                                                             std::span<const std::byte>);
/** 使用视图绑定的注册表，包含独立注册表；不重新查询默认表。 */
GNEISS_API result<std::vector<std::byte>, serialization::diagnostic>
encode_instance(const const_instance&);
GNEISS_API result<void, serialization::diagnostic> read_into(const instance&,
                                                             std::span<const std::byte>);
/** 返回编码后的字节；未知类型不隐式注册。 */
template <class T>
[[nodiscard]] result<std::vector<std::byte>, serialization::diagnostic>
encode_bytes(const write_context& ctx, const T& object) {
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  auto view = gneiss::meta::view(*registry, std::as_const(object));
  if (!view)
    return serialization::diagnostic{.error = view.error()};
  return encode_instance(ctx, *view);
}
/** 解码返回独立对象；失败不向调用方暴露半成品。 */
template <class T>
[[nodiscard]] result<T, serialization::diagnostic> decode(const read_context& ctx,
                                                          std::span<const std::byte> bytes) {
  return serialization::detail::decode_typed<T>(
      ctx, [&](const instance& view) { return read_into(ctx, view, bytes); },
      [&](type_id id) { return decode_object(ctx, id, bytes); });
}
/** 使用默认注册表，不隐式注册或冻结。 */
template <class T>
[[nodiscard]] result<std::vector<std::byte>, serialization::diagnostic>
encode_bytes(const T& object) {
  write_context ctx;
  return encode_bytes(ctx, object);
}
template <class T>
[[nodiscard]] result<T, serialization::diagnostic> decode(std::span<const std::byte> bytes) {
  read_context ctx;
  return decode<T>(ctx, bytes);
}
/** 使用默认注册表的类型擦除与字段遍历入口；不隐式注册或冻结。 */
[[nodiscard]] inline result<std::vector<std::byte>, serialization::diagnostic>
encode_object(type_id type, const void* object) {
  write_context ctx;
  return encode_object(ctx, type, object);
}
[[nodiscard]] inline result<ref<void>, serialization::diagnostic>
decode_object(type_id type, std::span<const std::byte> bytes) {
  read_context ctx;
  return decode_object(ctx, type, bytes);
}
/** JSON 文本便捷入口；与字节入口具有相同的校验及诊断语义。 */
template <class T>
[[nodiscard]] result<T, serialization::diagnostic> decode(std::string_view json) {
  return decode<T>(std::as_bytes(std::span{json.data(), json.size()}));
}
template <class T>
[[nodiscard]] result<std::string, serialization::diagnostic> encode(const write_context& ctx,
                                                                    const T& object) {
  auto bytes = encode_bytes(ctx, object);
  if (!bytes)
    return std::move(bytes).error();
  return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

/** 默认注册表文本入口；不隐式注册或冻结。 */
template <class T>
[[nodiscard]] result<std::string, serialization::diagnostic> encode(const T& object) {
  write_context ctx;
  return encode(ctx, object);
}
template <class T>
[[nodiscard]] result<T, serialization::diagnostic> decode(const read_context& ctx,
                                                          std::string_view text) {
  return decode<T>(ctx, std::as_bytes(std::span{text.data(), text.size()}));
}

}
