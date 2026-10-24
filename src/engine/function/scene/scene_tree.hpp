// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_TREE_HPP_
#define GNEISS_SCENE_SCENE_TREE_HPP_

#include "engine/core/math/transform.hpp"
#include "engine/core/publication_group.hpp"
#include "engine/core/result.hpp"
#include <gneiss/engine/scene.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::engine {

class scene_tree final {
public:
  [[nodiscard]] bool publication_closed(const gneiss::engine::publication& group) const noexcept;
  /** 给已发布的新节点关联同样已发布的组；不改变可见性。 */
  [[nodiscard]] gneiss::engine::result
  bind_publication(gneiss_scene_node_id node, const gneiss::engine::publication& group) noexcept;
  explicit scene_tree(std::uint32_t domain) noexcept : domain_(domain) {}

  [[nodiscard]] gneiss::engine::result create(gneiss_scene_node_id parent, gneiss_entity_id entity,
                                              gneiss_scene_node_id* out_node) noexcept;
  [[nodiscard]] gneiss::engine::result destroy(gneiss_scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::result reparent(gneiss_scene_node_id node,
                                                gneiss_scene_node_id parent) noexcept;
  [[nodiscard]] gneiss::engine::result
  set_local(gneiss_scene_node_id node, const gneiss::engine::transform& transform) noexcept;
  [[nodiscard]] const gneiss::engine::transform*
  get_local(gneiss_scene_node_id node) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_world(gneiss_scene_node_id node, gneiss::engine::transform* out_transform) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_world_for_entity(gneiss_entity_id entity,
                       gneiss::engine::transform* out_transform) const noexcept;
  [[nodiscard]] const gneiss::engine::transform*
  get_local_for_entity(gneiss_entity_id entity) const noexcept;
  [[nodiscard]] gneiss::engine::result
  set_local_for_entity(gneiss_entity_id entity,
                       const gneiss::engine::transform& transform) noexcept;
  [[nodiscard]] gneiss_entity_id get_entity(gneiss_scene_node_id node) const noexcept;
  [[nodiscard]] gneiss_scene_node_id get_parent(gneiss_scene_node_id node) const noexcept;
  void detach_entity(gneiss_entity_id entity) noexcept;
  /** 候选节点只允许同组或已发布父节点；普通查询不可见。 */
  [[nodiscard]] gneiss::engine::result create_staged(const gneiss::engine::publication& group,
                                                     gneiss_scene_node_id parent,
                                                     gneiss_entity_id entity,
                                                     gneiss_scene_node_id* output) noexcept;
  [[nodiscard]] gneiss::engine::result
  set_staged_local(const gneiss::engine::publication& group, gneiss_scene_node_id id,
                   const gneiss::engine::transform& transform) noexcept;
  /** 只删除候选叶节点，调用方按拓扑逆序推进，避免递归清理。 */
  [[nodiscard]] gneiss::engine::result destroy_staged_leaf(const gneiss::engine::publication& group,
                                                           gneiss_scene_node_id id) noexcept;
  [[nodiscard]] gneiss_entity_id get_staged_entity(const gneiss::engine::publication& group,
                                                   gneiss_scene_node_id id) const noexcept;

private:
  struct node {
    gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
    std::vector<gneiss_scene_node_id> children;
    gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
    gneiss::engine::transform local = gneiss::engine::transform{};
    gneiss::engine::publication publication;
  };
  struct slot {
    std::unique_ptr<node> value;
    std::uint16_t generation = 1U;
    bool retired = false;
  };

  [[nodiscard]] gneiss_scene_node_id encode(std::uint16_t index,
                                            std::uint16_t generation) const noexcept;
  [[nodiscard]] slot* find(gneiss_scene_node_id id) noexcept;
  [[nodiscard]] const slot* find(gneiss_scene_node_id id) const noexcept;
  [[nodiscard]] slot* find_raw(gneiss_scene_node_id id) noexcept;
  [[nodiscard]] const slot* find_raw(gneiss_scene_node_id id) const noexcept;
  [[nodiscard]] bool has_unpublished_descendant(gneiss_scene_node_id id) const noexcept;
  [[nodiscard]] gneiss::engine::result create_impl(const gneiss::engine::publication& group,
                                                   gneiss_scene_node_id parent,
                                                   gneiss_entity_id entity,
                                                   gneiss_scene_node_id* out_node) noexcept;
  [[nodiscard]] bool is_descendant(gneiss_scene_node_id ancestor,
                                   gneiss_scene_node_id candidate) const noexcept;
  void remove_child(gneiss_scene_node_id parent, gneiss_scene_node_id child) noexcept;
  void destroy_subtree(gneiss_scene_node_id id) noexcept;

  std::uint32_t domain_;
  std::vector<slot> slots_;
};

} // namespace gneiss::engine

#endif
