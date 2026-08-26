// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_WORLD_STATE_H_
#define GNEISS_WORLD_WORLD_STATE_H_

#include <gneiss/core/entity.h>
#include <gneiss/render.h>

#include "scene/scene_tree.h"

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <thread>
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
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return false;
    }
    scene_.detach_entity(entity);
    registry_.destroy(native);
    --entity_count_;
    return true;
  }

  [[nodiscard]] bool is_alive(gneiss_entity_id entity) const noexcept {
    const auto native = decode(entity);
    return native != entt::null && registry_.valid(native);
  }

  [[nodiscard]] std::size_t entity_count() const noexcept { return entity_count_; }

  template <typename Component, typename... Args>
  Component& emplace(gneiss_entity_id entity, Args&&... args) {
    return registry_.emplace<Component>(decode(entity), std::forward<Args>(args)...);
  }

  template <typename Component, typename... Args>
  Component& emplace_or_replace(gneiss_entity_id entity, Args&&... args) {
    return registry_.emplace_or_replace<Component>(decode(entity), std::forward<Args>(args)...);
  }

  template <typename Component> [[nodiscard]] Component* get(gneiss_entity_id entity) noexcept {
    const auto native = decode(entity);
    return native == entt::null ? nullptr : registry_.try_get<Component>(native);
  }

  template <typename... Component, typename Function> void each(Function&& function) {
    registry_.view<Component...>().each(std::forward<Function>(function));
  }

  [[nodiscard]] scene_internal::scene_tree& scene() noexcept { return scene_; }
  [[nodiscard]] const scene_internal::scene_tree& scene() const noexcept { return scene_; }
  [[nodiscard]] gneiss_entity_id encode_entity(entt::entity entity) const noexcept {
    return encode(entity);
  }

private:
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
};

} // namespace gneiss::world_internal

#endif
