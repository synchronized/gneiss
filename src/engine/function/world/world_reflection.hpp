// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include "engine/core/math/camera.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"

namespace gneiss::engine {
[[nodiscard]] GNEISS_API error register_reflection();
[[nodiscard]] GNEISS_API guid transform_property_type() noexcept;
} // namespace gneiss::engine
