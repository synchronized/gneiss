// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/serialization/context.hpp>
#include <gneiss/engine/core/serialization/detail/typed_decode.hpp>
#include <gneiss/engine/core/serialization/reader.hpp>
#include <gneiss/engine/core/serialization/writer.hpp>

namespace gneiss::serialization {
/** context 与视图必须绑定同一注册表；后端和对象均借用。失败时 writer 可能保留未完成输出。 */
GNEISS_API result<void, diagnostic> write(const write_context&, const meta::const_instance&,
                                          writer&);
/** 写入单个子节点，不设置文档根；节点归 writer 所有。可用于自定义 codec。 */
GNEISS_API result<writer::node, diagnostic> write_node(const write_context&,
                                                       const meta::const_instance&, writer&);
/** 从指定子节点写入目标，沿用当前路径、深度和适配器。 */
GNEISS_API result<void, diagnostic> read_into(const read_context&, reader&, reader::node,
                                              const meta::instance&);
GNEISS_API result<ref<void>, diagnostic> decode_object(const read_context&, type_id, reader&,
                                                       reader::node);

/** 写入调用方目标；失败不保证恢复。需要原子替换时使用返回独立对象的模板入口。 */
GNEISS_API result<void, diagnostic> read_into(const read_context&, reader&, const meta::instance&);
GNEISS_API result<void, diagnostic> write(const meta::const_instance&, writer&);
GNEISS_API result<void, diagnostic> read_into(reader&, const meta::instance&);
GNEISS_API result<ref<void>, serialization::diagnostic> decode_object(const read_context&, type_id,
                                                                      reader&);
inline result<ref<void>, serialization::diagnostic> decode_object(type_id id, reader& input) {
  read_context ctx;
  return decode_object(ctx, id, input);
}

template <class T>
result<writer::node, diagnostic> write_node(const write_context& ctx, const T& value,
                                            writer& output) {
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  auto view = meta::view(*registry, std::as_const(value));
  return write_node(ctx, view, output);
}

template <class T>
result<T, diagnostic> decode(const read_context& ctx, reader& input, reader::node node) {
  return detail::decode_typed<T>(
      ctx, [&](const meta::instance& view) { return read_into(ctx, input, node, view); },
      [&](type_id id) { return decode_object(ctx, id, input, node); });
}

template <class T>
result<void, diagnostic> write(const write_context& ctx, const T& value, writer& output) {
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  auto view = meta::view(*registry, std::as_const(value));
  return write(ctx, view, output);
}
template <class T> result<void, diagnostic> write(const T& value, writer& output) {
  write_context ctx;
  return write(ctx, value, output);
}
template <class T>
result<T, serialization::diagnostic> decode(const read_context& ctx, reader& input) {
  return detail::decode_typed<T>(
      ctx, [&](const meta::instance& view) { return read_into(ctx, input, view); },
      [&](type_id id) { return decode_object(ctx, id, input); });
}
template <class T> result<T, serialization::diagnostic> decode(reader& input) {
  read_context ctx;
  return decode<T>(ctx, input);
}
}
