// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/serialization/serialization.hpp>
#include "core/serialization/detail/traversal.hpp"
namespace gneiss::serialization {
result<void, diagnostic> write(const write_context& ctx, const meta::const_instance& view,
                               writer& output) {
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  if (*registry != view.registry())
    return diagnostic{.error = error::invalid_argument};
  detail::operation_scope operation{ctx};
  auto encoded =
      detail::encode_view(ctx, *registry, view, &output, detail::context_access::depth(ctx));
  if (!encoded)
    return std::move(encoded).error();
  if (!output.root(*encoded))
    return diagnostic{.error = error::out_of_memory};
  return {};
}
result<void, diagnostic> read_into(const read_context& ctx, reader& input,
                                   const meta::instance& view) {
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  detail::operation_scope operation{ctx};
  detail::context_access::input(ctx) = &input;
  if (*registry != view.registry())
    return diagnostic{.error = error::invalid_argument};
  auto status =
      detail::decode_view(ctx, *registry, view, input.root(), detail::context_access::depth(ctx));
  if (status.error.failed())
    return status;
  return {};
}
result<ref<void>, serialization::diagnostic> decode_object(const read_context& ctx, type_id id,
                                                           reader& input) {
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  detail::operation_scope operation{ctx};
  detail::context_access::input(ctx) = &input;
  return detail::decode_owned(ctx, *registry, id, input.root(), detail::context_access::depth(ctx),
                              true);
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
