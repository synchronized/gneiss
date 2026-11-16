// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/scene/prefab_property_override.hpp>
namespace gneiss {
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
replay_prefab_reference(const prefab_description&, const prefab_reference_description&);
}
