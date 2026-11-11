// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/prefab_replay.hpp"
namespace gneiss::editor {
struct prefab_edit_candidate {
  engine::prefab_reference_description reference;
  std::vector<engine::prefab_override_conflict> conflicts;
  engine::prefab_description effective{};
};
/** 计算来源组件或已有本地组件的持久差异，不访问 World、资源服务或历史。
 * current 用于确定本次触及路径；未触及的冲突保留，恢复来源值移除对应差异。
 * 本地新增组件更新其整项描述；不新增、删除或恢复组件，输入不变，失败不返回部分候选。
 */
engine::result<prefab_edit_candidate>
prepare_prefab_component_edit(const engine::prefab_description&,
                              const engine::prefab_reference_description&, engine::guid source_node,
                              const engine::component_description& current,
                              const engine::component_description& replacement);
/** 替换一个组件的结构/属性覆盖；空 added 且 removed=false 表示恢复来源。
 * 不访问 World 或资源；只拒绝本次目标的冲突，保留其他目标的失效覆盖。
 */
engine::result<prefab_edit_candidate> prepare_prefab_component_override(
    const engine::prefab_description&, const engine::prefab_reference_description&,
    engine::guid source_node, engine::guid type, const engine::component_description& added,
    bool removed, std::span<const engine::prefab_property_override>,
    engine::guid component_uuid = {});
/** 来源节点变换的编辑候选；反射生成差异，沿用组件编辑的冲突保留规则。
 * 实例根自身的局部变换由场景对象管理，不属于该来源覆盖入口。
 */
engine::result<prefab_edit_candidate>
prepare_prefab_transform_edit(const engine::prefab_description&,
                              const engine::prefab_reference_description&, engine::guid source_node,
                              const engine::math::transform& current,
                              const engine::math::transform& replacement);
/** 计算网格组件的 URI 覆盖；保留其他对象/属性记录。失败不改变输入，不访问 World 或资源服务。 */
[[nodiscard]] engine::result<std::vector<engine::prefab_property_override>>
prepare_mesh_renderer_overrides(engine::guid source_node, engine::guid component_uuid,
                                const engine::mesh_renderer_description& original,
                                const engine::mesh_renderer_description& replacement,
                                std::span<const engine::prefab_property_override> existing = {});

}
