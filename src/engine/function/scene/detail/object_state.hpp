// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/math/transform.hpp"
#include "engine/function/scene/scene_types.hpp"
#include <string>
#include <vector>
namespace gneiss::detail {
/** World 私有受控对象数据；归属、层级和变换只能通过对象接口修改。 */
struct object_state {
  scene_instance_id scene;
  std::string name;
  entity_id parent;
  std::vector<entity_id> children;
  math::transform local{};
};
/** 无对象层级的辅助实体归属，由内部创建方登记。 */
struct auxiliary_scene_membership {
  scene_instance_id scene;
};
}
