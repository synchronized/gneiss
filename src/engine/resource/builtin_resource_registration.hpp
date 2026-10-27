// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/registry.hpp"
namespace gneiss::engine {
/** 在未冻结的 Registry 登记对应资源身份与 codec；不创建运行时资源。 */
[[nodiscard]] GNEISS_API result
register_material_reflection(const gneiss::engine::meta::registry_ref&) noexcept;
[[nodiscard]] GNEISS_API result register_scene_reflection(const meta::registry_ref&) noexcept;
[[nodiscard]] GNEISS_API result register_prefab_reflection(const meta::registry_ref&) noexcept;
[[nodiscard]] GNEISS_API result
register_action_map_reflection(const gneiss::engine::meta::registry_ref&) noexcept;

/** 共享的只读内置资源 Registry，登记材质、输入映射、Scene 与 Prefab codec；失败保持输出。 */
[[nodiscard]] GNEISS_API result
builtin_resource_registry(gneiss::engine::meta::registry_ref& output) noexcept;
}
