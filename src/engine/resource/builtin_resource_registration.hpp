// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/registry.hpp"
#include "engine/core/result.hpp"
namespace gneiss::engine {
/** 在未冻结的默认 Registry 登记对应资源身份与 codec；不创建运行时资源。 */
[[nodiscard]] GNEISS_API error register_material_reflection() noexcept;
[[nodiscard]] GNEISS_API error register_scene_reflection() noexcept;
[[nodiscard]] GNEISS_API error register_prefab_reflection() noexcept;
[[nodiscard]] GNEISS_API error register_action_map_reflection() noexcept;
}
