// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_WORLD_STATE_HPP_
#define GNEISS_WORLD_WORLD_STATE_HPP_

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world_components.hpp"

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
  explicit world_state(std::uint32_t domain) noexcept
      : scene_(domain), domain_(domain), owner_thread_(std::this_thread::get_id()) {}

  world_state(const world_state&) = delete;
  world_state& operator=(const world_state&) = delete;
  world_state(world_state&&) = delete;
  world_state& operator=(world_state&&) = delete;

  [[nodiscard]] bool is_owner_thread() const noexcept {
    return owner_thread_ == std::this_thread::get_id();
  }

  [[nodiscard]] std::uint64_t create_entity() {
    const auto entity = registry_.create();
    ++entity_count_;
    return encode(entity);
  }

  [[nodiscard]] bool destroy_entity(std::uint64_t entity) {
    if (!is_alive(entity)) {
      return false;
    }
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return false;
    }
    scene_.detach_entity(entity);

    registry_.destroy(native);
    if (active_camera_ == entity) {
      active_camera_ = UINT64_C(0);
    }
    --entity_count_;
    return true;
  }

  [[nodiscard]] bool is_alive(std::uint64_t entity) const noexcept {
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return false;
    }
    return true;
  }

  [[nodiscard]] std::size_t entity_count() const noexcept { return entity_count_; }

  template <typename Component, typename... Args>
  Component& emplace(std::uint64_t entity, Args&&... args) {
    if (!is_alive(entity)) {
      throw std::invalid_argument{"实体已失效"};
    }
    return registry_.emplace<Component>(decode(entity), std::forward<Args>(args)...);
  }

  template <typename Component, typename... Args>
  Component& emplace_or_replace(std::uint64_t entity, Args&&... args) {
    if (!is_alive(entity)) {
      throw std::invalid_argument{"实体已失效"};
    }
    return registry_.emplace_or_replace<Component>(decode(entity), std::forward<Args>(args)...);
  }

  template <typename Component> [[nodiscard]] Component* get(std::uint64_t entity) noexcept {
    const auto native = decode(entity);
    return !is_alive(entity) ? nullptr : registry_.try_get<Component>(native);
  }

  [[nodiscard]] result set_active_camera(std::uint64_t entity) noexcept {
    if (!is_alive(entity)) {
      return result::invalid_handle;
    }
    if (get<camera_component>(entity) == nullptr) {
      return result::not_ready;
    }
    active_camera_ = entity;
    return result::success;
  }

  [[nodiscard]] result clear_camera(std::uint64_t entity) noexcept {
    if (!is_alive(entity)) {
      return result::invalid_handle;
    }
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return result::invalid_handle;
    }
    if (registry_.remove<camera_component>(native) == 0U) {
      return result::not_found;
    }
    if (active_camera_ == entity) {
      active_camera_ = UINT64_C(0);
    }
    return result::success;
  }

  [[nodiscard]] result clear_mesh_renderer(std::uint64_t entity) noexcept {
    if (!is_alive(entity)) {
      return result::invalid_handle;
    }
    const auto native = decode(entity);
    if (native == entt::null || !registry_.valid(native)) {
      return result::invalid_handle;
    }
    return registry_.remove<mesh_renderer_component>(native) == 0U ? result::not_found
                                                                   : result::success;
  }

  void clear_active_camera(std::uint64_t entity) noexcept {
    if (active_camera_ != entity) {
      return;
    }
    active_camera_ = UINT64_C(0);
  }

  [[nodiscard]] std::uint64_t active_camera() const noexcept { return active_camera_; }

  template <typename... Component, typename Function> void each(Function&& function) {
    registry_.view<Component...>().each([&](entt::entity entity, auto&... components) {
      if constexpr (std::is_invocable_v<Function&, entt::entity, decltype(components)...>) {
        std::invoke(function, entity, components...);
      } else {
        std::invoke(function, components...);
      }
    });
  }

  [[nodiscard]] scene_tree& scene() noexcept { return scene_; }
  [[nodiscard]] const scene_tree& scene() const noexcept { return scene_; }
  [[nodiscard]] std::uint64_t encode_entity(entt::entity entity) const noexcept {
    return encode(entity);
  }

private:
  [[nodiscard]] std::uint64_t encode(entt::entity entity) const noexcept {
    const auto native = static_cast<std::uint32_t>(entt::to_integral(entity));
    return (static_cast<std::uint64_t>(domain_) << 32U) | (static_cast<std::uint64_t>(native) + 1U);
  }

  [[nodiscard]] entt::entity decode(std::uint64_t entity) const noexcept {
    if (entity == UINT64_C(0) || static_cast<std::uint32_t>(entity >> 32U) != domain_) {
      return entt::null;
    }
    const auto encoded_native = static_cast<std::uint32_t>(entity);
    if (encoded_native == 0U) {
      return entt::null;
    }
    return static_cast<entt::entity>(encoded_native - 1U);
  }

  entt::registry registry_;
  scene_tree scene_;
  std::uint32_t domain_;
  std::thread::id owner_thread_;
  std::size_t entity_count_ = 0;
  std::uint64_t active_camera_ = UINT64_C(0);
};

} // namespace gneiss::engine

#endif
