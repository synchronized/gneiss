// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/serialization/context.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include "core/serialization/detail/context_access.hpp"
namespace gneiss::serialization {
std::string_view detail::context_base::property_path() const noexcept {
  return state_ ? std::string_view{state_->path} : std::string_view{};
}
result<meta::registry_ref> detail::context_base::registry() const {
  if (use_default_)
    return meta::default_registry();
  if (!registry_)
    return error::invalid_handle;
  return registry_;
}
}
