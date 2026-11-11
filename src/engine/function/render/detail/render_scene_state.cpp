// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/render/detail/render_scene_state.hpp"
#include <cmath>
#include <set>
#include <type_traits>
namespace gneiss::detail {
namespace {
bool valid_matrix(const math::matrix4& value) {
  for (auto element : math::matrix_elements(value))
    if (!std::isfinite(element))
      return false;
  return value[0][3] == 0.F && value[1][3] == 0.F && value[2][3] == 0.F && value[3][3] == 1.F &&
         std::isfinite(glm::determinant(value)) && glm::determinant(value) != 0.F;
}
}
result<rid> render_scene_state::create_scene() {
  if (closed_)
    return error::invalid_state;
  return scenes_.create(resource_type::render_scene, scene{});
}
result<void> render_scene_state::submit(render::scene_commands&& commands) {
  if (closed_)
    return error::invalid_state;
  auto* group = scenes_.get(commands.scene, resource_type::render_scene);
  if (!group)
    return error::invalid_handle;
  // 仅暂存本次触及的实例。返回错误前不发布状态，也不消费调用方命令。
  std::map<std::uint64_t, instance> changed;
  std::set<std::uint64_t> removed;
  auto last_key = group->last_key;
  for (const auto& command : commands.entries) {
    auto applied = std::visit(
        [&](const auto& update) -> result<void> {
          using T = std::decay_t<decltype(update)>;
          const auto key = update.id.value;
          if (!update.id)
            return error::invalid_handle;
          if constexpr (std::is_same_v<T, render::create_instance>) {
            if (key <= last_key)
              return error::invalid_argument;
            const auto& value = update.description;
            if (value.mesh == null_rid || value.material == null_rid ||
                !valid_matrix(value.world_matrix))
              return error::invalid_argument;
            auto pinned = pin_(value);
            if (!pinned)
              return pinned.error();
            changed.emplace(key, instance{value, std::move(*pinned)});
            last_key = key;
          } else {
            if (removed.contains(key))
              return error::invalid_handle;
            auto found = changed.find(key);
            if (found == changed.end()) {
              const auto previous = group->instances.find(key);
              if (previous == group->instances.end())
                return error::invalid_handle;
              found = changed.emplace(key, previous->second).first;
            }
            auto& entry = found->second;
            if constexpr (std::is_same_v<T, render::set_transform>) {
              if (!valid_matrix(update.world_matrix))
                return error::invalid_argument;
              entry.description.world_matrix = update.world_matrix;
            } else if constexpr (std::is_same_v<T, render::set_resources>) {
              if (update.mesh == null_rid || update.material == null_rid)
                return error::invalid_handle;
              auto value = entry.description;
              value.mesh = update.mesh;
              value.material = update.material;
              auto pinned = pin_(value);
              if (!pinned)
                return pinned.error();
              entry.description = value;
              entry.resources = std::move(*pinned);
            } else if constexpr (std::is_same_v<T, render::set_visibility>) {
              entry.description.layers = update.visibility.layers;
              entry.description.visible = update.visibility.visible;
              entry.description.casts_shadows = update.visibility.casts_shadows;
            } else if constexpr (std::is_same_v<T, render::destroy_instance>) {
              changed.erase(found);
              removed.insert(key);
            }
          }
          return {};
        },
        command);
    if (!applied)
      return applied.error();
  }
  // 校验及依赖固定完成后转移节点；发布阶段不再分配实例存储。
  for (auto key : removed)
    group->instances.erase(key);
  while (!changed.empty()) {
    auto node = changed.extract(changed.begin());
    group->instances.erase(node.key());
    group->instances.insert(std::move(node));
  }
  group->last_key = last_key;
  commands.entries.clear();
  commands.scene = null_rid;
  return {};
}
result<void> render_scene_state::destroy_scene(rid handle) {
  if (closed_)
    return error::invalid_state;
  if (!scenes_.get(handle, resource_type::render_scene))
    return error::invalid_handle;
  (void)scenes_.destroy(handle, resource_type::render_scene);
  return {};
}
void render_scene_state::close() noexcept {
  closed_ = true;
  scenes_.close();
  pin_ = {};
}
const render_scene_state::instance*
render_scene_state::find(rid handle, render::instance_key key) const noexcept {
  const auto* group = scenes_.get(handle, resource_type::render_scene);
  if (!group)
    return nullptr;
  const auto found = group->instances.find(key.value);
  return found == group->instances.end() ? nullptr : &found->second;
}
}
