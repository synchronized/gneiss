// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_HPP_
#define GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_HPP_

#include <gneiss/engine/export.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/resource/scene/prefab_description.hpp>
#include <gneiss/engine/resource/scene/prefab_property_data.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace gneiss {

/** 只修改私有描述，失败保持 output 不变；不读文件、不创建实体、不发送通知。 */
[[nodiscard]] error prepare_prefab_description(const prefab_description& source,
                                               std::span<const prefab_property_override> overrides,
                                               prefab_description& output);

/** 应用结构与属性覆盖；诊断按来源 UUID、类型 GUID 和属性路径定位。 */
[[nodiscard]] GNEISS_ENGINE_API error prepare_prefab_reference(const prefab_description&,
                                                        const prefab_reference_description&,
                                                        prefab_description&,
                                                        scene_diagnostic* diagnostic = nullptr);

/** 验证作者键、值、字段存在性、类型和可写能力。Registry 必须已经冻结。 */
[[nodiscard]] error validate_prefab_property_override(const prefab_property_override& value);

/**
 * 写入或移除一条稀疏覆盖。
 *
 * candidate 与 source_value
 * 规范化后相等时删除对应记录，否则替换该路径及其子路径的旧记录，保留其他操作顺序。
 */
[[nodiscard]] GNEISS_ENGINE_API error
set_prefab_property_override(std::vector<prefab_property_override>& overrides,
                             prefab_property_override candidate, gneiss::value source_value);

/** 在临时目标上应用单条值树操作。 */
[[nodiscard]] error apply_prefab_override_operation(meta::instance target, const value_operation&);

} // namespace gneiss

#endif
