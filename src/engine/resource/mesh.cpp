// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/mesh.hpp"
#include "engine/resource/mesh/detail/mesh_resource_access.hpp"

namespace gneiss::engine {
mesh_resource::mesh_resource(rid value) noexcept
    : asset(runtime_type_of<mesh_resource>()), rid_(value) {}
mesh_resource::~mesh_resource() = default;
rid resource_handle(const mesh& value) noexcept {
  return value ? detail::mesh_resource_access::handle(*value) : null_rid;
}
} // namespace gneiss::engine
