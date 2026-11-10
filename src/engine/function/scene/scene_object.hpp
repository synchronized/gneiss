// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/scene/scene_description.hpp"

namespace gneiss::engine {
/** 仅用于创建过程；移交场景前的失败路径回收整棵子树。 */
class scene_subtree_guard final {
public:
  scene_subtree_guard(world& owner, scene_node_id root) noexcept : owner_(owner), root_(root) {}
  ~scene_subtree_guard() {
    if (root_)
      (void)owner_.find(gneiss::entity_id{(root_).get()}).destroy();
  }
  scene_subtree_guard(const scene_subtree_guard&) = delete;
  scene_subtree_guard& operator=(const scene_subtree_guard&) = delete;
  void release() noexcept { root_ = {}; }

private:
  world& owner_;
  scene_node_id root_;
};

/** 未注册适配器的描述不得被静默忽略。 */
inline error validate_scene_components(const game_object_description& source) noexcept {
  if (source.prefab && !source.components.empty())
    return error::invalid_argument;
  return validate_components(source.components);
}
/** 旧网格创建入口的固定资源适配；不作为通用组件上下文的一部分。 */
inline auto fixed_renderer_resources(mesh_renderer renderer) {
  return [renderer = std::move(renderer)](
             const render_resource_source& source) -> result<render_resource_ref> {
    if (source.type == render_resource_kind::mesh && resource_uri(renderer.mesh) == source.uri)
      return render_resource_ref{renderer.mesh};
    if (source.type == render_resource_kind::material &&
        resource_uri(renderer.material) == source.uri)
      return render_resource_ref{renderer.material};
    return error::not_ready;
  };
}
} // namespace gneiss::engine
