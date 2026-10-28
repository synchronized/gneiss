// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/mesh/mesh_resource.hpp"

namespace gneiss::engine {
mesh_resource::mesh_resource(rid value) noexcept : rid_(value) {}
mesh_resource::~mesh_resource() = default;
} // namespace gneiss::engine
