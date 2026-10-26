// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization.hpp"
namespace gneiss::engine {
/** 共享的只读内置资源 Registry，登记材质、输入映射、Scene 与 Prefab codec；失败保持输出。 */
[[nodiscard]] GNEISS_API result builtin_resource_registry(registry_ref& output) noexcept;
}
