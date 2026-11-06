// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/material.hpp"
#include "engine/resource/material/detail/material_resource_access.hpp"

#include <utility>

namespace gneiss::engine {
material::material(rid value, std::array<ref<const texture>, 5> dependencies) noexcept
    : asset(runtime_type_of<material>()), rid_(value), textures_(std::move(dependencies)) {}
material::~material() = default;
rid resource_handle(const ref<const material>& value) noexcept {
  return value ? detail::material_resource_access::handle(*value) : null_rid;
}
} // namespace gneiss::engine
