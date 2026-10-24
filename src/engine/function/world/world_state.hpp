// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_WORLD_STATE_HPP_
#define GNEISS_WORLD_WORLD_STATE_HPP_

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world_components.hpp"
#include <gneiss/engine/core/entity.h>

#include "engine/function/scene/scene_tree.hpp"

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace gneiss::engine {

class world_state final {
public:
  using publication_group = gneiss::engine::publication_group;
  using publication = gneiss::engine::publication;

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
  [[nodiscard]] gneiss::engine::result publish(const publication& group) noexcept {
    if (!valid_group(group) || group->published_ || group->retired_) {
      return gneiss::engine::result::invalid_state;
    }
    entity_count_ += group->count_;
    group->published_ = true;
    return gneiss::engine::result::success;
  }
  /** 提交删除后整组不可见；仅可逐步释放，不可再次发布或构造。 */
  [[nodiscard]] gneiss::engine::result retire(const publication& group) noexcept {
    if (!valid_group(group) || !group->published_ || group->retired_ ||
        !scene_.publication_closed(group)) {
      return gneiss::engine::result::invalid_state;
    }
    retire_validated(group);
    return gneiss::engine::result::success;
  }
  /** 整批校验后再切换可见性；所属线程执行，提交阶段不分配或调用外部回调。 */
  [[nodiscard]] gneiss::engine::result
  replace_publications(std::span<const publication> previous,
                       std::span<const publication> candidates) noexcept {
    if (previous.empty() || previous.size() != candidates.size()) {
      return gneiss::engine::result::invalid_argument;
    }
    for (std::size_t index = 0; index < previous.size(); ++index) {
      const auto& old = previous[index];
      const auto& next = candidates[index];
      if (!valid_group(old) || !old->published_ || old->retired_ || !valid_group(next) ||
          next->published_ || next->retired_ || !scene_.publication_closed(old)) {
        return gneiss::engine::result::invalid_state;
      }
      for (std::size_t other = 0; other < index; ++other) {
        if (previous[other] == old || candidates[other] == next) {
          return gneiss::engine::result::invalid_argument;
        }
      }
    }
    for (const auto& group : previous) {
      retire_validated(group);
    }
    for (const auto& group : candidates) {
      entity_count_ += group->count_;
      group->published_ = true;
    }
    return gneiss::engine::result::success;
  }
  /** 结构重载中新创建的可见对象加入原发布组；保持可见，不改变实体总数。 */
  [[nodiscard]] gneiss::engine::result
  bind_publication(const publication& group, gneiss_entity_id entity, gneiss::engine::rid node) {
    if (!valid_group(group) || !group->published_ || group->retired_ || !is_alive(entity) ||
        scene_.get_entity(node) != entity) {
      return gneiss::engine::result::invalid_state;
    }
    if (const auto* member = registry_.try_get<publication_member>(decode(entity))) {
      return member->group == group ? gneiss::engine::result::success
                                    : gneiss::engine::result::invalid_state;
    }
    registry_.emplace<publication_member>(decode(entity), group);
    const auto result = scene_.bind_publication(node, group);
    if (result != gneiss::engine::result::success) {
      registry_.remove<publication_member>(decode(entity));
      return result;
    }
    ++group->count_;
    return gneiss::engine::result::success;
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

  [[nodiscard]] gneiss::engine::result set_active_camera(gneiss_entity_id entity) noexcept {
    if (!is_alive(entity)) {
      return gneiss::engine::result::invalid_handle;
    }
    if (get<camera_component>(entity) == nullptr) {
      return gneiss::engine::result::not_ready;
    }
    active_camera_ = entity;
    return gneiss::engine::result::success;
  }

  [[nodiscard]] gneiss::engine::result clear_camera(gneiss_entity_id entity) noexcept {
    if (!is_alive(entity)) {
      return gneiss::engine::result::invalid_handle;
    }
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return gneiss::engine::result::invalid_handle;
    }
    if (registry_.remove<camera_component>(native) == 0U) {
      return gneiss::engine::result::not_found;
    }
    if (active_camera_ == entity) {
      active_camera_ = GNEISS_NULL_ENTITY_ID;
    }
    return gneiss::engine::result::success;
  }

  [[nodiscard]] gneiss::engine::result clear_mesh_renderer(gneiss_entity_id entity) noexcept {
    if (!is_alive(entity)) {
      return gneiss::engine::result::invalid_handle;
    }
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return gneiss::engine::result::invalid_handle;
    }
    return registry_.remove<mesh_renderer_component>(native) == 0U
               ? gneiss::engine::result::not_found
               : gneiss::engine::result::success;
  }

  void clear_active_camera(gneiss_entity_id entity) noexcept {
    if (active_camera_ != entity) {
      return;
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

  [[nodiscard]] gneiss::engine::scene_tree& scene() noexcept { return scene_; }
  [[nodiscard]] const gneiss::engine::scene_tree& scene() const noexcept { return scene_; }
  [[nodiscard]] gneiss_entity_id encode_entity(entt::entity entity) const noexcept {
    return encode(entity);
  }

private:
  void retire_validated(const publication& group) noexcept {
    if (is_alive(active_camera_)) {
      const auto* member = registry_.try_get<publication_member>(decode(active_camera_));
      if (member && member->group == group) {
        active_camera_ = GNEISS_NULL_ENTITY_ID;
      }
    }
    entity_count_ -= group->count_;
    group->published_ = false;
    group->retired_ = true;
  }

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
  gneiss::engine::scene_tree scene_;
  std::uint32_t domain_;
  std::thread::id owner_thread_;
  std::size_t entity_count_ = 0;
  gneiss_entity_id active_camera_ = GNEISS_NULL_ENTITY_ID;
};

} // namespace gneiss::engine

#endif
