// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/serialization/reader.hpp"
#include "engine/core/serialization/writer.hpp"
namespace gneiss::engine::serialization {
/** 使用视图携带的注册表；后端和对象均借用。失败时 writer 可能保留未完成输出。 */
GNEISS_API meta::serialization_diagnostic serialize(const meta::instance&, writer&) noexcept;
/** 写入调用方目标；失败不保证恢复。需要原子替换时使用返回独立对象的模板入口。 */
GNEISS_API meta::serialization_diagnostic deserialize(reader&,
                                                      const meta::mutable_instance&) noexcept;
GNEISS_API result<ref<void>, meta::serialization_diagnostic>
deserialize_object(const meta::registry_ref&, type_id, reader&) noexcept;
inline result<ref<void>, meta::serialization_diagnostic>
deserialize_object(type_id id, reader& input) noexcept {
  auto registry = meta::default_registry();
  if (!registry)
    return meta::serialization_diagnostic{.error = registry.error()};
  return deserialize_object(*registry, id, input);
}

template <class T>
meta::serialization_diagnostic serialize(const meta::registry_ref& registry, const T& value,
                                         writer& output) noexcept try {
  auto view = meta::instance::from(registry, value);
  if (!view)
    return {.error = view.error()};
  return serialize(*view, output);
} catch (const std::bad_alloc&) {
  return {.error = error::out_of_memory};
} catch (...) {
  return {.error = error::internal};
}
template <class T>
meta::serialization_diagnostic serialize(const T& value, writer& output) noexcept {
  auto registry = meta::default_registry();
  if (!registry)
    return {.error = registry.error()};
  return serialize(*registry, value, output);
}
template <class T>
result<T, meta::serialization_diagnostic> deserialize(const meta::registry_ref& registry,
                                                      reader& input) noexcept try {
  if constexpr (meta::detail::reflected_value_kind<T>() != value_kind::invalid ||
                meta::detail::optional_traits<T>::enabled ||
                meta::detail::reference_traits<T>::enabled || meta::sequence_traits<T>::enabled ||
                meta::associative_traits<T>::enabled || meta::detail::variant_traits<T>::enabled) {
    T result{};
    auto view = meta::mutable_instance::from(registry, result);
    if (!view)
      return meta::serialization_diagnostic{.error = view.error()};
    auto status = deserialize(input, *view);
    if (status.error.failed())
      return status;
    return result;
  } else {
    auto decoded = deserialize_object(registry, runtime_type_of<T>(), input);
    if (!decoded)
      return std::move(decoded).error();
    return std::move(*static_cast<T*>(decoded->get()));
  }
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
template <class T> result<T, meta::serialization_diagnostic> deserialize(reader& input) noexcept {
  auto registry = meta::default_registry();
  if (!registry)
    return meta::serialization_diagnostic{.error = registry.error()};
  return deserialize<T>(*registry, input);
}
}
