// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/prefab_property_override.hpp"
namespace gneiss::engine {
enum class prefab_override_section { component, property };
/** 索引对应原始覆盖列表；失败记录不从原列表删除。 */
struct prefab_override_conflict {
  prefab_override_section section;
  std::size_t index{};
  scene_diagnostic diagnostic;
};
struct prefab_replay_result {
  prefab_description description;
  std::vector<prefab_override_conflict> conflicts;
};
/** 在新来源上回放；独立覆盖继续执行，组件跨字段校验失败仅拒绝该组件的候选。
 * 来源和覆盖列表保持不变，系统错误返回失败，不伪装成可恢复冲突。 */
GNEISS_API result<prefab_replay_result>
replay_prefab_reference(const prefab_description&, const prefab_reference_description&) noexcept;
struct prefab_edit_candidate {
  prefab_reference_description reference;
  std::vector<prefab_override_conflict> conflicts;
};
/** 计算来源组件或已有本地组件的持久差异，不访问 World、资源服务或历史。
 * current 用于确定本次触及路径；未触及的冲突保留，恢复来源值移除对应差异。
 * 本地新增组件更新其整项描述；不新增、删除或恢复组件，输入不变，失败不返回部分候选。
 */
GNEISS_API result<prefab_edit_candidate>
prepare_prefab_component_edit(const prefab_description&, const prefab_reference_description&,
                              guid source_node, const component_description& current,
                              const component_description& replacement) noexcept;
/** 来源节点变换的编辑候选；反射生成差异，沿用组件编辑的冲突保留规则。
 * 实例根自身的局部变换由场景对象管理，不属于该来源覆盖入口。
 */
GNEISS_API result<prefab_edit_candidate>
prepare_prefab_transform_edit(const prefab_description&, const prefab_reference_description&,
                              guid source_node, const math::transform& current,
                              const math::transform& replacement) noexcept;
}
