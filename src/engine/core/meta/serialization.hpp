// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registry.hpp"
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
namespace gneiss::engine::meta {
/** 类型擦除入口；调用方保证对象类型。反射访问始终使用传入的冻结注册表。 */
GNEISS_API result<std::vector<std::byte>, serialization_diagnostic>
serialize_object(const registry_ref&, type_id, const void*) noexcept;
GNEISS_API result<ref<void>, serialization_diagnostic>
deserialize_object(const registry_ref&, type_id, std::span<const std::byte>) noexcept;

/** 格式包装器使用：只遍历注册字段，跳过本类型自定义 codec，保留嵌套 codec 和校验。
 * 类型必须显式启用 reflected_json。 */
GNEISS_API result<std::vector<std::byte>, serialization_diagnostic>
serialize_reflected_object(const registry_ref&, type_id, const void*) noexcept;
GNEISS_API result<ref<void>, serialization_diagnostic>
deserialize_reflected_object(const registry_ref&, type_id, std::span<const std::byte>) noexcept;
template <class T>
result<std::vector<std::byte>, serialization_diagnostic>
serialize_reflected(const registry_ref& registry, const T& object) noexcept {
  return serialize_reflected_object(registry, runtime_type_of<T>(), &object);
}
template <class T>
result<T, serialization_diagnostic> deserialize_reflected(const registry_ref& registry,
                                                          std::span<const std::byte> bytes) noexcept
    try {
  auto decoded = deserialize_reflected_object(registry, runtime_type_of<T>(), bytes);
  if (!decoded)
    return std::move(decoded).error();
  return std::move(*static_cast<T*>(decoded->get()));
} catch (const std::bad_alloc&) {
  return serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization_diagnostic{.error = error::internal};
}

/** 编解码任意反射视图；解码调用方提供临时目标，失败不承诺恢复目标。 */
GNEISS_API result<std::vector<std::byte>, serialization_diagnostic>
serialize_instance(const registry_ref&, const instance&) noexcept;
GNEISS_API serialization_diagnostic deserialize_instance(const registry_ref&,
                                                         const mutable_instance&,
                                                         std::span<const std::byte>) noexcept;
/** 返回编码后的字节；未知类型不隐式注册。 */
template <class T>
[[nodiscard]] result<std::vector<std::byte>, serialization_diagnostic>
serialize(const registry_ref& registry, const T& object) noexcept try {
  if constexpr (detail::reference_traits<std::remove_cv_t<T>>::enabled ||
                sequence_traits<T>::enabled || associative_traits<T>::enabled) {
    auto view = instance::from(registry, object);
    if (!view)
      return serialization_diagnostic{.error = view.error()};
    return serialize_instance(registry, *view);
  } else
    return serialize_object(registry, runtime_type_of<T>(), &object);
} catch (const std::bad_alloc&) {
  return serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization_diagnostic{.error = error::internal};
}
/** 解码返回独立对象；失败不向调用方暴露半成品。 */
template <class T>
[[nodiscard]] result<T, serialization_diagnostic>
deserialize(const registry_ref& registry, std::span<const std::byte> bytes) noexcept try {
  if constexpr (detail::reference_traits<T>::enabled || sequence_traits<T>::enabled ||
                associative_traits<T>::enabled) {
    T object{};
    auto view = mutable_instance::from(registry, object);
    if (!view)
      return serialization_diagnostic{.error = view.error()};
    auto status = deserialize_instance(registry, *view, bytes);
    if (status.error.failed())
      return status;
    return object;
  } else {
    auto decoded = deserialize_object(registry, runtime_type_of<T>(), bytes);
    if (!decoded)
      return std::move(decoded).error();
    if (!*decoded)
      return serialization_diagnostic{.error = error::internal};
    return std::move(*static_cast<T*>(decoded->get()));
  }
} catch (const std::bad_alloc&) {
  return serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization_diagnostic{.error = error::internal};
}
/** 使用默认注册表，不隐式注册或冻结。 */
template <class T>
[[nodiscard]] result<std::vector<std::byte>, serialization_diagnostic>
serialize(const T& object) noexcept {
  auto registry = default_registry();
  if (!registry)
    return serialization_diagnostic{.error = registry.error()};
  return serialize(*registry, object);
}
template <class T>
[[nodiscard]] result<T, serialization_diagnostic>
deserialize(std::span<const std::byte> bytes) noexcept {
  auto registry = default_registry();
  if (!registry)
    return serialization_diagnostic{.error = registry.error()};
  return deserialize<T>(*registry, bytes);
}
/** 使用默认注册表的类型擦除与字段遍历入口；不隐式注册或冻结。 */
[[nodiscard]] inline result<std::vector<std::byte>, serialization_diagnostic>
serialize_object(type_id type, const void* object) noexcept {
  auto registry = default_registry();
  if (!registry)
    return serialization_diagnostic{.error = registry.error()};
  return serialize_object(*registry, type, object);
}
[[nodiscard]] inline result<ref<void>, serialization_diagnostic>
deserialize_object(type_id type, std::span<const std::byte> bytes) noexcept {
  auto registry = default_registry();
  if (!registry)
    return serialization_diagnostic{.error = registry.error()};
  return deserialize_object(*registry, type, bytes);
}
[[nodiscard]] inline result<std::vector<std::byte>, serialization_diagnostic>
serialize_reflected_object(type_id type, const void* object) noexcept {
  auto registry = default_registry();
  if (!registry)
    return serialization_diagnostic{.error = registry.error()};
  return serialize_reflected_object(*registry, type, object);
}
[[nodiscard]] inline result<ref<void>, serialization_diagnostic>
deserialize_reflected_object(type_id type, std::span<const std::byte> bytes) noexcept {
  auto registry = default_registry();
  if (!registry)
    return serialization_diagnostic{.error = registry.error()};
  return deserialize_reflected_object(*registry, type, bytes);
}
template <class T>
[[nodiscard]] result<std::vector<std::byte>, serialization_diagnostic>
serialize_reflected(const T& object) noexcept {
  auto registry = default_registry();
  if (!registry)
    return serialization_diagnostic{.error = registry.error()};
  return serialize_reflected(*registry, object);
}
template <class T>
[[nodiscard]] result<T, serialization_diagnostic>
deserialize_reflected(std::span<const std::byte> bytes) noexcept {
  auto registry = default_registry();
  if (!registry)
    return serialization_diagnostic{.error = registry.error()};
  return deserialize_reflected<T>(*registry, bytes);
}

/** JSON 文本便捷入口；与字节入口具有相同的校验及诊断语义。 */
template <class T>
[[nodiscard]] result<T, serialization_diagnostic> deserialize(std::string_view json) noexcept {
  return deserialize<T>(std::as_bytes(std::span{json.data(), json.size()}));
}
template <class T>
[[nodiscard]] result<std::string, serialization_diagnostic> serialize_json(const T& object) noexcept
    try {
  auto bytes = serialize(object);
  if (!bytes)
    return std::move(bytes).error();
  return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
} catch (const std::bad_alloc&) {
  return serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization_diagnostic{.error = error::internal};
}

}
