// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_HPP_
#define GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_HPP_

#include "engine/resource/prefab_property_data.hpp"
#include "engine/core/reflection/property_registry.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace gneiss::scene_internal {

/** 验证作者键、值、字段存在性、类型和可写能力。Registry 必须已经冻结。 */
[[nodiscard]] gneiss_result
validate_prefab_property_override(const reflection_internal::registry_ref& registry,
                                  const prefab_property_override& value) noexcept;

/**
 * 写入或移除一条稀疏覆盖。
 *
 * candidate 与 source_value 规范化后相等时删除对应记录，否则按稳定键更新并确定性排序。
 */
[[nodiscard]] gneiss_result
set_prefab_property_override(const reflection_internal::registry_ref& registry,
                             std::vector<prefab_property_override>& overrides,
                             prefab_property_override candidate,
                             prefab_property_value source_value) noexcept;

} // namespace gneiss::scene_internal

#endif
