// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/json.hpp"
#include <limits>
namespace gneiss::engine {
struct material_description;
struct mesh_description;
struct action_map;
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
};
template <class T> struct format_traits {
  static constexpr bool enabled = false;
};
template <> struct format_traits<mesh_description> {
  static constexpr bool enabled = true;
  static GNEISS_API format get() noexcept;
};

template <> struct format_traits<material_description> {
  static constexpr bool enabled = true;
  static GNEISS_API format get() noexcept;
};
template <> struct format_traits<action_map> {
  static constexpr bool enabled = true;
  static GNEISS_API format get() noexcept;
};
template <> struct format_traits<scene_description> {
  static constexpr bool enabled = true;
  static GNEISS_API format get() noexcept;
};
template <> struct format_traits<prefab_description> {
  static constexpr bool enabled = true;
  static GNEISS_API format get() noexcept;
};
template <> struct format_traits<texture_description> {
  static constexpr bool enabled = true;
  static GNEISS_API format get() noexcept;
};
GNEISS_API result<std::vector<std::byte>, serialization::diagnostic>
serialize_instance(const serialization::context&, format, const meta::instance&) noexcept;
GNEISS_API result<ref<void>, serialization::diagnostic>
deserialize_object(const serialization::context&, format, type_id,
                   std::span<const std::byte>) noexcept;
template <class T>
result<std::vector<std::byte>, serialization::diagnostic>
serialize_bytes(const serialization::context& ctx, const T& object) noexcept {
  static_assert(format_traits<T>::enabled, "该类型没有资源文件格式");
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  auto view = meta::instance::from(*registry, object);
  if (!view)
    return serialization::diagnostic{.error = view.error()};
  return serialize_instance(ctx, format_traits<T>::get(), *view);
}
template <class T>
result<std::vector<std::byte>, serialization::diagnostic>
serialize_bytes(const T& object) noexcept {
  serialization::context ctx;
  return serialize_bytes(ctx, object);
}
template <class T>
result<T, serialization::diagnostic> deserialize(const serialization::context& ctx,
                                                 std::span<const std::byte> bytes) noexcept try {
  static_assert(format_traits<T>::enabled, "该类型没有资源文件格式");
  auto decoded = deserialize_object(ctx, format_traits<T>::get(), runtime_type_of<T>(), bytes);
  if (!decoded)
    return std::move(decoded).error();
  return std::move(*static_cast<T*>(decoded->get()));
} catch (const std::bad_alloc&) {
  return serialization::diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization::diagnostic{.error = error::internal};
}
template <class T>
result<T, serialization::diagnostic> deserialize(std::span<const std::byte> bytes) noexcept {
  serialization::context ctx;
  return deserialize<T>(ctx, bytes);
}
template <class T>
result<T, serialization::diagnostic> deserialize(const serialization::context& ctx,
                                                 std::string_view text) noexcept {
  return deserialize<T>(ctx, std::as_bytes(std::span{text.data(), text.size()}));
}
template <class T>
result<T, serialization::diagnostic> deserialize(std::string_view text) noexcept {
  serialization::context ctx;
  return deserialize<T>(ctx, text);
}
template <class T>
result<std::string, serialization::diagnostic> serialize(const serialization::context& ctx,
                                                         const T& object) noexcept try {
  auto bytes = serialize_bytes(ctx, object);
  if (!bytes)
    return std::move(bytes).error();
  return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
} catch (const std::bad_alloc&) {
  return serialization::diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization::diagnostic{.error = error::internal};
}
template <class T>
result<std::string, serialization::diagnostic> serialize(const T& object) noexcept {
  serialization::context ctx;
  return serialize(ctx, object);
}

}
}
