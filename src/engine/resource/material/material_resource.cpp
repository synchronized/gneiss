// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/material/detail/material_resource_access.hpp"

#include <utility>

namespace gneiss::engine {
material_resource::material_resource(rid value, std::array<texture, 5> dependencies) noexcept
    : rid_(value), textures_(std::move(dependencies)) {}
material_resource::~material_resource() = default;
rid resource_handle(const material& value) noexcept {
  return value ? detail::material_resource_access::handle(*value) : null_rid;
}
} // namespace gneiss::engine
