// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/serialization/serialization.hpp"
#include "engine/core/meta/detail/property_view_access.hpp"
#include "engine/core/serialization/detail/traversal.hpp"
namespace gneiss::engine::serialization {
meta::serialization_diagnostic serialize(const meta::instance& view, writer& output) noexcept try {
  detail::traversal_context context;
  auto encoded = detail::encode_view(context, meta::detail::property_view_access::registry(view),
                                     view, &output, 0);
  if (!encoded)
    return std::move(encoded).error();
  return {.error = output.root(*encoded) ? error::success : error::out_of_memory};
} catch (const std::bad_alloc&) {
  return {.error = error::out_of_memory};
} catch (...) {
  return {.error = error::internal};
}
meta::serialization_diagnostic deserialize(reader& input,
                                           const meta::mutable_instance& view) noexcept try {
  detail::traversal_context context{.input = &input};
  return detail::decode_view(context, meta::detail::property_view_access::registry(view), view,
                             input.root(), 0);
} catch (const std::bad_alloc&) {
  return {.error = error::out_of_memory};
} catch (...) {
  return {.error = error::internal};
}
result<ref<void>, meta::serialization_diagnostic>
deserialize_object(const meta::registry_ref& registry, type_id id, reader& input) noexcept try {
  detail::traversal_context context{.input = &input};
  return detail::decode_owned(context, registry, id, input.root(), 0, true);
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
}
