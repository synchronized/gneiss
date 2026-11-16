// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_IMPLEMENTATION_HPP_
#define GNEISS_WORLD_IMPLEMENTATION_HPP_

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/core/types/rid_table.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <atomic>
#include <limits>

#include "function/scene/detail/object_state.hpp"
#include "function/scene/detail/world_changes.hpp"
#include <gneiss/engine/function/scene/world.hpp>

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

namespace gneiss {

struct world::implementation final {
public:
  static std::uint16_t allocate_scene_domain() noexcept {
    static std::atomic_uint32_t next{1024U};
    const auto value = next.fetch_add(1U, std::memory_order_relaxed);
    return value <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(value)
                                                              : 0U;
  }

private:
  friend class world;
  friend class scene_instance;
  const std::uint16_t scene_domain = allocate_scene_domain();
  rid_table<scene_instance_id, scene_instance_id> scene_slots{scene_domain};

public:
  explicit implementation(std::uint32_t domain) noexcept
      : domain_(domain), owner_thread_(std::this_thread::get_id()) {}

  implementation(const implementation&) = delete;
  implementation& operator=(const implementation&) = delete;
  implementation(implementation&&) = delete;
  implementation& operator=(implementation&&) = delete;

  [[nodiscard]] bool is_owner_thread() const noexcept {
    return owner_thread_ == std::this_thread::get_id();
  }

  detail::world_changes changes;

  /** 祖先变化影响整棵子树；没有消费者时不遍历。 */
  void mark_transform_changed(entity_id root) {
    if (!changes.observed())
      return;
    std::vector<entity_id> pending{root};
    while (!pending.empty()) {
      const auto id = pending.back();
      pending.pop_back();
      changes.transform(id);
      if (const auto* object = get<detail::object_state>(id.get()))
        pending.insert(pending.end(), object->children.begin(), object->children.end());
    }
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
    while (const auto* object = get<detail::object_state>(entity)) {
      if (object->children.empty())
        break;
      (void)destroy_entity(object->children.back().get());
    }
    detach(entity_id{entity});

    changes.destroyed(entity_id{entity});
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
    auto& component = registry_.emplace<Component>(decode(entity), std::forward<Args>(args)...);
    if (changes.observed())
      changes.component(entity_id{entity}, runtime_type_of<Component>());
    return component;
  }

  template <typename Component, typename... Args>
  Component& emplace_or_replace(std::uint64_t entity, Args&&... args) {
    if (!is_alive(entity)) {
      throw std::invalid_argument{"实体已失效"};
    }
    auto& component =
        registry_.emplace_or_replace<Component>(decode(entity), std::forward<Args>(args)...);
    if (changes.observed())
      changes.component(entity_id{entity}, runtime_type_of<Component>());
    return component;
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
    if (!registry_.remove<Component>(decode(entity)))
      return error::not_found;
    if (changes.observed())
      changes.component(entity_id{entity}, runtime_type_of<Component>(), true);
    return error::success;
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
  /** 仅收集稳定 ID；父节点删除过的子节点会在随后校验时跳过。 */
  void destroy_scene_members(scene_instance_id scene) {
    std::vector<entity_id> members;
    each_scene_member(scene, [&](entity_id id) { members.push_back(id); });
    for (auto id : members)
      (void)destroy_entity(id.get());
  }
  result<entity_id> create_object(entity_id parent, std::uint64_t entity = 0);
  void detach(entity_id id) noexcept;
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
  std::uint32_t domain_;
  std::thread::id owner_thread_;
  std::size_t entity_count_ = 0;
};

} // namespace gneiss

#endif
