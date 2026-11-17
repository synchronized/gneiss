// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/scene/rendering/world_render_binding.hpp"
#include <gneiss/engine/function/render/service.hpp>
#include "function/scene/detail/world_component_access.hpp"
#include <limits>
#include <map>
namespace gneiss {
world_render_binding::world_render_binding(world& source, render::service& render)
    : source_(source), render_(render) {}
world_render_binding::~world_render_binding() {
  subscription_.disconnect();
  if (scene_)
    (void)render_.destroy_scene(scene_);
}
rid world_render_binding::scene() const noexcept { return scene_; }
void world_render_binding::on_changed(const object_changes& batch) {
  const auto mesh_type = runtime_type_of<mesh_component>();
  for (const auto& [id, change] : batch) {
    const auto component = change.components.find(mesh_type);
    if (!change.transform && !change.destroyed && component == change.components.end())
      continue;
    auto& pending = pending_[id];
    if (change.destroyed) {
      pending = {.destroyed = true, .components = {}};
      continue;
    }
    if (pending.destroyed)
      continue;
    pending.transform |= change.transform;
    if (component != change.components.end())
      pending.components[mesh_type] |= component->second;
  }
}
result<std::unique_ptr<world_render_binding>>
world_render_binding::create(world& source, gneiss::render::service& render) {
  auto state = detail::world_component_access::get(source);
  if (!state)
    return state.error();
  auto value = std::unique_ptr<world_render_binding>(new world_render_binding(source, render));
  value->subscription_ = source.changes().connect<&world_render_binding::on_changed>(*value);
  // 只在建立绑定时全量枚举；后续订阅记录不会被其他消费者清空。
  (*state)->each<detail::object_state, mesh_component>(
      [&](entt::entity entity, const auto&, const auto&) {
        value->pending_[entity_id{(*state)->encode_entity(entity)}].transform = true;
      });
  auto scene = render.create_scene();
  if (!scene)
    return scene.error();
  value->scene_ = *scene;
  return value;
}
result<void> world_render_binding::synchronize() {
  auto state = detail::world_component_access::get(source_);
  if (!state)
    return state.error();
  render::scene_commands commands{scene_, {}};
  std::map<entity_id, render::instance_key> updates;
  auto last_key = last_key_;
  const auto mesh_type = runtime_type_of<mesh_component>();
  for (const auto& [id, change] : pending_) {
    const auto existing = instances_.find(id);
    auto key = existing == instances_.end() ? render::instance_key{} : existing->second;
    const auto changed_component = change.components.find(mesh_type);
    if (!change.transform && !change.destroyed && changed_component == change.components.end())
      continue;
    const auto* component = (*state)->get<mesh_component>(id.get());
    const auto* object = (*state)->get<detail::object_state>(id.get());
    const bool ready =
        !change.destroyed && object && component && component->mesh && component->material;
    if (key &&
        (!ready || (changed_component != change.components.end() && changed_component->second))) {
      commands.entries.emplace_back(render::destroy_instance{key});
      key = {};
      updates[id] = {};
    }
    if (!ready)
      continue;
    auto matrix = source_.find(id).world_matrix();
    const auto mesh = resource_handle(component->mesh);
    const auto material = resource_handle(component->material);
    if (!key) {
      if (last_key == std::numeric_limits<std::uint64_t>::max())
        return error::invalid_state;
      key = render::instance_key{++last_key};
      commands.entries.emplace_back(render::create_instance{key, {mesh, material, matrix}});
      updates[id] = key;
    } else {
      if (change.transform)
        commands.entries.emplace_back(render::set_transform{key, matrix});
      if (changed_component != change.components.end())
        commands.entries.emplace_back(render::set_resources{key, mesh, material});
    }
  }
  if (!commands.entries.empty()) {
    auto submitted = render_.submit(std::move(commands));
    if (!submitted)
      return submitted.error();
  }
  // 所有分配在接受前完成；成功后通过节点转移提交身份，避免再次分配。
  for (auto it = updates.begin(); it != updates.end();) {
    instances_.erase(it->first);
    if (!it->second)
      it = updates.erase(it);
    else
      ++it;
  }
  instances_.merge(updates);
  last_key_ = last_key;
  pending_.clear();
  return {};
}
}
