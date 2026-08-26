// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_TREE_H_
#define GNEISS_SCENE_SCENE_TREE_H_

#include <gneiss/scene.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::scene_internal {

class scene_tree final {
public:
  explicit scene_tree(std::uint32_t domain) noexcept : domain_(domain) {}

  [[nodiscard]] gneiss_result create(gneiss_scene_node_id parent, gneiss_entity_id entity,
                                     gneiss_scene_node_id* out_node) noexcept;
  [[nodiscard]] gneiss_result destroy(gneiss_scene_node_id node) noexcept;
  [[nodiscard]] gneiss_result reparent(gneiss_scene_node_id node,
                                       gneiss_scene_node_id parent) noexcept;
  [[nodiscard]] gneiss_result set_local(gneiss_scene_node_id node,
                                        const gneiss_transform& transform) noexcept;
  [[nodiscard]] const gneiss_transform* get_local(gneiss_scene_node_id node) const noexcept;
  [[nodiscard]] gneiss_result get_world(gneiss_scene_node_id node,
                                        gneiss_transform* out_transform) const noexcept;
  [[nodiscard]] gneiss_entity_id get_entity(gneiss_scene_node_id node) const noexcept;
  void detach_entity(gneiss_entity_id entity) noexcept;

private:
  struct node {
    gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
    std::vector<gneiss_scene_node_id> children;
    gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
    gneiss_transform local = GNEISS_TRANSFORM_IDENTITY;
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
  [[nodiscard]] bool is_descendant(gneiss_scene_node_id ancestor,
                                   gneiss_scene_node_id candidate) const noexcept;
  void remove_child(gneiss_scene_node_id parent, gneiss_scene_node_id child) noexcept;
  void destroy_subtree(gneiss_scene_node_id id) noexcept;

  std::uint32_t domain_;
  std::vector<slot> slots_;
};

} // namespace gneiss::scene_internal

#endif
