// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/serialization/context.hpp"
#include "engine/core/serialization/detail/typed_decode.hpp"
#include "engine/core/serialization/reader.hpp"
#include "engine/core/serialization/writer.hpp"
namespace gneiss::engine::serialization {
/** context 与视图必须绑定同一注册表；后端和对象均借用。失败时 writer 可能保留未完成输出。 */
GNEISS_API result<void, diagnostic> write(const context&, const meta::const_instance&,
                                          writer&) noexcept;
/** 写入调用方目标；失败不保证恢复。需要原子替换时使用返回独立对象的模板入口。 */
GNEISS_API result<void, diagnostic> read_into(const context&, reader&,
                                              const meta::instance&) noexcept;
GNEISS_API result<void, diagnostic> write(const meta::const_instance&, writer&) noexcept;
GNEISS_API result<void, diagnostic> read_into(reader&, const meta::instance&) noexcept;
GNEISS_API result<ref<void>, serialization::diagnostic> decode_object(const context&, type_id,
                                                                      reader&) noexcept;
inline result<ref<void>, serialization::diagnostic> decode_object(type_id id,
                                                                  reader& input) noexcept {
  context ctx;
  return decode_object(ctx, id, input);
}

template <class T>
result<void, diagnostic> write(const context& ctx, const T& value, writer& output) noexcept try {
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  auto view = meta::view(*registry, std::as_const(value));
  if (!view)
    return diagnostic{.error = view.error()};
  return write(ctx, *view, output);
} catch (const std::bad_alloc&) {
  return diagnostic{.error = error::out_of_memory};
} catch (...) {
  return diagnostic{.error = error::internal};
}
template <class T> result<void, diagnostic> write(const T& value, writer& output) noexcept {
  context ctx;
  return write(ctx, value, output);
}
template <class T>
result<T, serialization::diagnostic> decode(const context& ctx, reader& input) noexcept try {
  return detail::decode_typed<T>(
      ctx, [&](const meta::instance& view) { return read_into(ctx, input, view); },
      [&](type_id id) { return decode_object(ctx, id, input); });
} catch (const std::bad_alloc&) {
  return serialization::diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization::diagnostic{.error = error::internal};
}
template <class T> result<T, serialization::diagnostic> decode(reader& input) noexcept {
  context ctx;
  return decode<T>(ctx, input);
}
}
