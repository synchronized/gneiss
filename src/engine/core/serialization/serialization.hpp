// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/serialization/context.hpp"
#include "engine/core/serialization/reader.hpp"
#include "engine/core/serialization/writer.hpp"
namespace gneiss::engine::serialization {
/** context 与视图必须绑定同一注册表；后端和对象均借用。失败时 writer 可能保留未完成输出。 */
GNEISS_API serialization::diagnostic serialize(const context&, const meta::instance&,
                                               writer&) noexcept;
/** 写入调用方目标；失败不保证恢复。需要原子替换时使用返回独立对象的模板入口。 */
GNEISS_API serialization::diagnostic deserialize(const context&, reader&,
                                                 const meta::mutable_instance&) noexcept;
GNEISS_API serialization::diagnostic serialize(const meta::instance&, writer&) noexcept;
GNEISS_API serialization::diagnostic deserialize(reader&, const meta::mutable_instance&) noexcept;
GNEISS_API result<ref<void>, serialization::diagnostic> deserialize_object(const context&, type_id,
                                                                           reader&) noexcept;
inline result<ref<void>, serialization::diagnostic> deserialize_object(type_id id,
                                                                       reader& input) noexcept {
  context ctx;
  return deserialize_object(ctx, id, input);
}

template <class T>
serialization::diagnostic serialize(const context& ctx, const T& value, writer& output) noexcept
    try {
  auto registry = ctx.registry();
  if (!registry)
    return {.error = registry.error()};
  auto view = meta::instance::from(*registry, value);
  if (!view)
    return {.error = view.error()};
  return serialize(ctx, *view, output);
} catch (const std::bad_alloc&) {
  return {.error = error::out_of_memory};
} catch (...) {
  return {.error = error::internal};
}
template <class T> serialization::diagnostic serialize(const T& value, writer& output) noexcept {
  context ctx;
  return serialize(ctx, value, output);
}
template <class T>
result<T, serialization::diagnostic> deserialize(const context& ctx, reader& input) noexcept try {
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  if constexpr (meta::detail::reflected_value_kind<T>() != value_kind::invalid ||
                meta::detail::optional_traits<T>::enabled ||
                meta::detail::reference_traits<T>::enabled || meta::sequence_traits<T>::enabled ||
                meta::associative_traits<T>::enabled || meta::variant_traits<T>::enabled) {
    T result{};
    auto view = meta::mutable_instance::from(*registry, result);
    if (!view)
      return serialization::diagnostic{.error = view.error()};
    auto status = deserialize(ctx, input, *view);
    if (status.error.failed())
      return status;
    return result;
  } else {
    auto decoded = deserialize_object(ctx, runtime_type_of<T>(), input);
    if (!decoded)
      return std::move(decoded).error();
    return std::move(*static_cast<T*>(decoded->get()));
  }
} catch (const std::bad_alloc&) {
  return serialization::diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization::diagnostic{.error = error::internal};
}
template <class T> result<T, serialization::diagnostic> deserialize(reader& input) noexcept {
  context ctx;
  return deserialize<T>(ctx, input);
}
}
