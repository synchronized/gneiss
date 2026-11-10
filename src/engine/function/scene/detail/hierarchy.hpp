// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_WORLD_HIERARCHY_HPP_
#define GNEISS_WORLD_WORLD_HIERARCHY_HPP_

#include "engine/core/math/transform.hpp"
#include "engine/core/result.hpp"
#include "engine/function/scene/detail/object_state.hpp"
#include "engine/function/scene/scene_types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

class world_state;

/** 借用 World 的实体存储，不分配独立节点身份。 */
class world_hierarchy final {
public:
  explicit world_hierarchy(world_state& owner) noexcept : owner_(owner) {}

  [[nodiscard]] result<entity_id> create(entity_id parent, std::uint64_t entity);
  [[nodiscard]] error destroy(entity_id node);
  [[nodiscard]] error reparent(entity_id node, entity_id parent);
  [[nodiscard]] error set_local(entity_id node, const math::transform& transform) noexcept;
  [[nodiscard]] const math::transform* get_local(entity_id node) const noexcept;
  [[nodiscard]] result<math::transform> get_world(entity_id node) const noexcept;
  [[nodiscard]] result<math::transform> get_world_for_entity(std::uint64_t entity) const noexcept;
  [[nodiscard]] const math::transform* get_local_for_entity(std::uint64_t entity) const noexcept;
  [[nodiscard]] error set_local_for_entity(std::uint64_t entity,
                                           const math::transform& transform) noexcept;
  [[nodiscard]] std::uint64_t get_entity(entity_id node) const noexcept;
  [[nodiscard]] entity_id get_parent(entity_id node) const noexcept;
  [[nodiscard]] error set_name(entity_id node, std::string name);
  [[nodiscard]] result<std::string_view> name(entity_id node) const;
  [[nodiscard]] const std::vector<entity_id>* children(entity_id node) const noexcept;

private:
  friend class world_state;
  [[nodiscard]] detail::object_state* find(entity_id id) noexcept;
  [[nodiscard]] const detail::object_state* find(entity_id id) const noexcept;
  void detach(entity_id id) noexcept;
  [[nodiscard]] bool is_descendant(entity_id ancestor, entity_id candidate) const noexcept;
  void remove_child(entity_id parent, entity_id child) noexcept;

  world_state& owner_;
};

} // namespace gneiss::engine

#endif
