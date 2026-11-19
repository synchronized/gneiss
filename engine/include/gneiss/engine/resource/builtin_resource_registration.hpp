// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/resource/export.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/base/result.hpp>
namespace gneiss {
/** 内置资源注册的场景子步骤；由 register_builtin_resource_types 按依赖顺序调用。 */
[[nodiscard]] error register_component_descriptions();
/** 在未冻结的默认 Registry 按依赖顺序登记内置资源类型与 codec。
 * 冻结前须由宿主先注册 World 基础值；initialize_meta 已按此顺序装配。
 * 首个失败立即返回，不回滚已注册类型；重复调用或冻结后调用失败。不创建运行资源。 */
[[nodiscard]] GNEISS_RESOURCE_API error register_builtin_resource_types();
}
