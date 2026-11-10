// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_WORLD_STATE_HPP_
#define GNEISS_WORLD_WORLD_STATE_HPP_

#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/rid_table.hpp"
#include "engine/function/scene/components.hpp"
#include <atomic>
#include <limits>

#include "engine/function/scene/detail/hierarchy.hpp"

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
  static std::uint16_t allocate_scene_domain() noexcept {
    static std::atomic_uint32_t next{1024U};
    const auto value = next.fetch_add(1U, std::memory_order_relaxed);
    return value <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(value)
                                                              : 0U;
  }
  const std::uint16_t scene_domain = allocate_scene_domain();
  rid_table<scene_instance_id, scene_instance_id> scene_slots{scene_domain};
  explicit world_state(std::uint32_t domain) noexcept
      : scene_(*this), domain_(domain), owner_thread_(std::this_thread::get_id()) {}

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
    // 层级组件与实体同寿命。删除子节点时 EnTT 可能移动父组件，不能保存其地址。
    while (const auto* children = scene_.children(entity_id{entity})) {
      if (children->empty())
        break;
      (void)destroy_entity(children->back().get());
    }
    scene_.detach(entity_id{entity});

    registry_.destroy(native);
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

  [[nodiscard]] std::size_t entity_count() const { return entity_count_; }

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

  template <typename Component>
  [[nodiscard]] const Component* get(std::uint64_t entity) const noexcept {
    return !is_alive(entity) ? nullptr : registry_.try_get<Component>(decode(entity));
  }

  template <class Component> error remove(std::uint64_t entity) {
    if (!is_owner_thread())
      return error::invalid_state;
    if (!is_alive(entity))
      return error::invalid_handle;
    return registry_.remove<Component>(decode(entity)) ? error::success : error::not_found;
  }

  /** 扩展模块的 World 级状态；类型和业务规则由使用模块定义。 */
  template <class T> T& context() {
    if (auto* value = registry_.ctx().find<T>())
      return *value;
    return registry_.ctx().emplace<T>();
  }

  template <typename... Component, typename Function> void each(Function&& function) {
    registry_.view<Component...>().each([&](entt::entity entity, auto&... components) {
      if constexpr (std::is_invocable_v<Function&, entt::entity, decltype(components)...>) {
        std::invoke(function, entity, components...);
      } else {
        std::invoke(function, components...);
      }
    });
  }

  [[nodiscard]] scene_instance_id scene_of(entity_id node) const noexcept {
    if (const auto* object = get<detail::object_state>(node.get()))
      return object->scene;
    const auto* member = get<detail::auxiliary_scene_membership>(node.get());
    return member ? member->scene : scene_instance_id{};
  }
  void assign_scene(entity_id node, scene_instance_id scene) {
    if (auto* object = get<detail::object_state>(node.get()))
      object->scene = scene;
    else
      emplace_or_replace<detail::auxiliary_scene_membership>(node.get(), scene);
  }
  template <class Function>
  void each_scene_member(scene_instance_id scene, Function&& function) const {
    if (!scene)
      return;
    registry_.view<const detail::object_state>().each([&](entt::entity entity, const auto& member) {
      if (member.scene == scene)
        function(entity_id{encode(entity)});
    });
    registry_.view<const detail::auxiliary_scene_membership>().each(
        [&](entt::entity entity, const auto& member) {
          if (member.scene == scene)
            function(entity_id{encode(entity)});
        });
  }
  /** 不保存查询快照；每次删除后重新取成员，避免 EnTT 删除使迭代器失效。 */
  void destroy_scene_members(scene_instance_id scene) {
    for (;;) {
      entity_id first;
      each_scene_member(scene, [&](entity_id node) {
        if (!first)
          first = node;
      });
      if (!first)
        break;
      (void)destroy_entity(first.get());
    }
  }
  [[nodiscard]] world_hierarchy& scene() noexcept { return scene_; }
  [[nodiscard]] const world_hierarchy& scene() const noexcept { return scene_; }
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
  world_hierarchy scene_;
  std::uint32_t domain_;
  std::thread::id owner_thread_;
  std::size_t entity_count_ = 0;
};

} // namespace gneiss::engine

#endif
