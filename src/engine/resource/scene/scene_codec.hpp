// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include "engine/resource/scene/scene_description.hpp"
namespace gneiss::engine {
[[nodiscard]] error register_component_descriptions() noexcept;
}
