// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/registry.hpp"
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
namespace gneiss::engine::meta {
/** 返回编码后的字节；未知类型不隐式注册。 */
template <class T>
[[nodiscard]] result<std::vector<std::byte>, serialization_diagnostic>
serialize(const registry_ref& registry, const T& object) noexcept try {
  auto binding = find_serialization(registry, runtime_type_of<T>());
  if (!binding)
    return serialization_diagnostic{.error = binding.error()};
  if (!binding->encode)
    return serialization_diagnostic{.error = error::unsupported};
  return binding->encode(&object);
} catch (const std::bad_alloc&) {
  return serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization_diagnostic{.error = error::internal};
}
/** 解码返回独立对象；失败不向调用方暴露半成品。 */
template <class T>
[[nodiscard]] result<T, serialization_diagnostic>
deserialize(const registry_ref& registry, std::span<const std::byte> bytes) noexcept try {
  auto binding = find_serialization(registry, runtime_type_of<T>());
  if (!binding)
    return serialization_diagnostic{.error = binding.error()};
  if (!binding->decode)
    return serialization_diagnostic{.error = error::unsupported};
  auto decoded = binding->decode(bytes);
  if (!decoded)
    return std::move(decoded).error();
  if (!*decoded)
    return serialization_diagnostic{.error = error::internal};
  return std::move(*std::static_pointer_cast<T>(*decoded));
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
}
