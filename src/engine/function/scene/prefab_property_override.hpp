// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_HPP_
#define GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_HPP_

#include "engine/core/meta/registry.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include "engine/resource/scene/prefab_property_data.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace gneiss::engine {

/** 只修改私有描述，失败保持 output 不变；不读文件、不创建实体、不发送通知。 */
[[nodiscard]] error prepare_prefab_description(const prefab_description& source,
                                               std::span<const prefab_property_override> overrides,
                                               prefab_description& output) noexcept;

/** 应用结构与属性覆盖；诊断按来源 UUID、类型 GUID 和属性路径定位。 */
[[nodiscard]] GNEISS_API error
prepare_prefab_reference(const prefab_description&, const prefab_reference_description&,
                         prefab_description&, scene_diagnostic* diagnostic = nullptr) noexcept;

/** 验证作者键、值、字段存在性、类型和可写能力。Registry 必须已经冻结。 */
[[nodiscard]] error
validate_prefab_property_override(const prefab_property_override& value) noexcept;

/**
 * 写入或移除一条稀疏覆盖。
 *
 * candidate 与 source_value 规范化后相等时删除对应记录，否则按稳定键更新并确定性排序。
 */
[[nodiscard]] GNEISS_API error set_prefab_property_override(
    std::vector<prefab_property_override>& overrides, prefab_property_override candidate,
    engine::value source_value) noexcept;

/** 计算网格组件的 URI 覆盖；保留其他对象/属性记录。失败不改变输入，不访问 World 或资源服务。 */
[[nodiscard]] GNEISS_API result<std::vector<prefab_property_override>>
prepare_mesh_renderer_overrides(guid source_node, const mesh_renderer_description& original,
                                const mesh_renderer_description& replacement,
                                std::span<const prefab_property_override> existing = {}) noexcept;

} // namespace gneiss::engine

#endif
