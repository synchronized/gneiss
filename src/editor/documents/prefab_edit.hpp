// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/prefab_replay.hpp"
namespace gneiss::editor {
struct prefab_edit_candidate {
  gneiss::prefab_reference_description reference;
  std::vector<gneiss::prefab_override_conflict> conflicts;
  gneiss::prefab_description effective{};
};
/** 计算来源组件或已有本地组件的持久差异，不访问 World、资源服务或历史。
 * current 用于确定本次触及路径；未触及的冲突保留，恢复来源值移除对应差异。
 * 本地新增组件更新其整项描述；不新增、删除或恢复组件，输入不变，失败不返回部分候选。
 */
gneiss::result<prefab_edit_candidate>
prepare_prefab_component_edit(const gneiss::prefab_description&,
                              const gneiss::prefab_reference_description&, gneiss::guid source_node,
                              const gneiss::component_description& current,
                              const gneiss::component_description& replacement);
/** 替换一个组件的结构/属性覆盖；空 added 且 removed=false 表示恢复来源。
 * 不访问 World 或资源；只拒绝本次目标的冲突，保留其他目标的失效覆盖。
 */
gneiss::result<prefab_edit_candidate> prepare_prefab_component_override(
    const gneiss::prefab_description&, const gneiss::prefab_reference_description&,
    gneiss::guid source_node, gneiss::guid type, const gneiss::component_description& added,
    bool removed, std::span<const gneiss::prefab_property_override>,
    gneiss::guid component_uuid = {});
/** 来源节点变换的编辑候选；反射生成差异，沿用组件编辑的冲突保留规则。
 * 实例根自身的局部变换由场景对象管理，不属于该来源覆盖入口。
 */
gneiss::result<prefab_edit_candidate>
prepare_prefab_transform_edit(const gneiss::prefab_description&,
                              const gneiss::prefab_reference_description&, gneiss::guid source_node,
                              const gneiss::math::transform& current,
                              const gneiss::math::transform& replacement);
/** 计算网格组件的 URI 覆盖；保留其他对象/属性记录。失败不改变输入，不访问 World 或资源服务。 */
[[nodiscard]] gneiss::result<std::vector<gneiss::prefab_property_override>>
prepare_mesh_renderer_overrides(gneiss::guid source_node, gneiss::guid component_uuid,
                                const gneiss::mesh_renderer_description& original,
                                const gneiss::mesh_renderer_description& replacement,
                                std::span<const gneiss::prefab_property_override> existing = {});

}
