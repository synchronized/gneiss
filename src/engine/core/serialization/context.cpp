// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/serialization/context.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/serialization/detail/context_access.hpp"
namespace gneiss::engine::serialization {
std::string_view context::property_path() const noexcept {
  return state_ ? std::string_view{state_->path} : std::string_view{};
}
result<meta::registry_ref> context::registry() const noexcept {
  if (use_default_)
    return meta::default_registry();
  if (!registry_)
    return error::invalid_handle;
  return registry_;
}
}
