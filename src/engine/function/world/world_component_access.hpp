// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_state.hpp"
namespace gneiss::engine::detail {
/** 仅供组件适配实现访问现有 ECS；不得将后端类型传播到 Scene/资源接口。
 * 返回借用指针，只能在 World 所属线程、World 存活期间使用。 */
struct GNEISS_API world_component_access {
  static result<world_state*> get(world& owner) noexcept;
};
}
