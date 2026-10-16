// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_WORLD_STATE_HPP_
#define GNEISS_WORLD_WORLD_STATE_HPP_

#include <gneiss/engine/core/entity.h>
#include <gneiss/engine/render.h>

#include "engine/function/scene/scene_tree.hpp"

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace gneiss::world_internal {

struct camera_component {
  gneiss_camera value;
};

struct mesh_renderer_component {
  gneiss_mesh_renderer value;
};

class world_state final {
public:
  using publication_group = core::publication_group;
  using publication = core::publication;

  /** 所属线程创建候选组；令牌只供内部构造使用，不得跨 World。 */
  [[nodiscard]] publication begin_publication() const {
    return is_owner_thread() ? publication{new publication_group{domain_}} : publication{};
  }
  [[nodiscard]] gneiss_entity_id create_staged_entity(const publication& group) {
    if (!valid_group(group) || group->published_ || group->retired_) {
      return GNEISS_NULL_ENTITY_ID;
    }
    const auto entity = registry_.create();
    try {
      registry_.emplace<publication_member>(entity, group);
    } catch (...) {
      registry_.destroy(entity);
      throw;
    }
    ++group->count_;
    return encode(entity);
  }
  template <typename Component, typename... Args>
  [[nodiscard]] bool emplace_staged(const publication& group, gneiss_entity_id entity,
                                    Args&&... args) {
    if (!staged_member(group, entity) || group->retired_) {
      return false;
    }
    registry_.emplace_or_replace<Component>(decode(entity), std::forward<Args>(args)...);
    return true;
  }
  [[nodiscard]] bool destroy_staged_entity(const publication& group, gneiss_entity_id entity) {
    if (!staged_member(group, entity)) {
      return false;
    }
    scene_.detach_entity(entity);
    registry_.destroy(decode(entity));
    --group->count_;
    return true;
  }
  /** 全部可失败准备完成后调用；不分配，不遍历成员。 */
  [[nodiscard]] gneiss_result publish(const publication& group) noexcept {
    if (!valid_group(group) || group->published_ || group->retired_) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    entity_count_ += group->count_;
    group->published_ = true;
    return GNEISS_SUCCESS;
  }
  /** 提交删除后整组不可见；仅可逐步释放，不可再次发布或构造。 */
  [[nodiscard]] gneiss_result retire(const publication& group) noexcept {
    if (!valid_group(group) || !group->published_ || group->retired_ ||
        !scene_.publication_closed(group)) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    if (is_alive(active_camera_)) {
      const auto* member = registry_.try_get<publication_member>(decode(active_camera_));
      if (member && member->group == group) {
        active_camera_ = GNEISS_NULL_ENTITY_ID;
      }
    }
    entity_count_ -= group->count_;
    group->published_ = false;
    group->retired_ = true;
    return GNEISS_SUCCESS;
  }
  /** 结构重载中新创建的可见对象加入原发布组；保持可见，不改变实体总数。 */
  [[nodiscard]] gneiss_result bind_publication(const publication& group, gneiss_entity_id entity,
                                               gneiss_scene_node_id node) {
    if (!valid_group(group) || !group->published_ || group->retired_ || !is_alive(entity) ||
        scene_.get_entity(node) != entity) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    if (const auto* member = registry_.try_get<publication_member>(decode(entity))) {
      return member->group == group ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE;
    }
    registry_.emplace<publication_member>(decode(entity), group);
    const auto result = scene_.bind_publication(node, group);
    if (result != GNEISS_SUCCESS) {
      registry_.remove<publication_member>(decode(entity));
      return result;
    }
    ++group->count_;
    return GNEISS_SUCCESS;
  }
  explicit world_state(std::uint32_t domain) noexcept
      : scene_(domain), domain_(domain), owner_thread_(std::this_thread::get_id()) {}

  world_state(const world_state&) = delete;
  world_state& operator=(const world_state&) = delete;
  world_state(world_state&&) = delete;
  world_state& operator=(world_state&&) = delete;

  [[nodiscard]] bool is_owner_thread() const noexcept {
    return owner_thread_ == std::this_thread::get_id();
  }

  [[nodiscard]] gneiss_entity_id create_entity() {
    const auto entity = registry_.create();
    ++entity_count_;
    return encode(entity);
  }

  [[nodiscard]] bool destroy_entity(gneiss_entity_id entity) {
    if (!is_alive(entity)) {
      return false;
    }
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return false;
    }
    scene_.detach_entity(entity);
    if (const auto* member = registry_.try_get<publication_member>(native); member != nullptr) {
      --member->group->count_;
    }
    registry_.destroy(native);
    if (active_camera_ == entity) {
      active_camera_ = GNEISS_NULL_ENTITY_ID;
    }
    --entity_count_;
    return true;
  }

  [[nodiscard]] bool is_alive(gneiss_entity_id entity) const noexcept {
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return false;
    }
    const auto* member = registry_.try_get<publication_member>(native);
    return member == nullptr || member->group->published_;
  }

  [[nodiscard]] std::size_t entity_count() const noexcept { return entity_count_; }

  template <typename Component, typename... Args>
  Component& emplace(gneiss_entity_id entity, Args&&... args) {
    if (!is_alive(entity)) {
      throw std::invalid_argument{"实体未发布或已失效"};
    }
    return registry_.emplace<Component>(decode(entity), std::forward<Args>(args)...);
  }

  template <typename Component, typename... Args>
  Component& emplace_or_replace(gneiss_entity_id entity, Args&&... args) {
    if (!is_alive(entity)) {
      throw std::invalid_argument{"实体未发布或已失效"};
    }
    return registry_.emplace_or_replace<Component>(decode(entity), std::forward<Args>(args)...);
  }

  template <typename Component> [[nodiscard]] Component* get(gneiss_entity_id entity) noexcept {
    const auto native = decode(entity);
    return !is_alive(entity) ? nullptr : registry_.try_get<Component>(native);
  }

  [[nodiscard]] gneiss_result set_active_camera(gneiss_entity_id entity) noexcept {
    if (!is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    if (get<camera_component>(entity) == nullptr) {
      return GNEISS_ERROR_NOT_READY;
    }
    each<camera_component>(
        [](camera_component& component) { component.value.is_primary = UINT8_C(0); });
    get<camera_component>(entity)->value.is_primary = UINT8_C(1);
    active_camera_ = entity;
    return GNEISS_SUCCESS;
  }

  [[nodiscard]] gneiss_result clear_camera(gneiss_entity_id entity) noexcept {
    if (!is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    if (registry_.remove<camera_component>(native) == 0U) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (active_camera_ == entity) {
      active_camera_ = GNEISS_NULL_ENTITY_ID;
    }
    return GNEISS_SUCCESS;
  }

  [[nodiscard]] gneiss_result clear_mesh_renderer(gneiss_entity_id entity) noexcept {
    if (!is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    return registry_.remove<mesh_renderer_component>(native) == 0U ? GNEISS_ERROR_NOT_FOUND
                                                                   : GNEISS_SUCCESS;
  }

  void clear_active_camera(gneiss_entity_id entity) noexcept {
    if (active_camera_ != entity) {
      return;
    }
    if (auto* component = get<camera_component>(entity); component != nullptr) {
      component->value.is_primary = UINT8_C(0);
    }
    active_camera_ = GNEISS_NULL_ENTITY_ID;
  }

  [[nodiscard]] gneiss_entity_id active_camera() const noexcept { return active_camera_; }

  template <typename... Component, typename Function> void each(Function&& function) {
    registry_.view<Component...>().each([&](entt::entity entity, auto&... components) {
      if (!is_alive(encode(entity))) {
        return;
      }
      if constexpr (std::is_invocable_v<Function&, entt::entity, decltype(components)...>) {
        std::invoke(function, entity, components...);
      } else {
        std::invoke(function, components...);
      }
    });
  }

  [[nodiscard]] scene_internal::scene_tree& scene() noexcept { return scene_; }
  [[nodiscard]] const scene_internal::scene_tree& scene() const noexcept { return scene_; }
  [[nodiscard]] gneiss_entity_id encode_entity(entt::entity entity) const noexcept {
    return encode(entity);
  }

private:
  struct publication_member {
    publication group;
  };
  [[nodiscard]] bool valid_group(const publication& group) const noexcept {
    return is_owner_thread() && group && group->domain_ == domain_;
  }
  [[nodiscard]] bool staged_member(const publication& group,
                                   gneiss_entity_id entity) const noexcept {
    if (!valid_group(group) || group->published_) {
      return false;
    }
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return false;
    }
    const auto* member = registry_.try_get<publication_member>(native);
    return member != nullptr && member->group == group;
  }
  [[nodiscard]] gneiss_entity_id encode(entt::entity entity) const noexcept {
    const auto native = static_cast<std::uint32_t>(entt::to_integral(entity));
    return (static_cast<gneiss_entity_id>(domain_) << 32U) |
           (static_cast<gneiss_entity_id>(native) + 1U);
  }

  [[nodiscard]] entt::entity decode(gneiss_entity_id entity) const noexcept {
    if (entity == GNEISS_NULL_ENTITY_ID || static_cast<std::uint32_t>(entity >> 32U) != domain_) {
      return entt::null;
    }
    const auto encoded_native = static_cast<std::uint32_t>(entity);
    if (encoded_native == 0U) {
      return entt::null;
    }
    return static_cast<entt::entity>(encoded_native - 1U);
  }

  entt::registry registry_;
  scene_internal::scene_tree scene_;
  std::uint32_t domain_;
  std::thread::id owner_thread_;
  std::size_t entity_count_ = 0;
  gneiss_entity_id active_camera_ = GNEISS_NULL_ENTITY_ID;
};

} // namespace gneiss::world_internal

#endif
