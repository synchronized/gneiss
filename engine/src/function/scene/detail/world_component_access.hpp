// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "function/scene/detail/world_impl.hpp"
#include <gneiss/engine/function/scene/world.hpp>
namespace gneiss::detail {
/** 仅供组件适配实现访问现有 ECS；不得将后端类型传播到 Scene/资源接口。
 * 返回借用引用；World 必须存活，所属线程为断言前置条件。 */
struct GNEISS_API world_component_access {
  using storage = world::implementation;
  static storage& get(world& owner) noexcept;
};
}
