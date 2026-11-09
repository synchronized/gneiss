// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_query.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/world/world.hpp"
#include <vector>

namespace gneiss::engine {
class scene_instance_state final {
public:
  scene_instance_state(world& native_world) noexcept;
  ~scene_instance_state() noexcept;

  scene_instance_state(const scene_instance_state&) = delete;
  scene_instance_state& operator=(const scene_instance_state&) = delete;

  void rollback() noexcept;
  /** 实例槽位创建后绑定身份；成员归属保存在 World 的 ECS 中。 */
  void bind(scene_instance_id id) noexcept { id_ = id; }
  [[nodiscard]] scene_node_id find_node(guid uuid) const;
  [[nodiscard]] result<scene_node_info> get_node_info(std::uint64_t index,
                                                      node_query_detail detail) const;
  [[nodiscard]] result<scene_node_id> create_node(const node_creation& desc);
  [[nodiscard]] error reparent_node(scene_node_id node, scene_node_id parent);
  [[nodiscard]] error destroy_subtree(scene_node_id root);
  [[nodiscard]] error destroy_node(scene_node_id node);
  [[nodiscard]] bool contains_editable_node(scene_node_id node) const noexcept;
  [[nodiscard]] std::uint64_t node_count() const noexcept;

private:
  friend class scene_instances;
  [[nodiscard]] world_state& storage() const noexcept;
  [[nodiscard]] std::vector<scene_node_id> members() const;
  void adopt(scene_node_id node);
  scene_instance_id id_;

  world& native_world_;
};
}
