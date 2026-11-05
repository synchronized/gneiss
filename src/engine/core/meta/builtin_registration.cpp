// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
namespace gneiss::engine::meta {
error register_builtin_types(const registry_ref& registry) noexcept {
  return type_builder<object>{registry}.polymorphic().commit();
}
}
