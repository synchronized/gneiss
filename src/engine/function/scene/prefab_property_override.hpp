// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_HPP_
#define GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_HPP_

#include "engine/core/error.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/resource/scene/prefab_property_data.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace gneiss::engine {

/** 验证作者键、值、字段存在性、类型和可写能力。Registry 必须已经冻结。 */
[[nodiscard]] error
validate_prefab_property_override(const prefab_property_override& value) noexcept;

/**
 * 写入或移除一条稀疏覆盖。
 *
 * candidate 与 source_value 规范化后相等时删除对应记录，否则按稳定键更新并确定性排序。
 */
[[nodiscard]] error set_prefab_property_override(std::vector<prefab_property_override>& overrides,
                                                 prefab_property_override candidate,
                                                 engine::value source_value) noexcept;

} // namespace gneiss::engine

#endif
