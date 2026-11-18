// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/resource/export.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <limits>
namespace gneiss {
struct material_description;
struct mesh_description;
namespace input {
struct action_map_description;
}
struct scene_description;
struct prefab_description;
struct texture_description;
namespace asset_file {
/** 文件外层约束；不属于描述对象的反射元数据。 */
struct format {
  std::string_view name;
  std::uint64_t version;
  std::size_t maximum_bytes = std::numeric_limits<std::size_t>::max();
  std::span<const std::string_view> rejected_fields{};
  /** 文件读写边界的领域校验，不参与通用对象序列化。 */
  serialization::diagnostic (*validate)(const meta::const_instance&){};
};
template <class T> struct format_traits {
  static constexpr bool enabled = false;
};
template <> struct format_traits<mesh_description> {
  static constexpr bool enabled = true;
  static GNEISS_RESOURCE_API format get();
};

template <> struct format_traits<material_description> {
  static constexpr bool enabled = true;
  static GNEISS_RESOURCE_API format get();
};
template <> struct format_traits<gneiss::input::action_map_description> {
  static constexpr bool enabled = true;
  static GNEISS_RESOURCE_API format get();
};
template <> struct format_traits<scene_description> {
  static constexpr bool enabled = true;
  static GNEISS_RESOURCE_API format get();
};
template <> struct format_traits<prefab_description> {
  static constexpr bool enabled = true;
  static GNEISS_RESOURCE_API format get();
};
template <> struct format_traits<texture_description> {
  static constexpr bool enabled = true;
  static GNEISS_RESOURCE_API format get();
};
GNEISS_RESOURCE_API result<std::vector<std::byte>, serialization::diagnostic>
serialize_instance(const serialization::write_context&, format, const meta::const_instance&);
GNEISS_RESOURCE_API result<ref<void>, serialization::diagnostic>
deserialize_object(const serialization::read_context&, format, type_id, std::span<const std::byte>);
template <class T>
result<std::vector<std::byte>, serialization::diagnostic>
serialize_bytes(const serialization::write_context& ctx, const T& object) {
  static_assert(format_traits<T>::enabled, "该类型没有资源文件格式");
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  auto view = meta::view(*registry, std::as_const(object));
  return serialize_instance(ctx, format_traits<T>::get(), view);
}
template <class T>
result<std::vector<std::byte>, serialization::diagnostic> serialize_bytes(const T& object) {
  serialization::write_context ctx;
  return serialize_bytes(ctx, object);
}
/** 直接保留解码对象的控制块，返回独立只读快照；不登记资产缓存。 */
template <class T>
result<ref<const T>, serialization::diagnostic>
deserialize_ref(const serialization::read_context& ctx, std::span<const std::byte> bytes) {
  static_assert(format_traits<T>::enabled, "该类型没有资源文件格式");
  auto decoded = deserialize_object(ctx, format_traits<T>::get(), runtime_type_of<T>(), bytes);
  if (!decoded)
    return std::move(decoded).error();
  return decoded->alias(static_cast<const T*>(decoded->get()));
}
template <class T>
result<ref<const T>, serialization::diagnostic> deserialize_ref(std::span<const std::byte> bytes) {
  serialization::read_context ctx;
  return deserialize_ref<T>(ctx, bytes);
}

template <class T>
result<T, serialization::diagnostic> deserialize(const serialization::read_context& ctx,
                                                 std::span<const std::byte> bytes) {
  static_assert(format_traits<T>::enabled, "该类型没有资源文件格式");
  auto decoded = deserialize_object(ctx, format_traits<T>::get(), runtime_type_of<T>(), bytes);
  if (!decoded)
    return std::move(decoded).error();
  return std::move(*static_cast<T*>(decoded->get()));
}
template <class T>
result<T, serialization::diagnostic> deserialize(std::span<const std::byte> bytes) {
  serialization::read_context ctx;
  return deserialize<T>(ctx, bytes);
}
template <class T>
result<T, serialization::diagnostic> deserialize(const serialization::read_context& ctx,
                                                 std::string_view text) {
  return deserialize<T>(ctx, std::as_bytes(std::span{text.data(), text.size()}));
}
template <class T> result<T, serialization::diagnostic> deserialize(std::string_view text) {
  serialization::read_context ctx;
  return deserialize<T>(ctx, text);
}
template <class T>
result<std::string, serialization::diagnostic> serialize(const serialization::write_context& ctx,
                                                         const T& object) {
  auto bytes = serialize_bytes(ctx, object);
  if (!bytes)
    return std::move(bytes).error();
  return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}
template <class T> result<std::string, serialization::diagnostic> serialize(const T& object) {
  serialization::write_context ctx;
  return serialize(ctx, object);
}

}
}
