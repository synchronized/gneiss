// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/serialization/serialization.hpp"
#include "engine/core/meta/detail/property_view_access.hpp"
#include "engine/core/serialization/detail/traversal.hpp"
namespace gneiss::engine::serialization {
serialization::diagnostic serialize(const context& ctx, const meta::const_instance& view,
                                    writer& output) noexcept try {
  auto registry = ctx.registry();
  if (!registry)
    return {.error = registry.error()};
  if (*registry != meta::detail::property_view_access::registry(view))
    return {.error = error::invalid_argument};
  detail::operation_scope operation{ctx};
  auto encoded =
      detail::encode_view(ctx, *registry, view, &output, detail::context_access::depth(ctx));
  if (!encoded)
    return std::move(encoded).error();
  return {.error = output.root(*encoded) ? error::success : error::out_of_memory};
} catch (const std::bad_alloc&) {
  return {.error = error::out_of_memory};
} catch (...) {
  return {.error = error::internal};
}
serialization::diagnostic deserialize(const context& ctx, reader& input,
                                      const meta::instance& view) noexcept try {
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  detail::operation_scope operation{ctx};
  detail::context_access::input(ctx) = &input;
  if (*registry != meta::detail::property_view_access::registry(view))
    return {.error = error::invalid_argument};
  return detail::decode_view(ctx, *registry, view, input.root(),
                             detail::context_access::depth(ctx));
} catch (const std::bad_alloc&) {
  return {.error = error::out_of_memory};
} catch (...) {
  return {.error = error::internal};
}
result<ref<void>, serialization::diagnostic> deserialize_object(const context& ctx, type_id id,
                                                                reader& input) noexcept try {
  auto registry = ctx.registry();
  if (!registry)
    return serialization::diagnostic{.error = registry.error()};
  detail::operation_scope operation{ctx};
  detail::context_access::input(ctx) = &input;
  return detail::decode_owned(ctx, *registry, id, input.root(), detail::context_access::depth(ctx),
                              true);
} catch (const std::bad_alloc&) {
  return serialization::diagnostic{.error = error::out_of_memory};
} catch (...) {
  return serialization::diagnostic{.error = error::internal};
}
serialization::diagnostic serialize(const meta::const_instance& view, writer& output) noexcept {
  context ctx{meta::detail::property_view_access::registry(view)};
  return serialize(ctx, view, output);
}
serialization::diagnostic deserialize(reader& input, const meta::instance& view) noexcept {
  context ctx{meta::detail::property_view_access::registry(view)};
  return deserialize(ctx, input, view);
}
}
