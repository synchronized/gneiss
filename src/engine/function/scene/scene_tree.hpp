// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_TREE_HPP_
#define GNEISS_SCENE_SCENE_TREE_HPP_

#include "engine/core/math/transform.hpp"
#include "engine/core/result.hpp"
#include "engine/core/scene_types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

class scene_tree final {
public:
  explicit scene_tree(std::uint32_t domain) noexcept : domain_(domain) {}

  [[nodiscard]] result<scene_node_id> create(scene_node_id parent, std::uint64_t entity) noexcept;
  [[nodiscard]] error destroy(scene_node_id node) noexcept;
  [[nodiscard]] error reparent(scene_node_id node, scene_node_id parent) noexcept;
  [[nodiscard]] error set_local(scene_node_id node, const math::transform& transform) noexcept;
  [[nodiscard]] const math::transform* get_local(scene_node_id node) const noexcept;
  [[nodiscard]] result<math::transform> get_world(scene_node_id node) const noexcept;
  [[nodiscard]] result<math::transform> get_world_for_entity(std::uint64_t entity) const noexcept;
  [[nodiscard]] result<scene_node_id> node_for_entity(std::uint64_t entity) const noexcept;
  [[nodiscard]] const math::transform* get_local_for_entity(std::uint64_t entity) const noexcept;
  [[nodiscard]] error set_local_for_entity(std::uint64_t entity,
                                           const math::transform& transform) noexcept;
  [[nodiscard]] std::uint64_t get_entity(scene_node_id node) const noexcept;
  [[nodiscard]] scene_node_id get_parent(scene_node_id node) const noexcept;
  [[nodiscard]] error set_identity(scene_node_id node, scene_node_identity identity) noexcept;
  [[nodiscard]] result<scene_node_identity_view> identity(scene_node_id node) const noexcept;
  [[nodiscard]] const std::vector<scene_node_id>* children(scene_node_id node) const noexcept;
  void detach_entity(std::uint64_t entity) noexcept;

private:
  struct node {
    scene_node_identity identity;
    scene_node_id parent = null_scene_node_id;
    std::vector<scene_node_id> children;
    std::uint64_t entity = UINT64_C(0);
    math::transform local = math::transform{};
  };
  struct slot {
    std::unique_ptr<node> value;
    std::uint16_t generation = 1U;
    bool retired = false;
  };

  [[nodiscard]] scene_node_id encode(std::uint16_t index, std::uint16_t generation) const noexcept;
  [[nodiscard]] slot* find(scene_node_id id) noexcept;
  [[nodiscard]] const slot* find(scene_node_id id) const noexcept;
  [[nodiscard]] bool is_descendant(scene_node_id ancestor, scene_node_id candidate) const noexcept;
  void remove_child(scene_node_id parent, scene_node_id child) noexcept;
  void destroy_subtree(scene_node_id id) noexcept;

  std::uint32_t domain_;
  std::vector<slot> slots_;
};

} // namespace gneiss::engine

#endif
