// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/serialization/context.hpp"
#include "engine/core/meta/registry.hpp"
namespace gneiss::engine::serialization {
result<meta::registry_ref> context::registry() const noexcept {
  if (use_default_)
    return meta::default_registry();
  if (!registry_)
    return error::invalid_handle;
  return registry_;
}
}
