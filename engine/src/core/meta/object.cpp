// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/meta/object.hpp>
#include <gneiss/engine/core/meta/registry.hpp>

namespace gneiss::meta {
meta::type object::dynamic_type(const registry_ref& registry) const {
  const auto* ops = dynamic_ops();
  return ops ? detail::resolve_type(registry, *ops) : meta::type{};
}
meta::type object::dynamic_type() const { return dynamic_type(default_registry()); }
}
