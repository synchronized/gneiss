// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/serialization/serialization.hpp>
#include "core/serialization/detail/traversal.hpp"
namespace gneiss::serialization {
result<writer::node, diagnostic> write_node(const write_context& ctx,
                                            const meta::const_instance& view, writer& output) {
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  if (*registry != view.registry())
    return diagnostic{.error = error::invalid_argument};
  detail::operation_scope operation{ctx};
  if (detail::context_access::depth(ctx) > ctx.maximum_depth())
    return diagnostic{.error = error::invalid_argument};
  return detail::encode_view(ctx, *registry, view, &output, detail::context_access::depth(ctx));
}
result<void, diagnostic> write(const write_context& ctx, const meta::const_instance& view,
                               writer& output) {
  auto encoded = write_node(ctx, view, output);
  if (!encoded)
    return std::move(encoded).error();
  if (!output.root(*encoded))
    return diagnostic{.error = error::out_of_memory};
  return {};
}
result<void, diagnostic> read_into(const read_context& ctx, reader& input, reader::node node,
                                   const meta::instance& view) {
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  if (*registry != view.registry() || !node)
    return diagnostic{.error = error::invalid_argument};
  detail::operation_scope operation{ctx};
  detail::context_access::input(ctx) = &input;
  if (detail::context_access::depth(ctx) > ctx.maximum_depth())
    return diagnostic{.error = error::invalid_argument};
  auto status = detail::decode_view(ctx, *registry, view, node, detail::context_access::depth(ctx));
  if (status.error.failed())
    return status;
  return {};
}
result<void, diagnostic> read_into(const read_context& ctx, reader& input,
                                   const meta::instance& view) {
  return read_into(ctx, input, input.root(), view);
}
result<ref<void>, serialization::diagnostic> decode_object(const read_context& ctx, type_id id,
                                                           reader& input, reader::node node) {
  if (!node)
    return diagnostic{.error = error::invalid_argument};
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  detail::operation_scope operation{ctx};
  detail::context_access::input(ctx) = &input;
  return detail::decode_owned(ctx, *registry, id, node, detail::context_access::depth(ctx), true);
}
result<ref<void>, diagnostic> decode_object(const read_context& ctx, type_id id, reader& input) {
  return decode_object(ctx, id, input, input.root());
}
result<void, diagnostic> write(const meta::const_instance& view, writer& output) {
  write_context ctx{view.registry()};
  return write(ctx, view, output);
}
result<void, diagnostic> read_into(reader& input, const meta::instance& view) {
  read_context ctx{view.registry()};
  return read_into(ctx, input, view);
}
}
