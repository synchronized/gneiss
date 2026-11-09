// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/object.hpp"
#include "engine/core/meta/registry.hpp"
namespace gneiss::engine::meta {
meta::type object::dynamic_type(const registry_ref& registry) const noexcept {
  const auto* ops = dynamic_ops();
  return ops ? detail::resolve_type(registry, *ops) : meta::type{};
}
meta::type object::dynamic_type() const noexcept {
  auto registry = default_registry();
  return dynamic_type(registry ? *registry : registry_ref{});
}
}
