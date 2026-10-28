// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/texture/texture_resource.hpp"

namespace gneiss::engine {
texture_resource::texture_resource(rid value) noexcept : rid_(value) {}
texture_resource::~texture_resource() = default;
} // namespace gneiss::engine
