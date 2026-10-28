// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/texture/texture_resource.hpp"

namespace gneiss::engine {
texture_resource::texture_resource(render_resource_service& owner, rid value) noexcept
    : resources_(&owner), rid_(value) {}
texture_resource::~texture_resource() = default;
} // namespace gneiss::engine
