// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/rendering/world_render_binding.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
#include <limits>
#include <map>
namespace gneiss {
struct world_render_binding::impl {
  world& source;
  render_service& render;
  rid scene{};
  std::shared_ptr<detail::world_change_inbox> changes;
  std::map<entity_id, render::instance_key> instances;
  std::uint64_t last_key{};
  impl(world& source, render_service& render) : source(source), render(render) {}
  ~impl() {
    if (scene)
      (void)render.destroy_scene(scene);
  }
};
world_render_binding::world_render_binding(std::unique_ptr<impl> value) : impl_(std::move(value)) {}
world_render_binding::~world_render_binding() = default;
rid world_render_binding::scene() const noexcept { return impl_->scene; }
result<std::unique_ptr<world_render_binding>> world_render_binding::create(world& source,
                                                                           render_service& render) {
  auto state = detail::world_component_access::get(source);
  if (!state)
    return state.error();
  auto value = std::make_unique<impl>(source, render);
  value->changes = (*state)->changes.subscribe();
  // 只在建立绑定时全量枚举；后续订阅记录不会被其他消费者清空。
  (*state)->each<detail::object_state, mesh_renderer_component>(
      [&](entt::entity entity, const auto&, const auto&) {
        value->changes->objects[entity_id{(*state)->encode_entity(entity)}].transform = true;
      });
  auto scene = render.create_scene();
  if (!scene)
    return scene.error();
  value->scene = *scene;
  return std::unique_ptr<world_render_binding>(new world_render_binding(std::move(value)));
}
result<void> world_render_binding::synchronize() {
  auto state = detail::world_component_access::get(impl_->source);
  if (!state)
    return state.error();
  render::scene_commands commands{impl_->scene, {}};
  std::map<entity_id, render::instance_key> updates;
  auto last_key = impl_->last_key;
  const auto mesh_type = runtime_type_of<mesh_renderer_component>();
  for (const auto& [id, change] : impl_->changes->objects) {
    const auto existing = impl_->instances.find(id);
    auto key = existing == impl_->instances.end() ? render::instance_key{} : existing->second;
    const auto changed_component = change.components.find(mesh_type);
    if (!change.transform && !change.destroyed && changed_component == change.components.end())
      continue;
    const auto* component = (*state)->get<mesh_renderer_component>(id.get());
    const auto* object = (*state)->get<detail::object_state>(id.get());
    const bool ready = !change.destroyed && object && component && component->value.mesh &&
                       component->value.material;
    if (key &&
        (!ready || (changed_component != change.components.end() && changed_component->second))) {
      commands.entries.emplace_back(render::destroy_instance{key});
      key = {};
      updates[id] = {};
    }
    if (!ready)
      continue;
    auto matrix = impl_->source.find(id).world_matrix();
    if (!matrix)
      return matrix.error();
    const auto mesh = resource_handle(component->value.mesh);
    const auto material = resource_handle(component->value.material);
    if (!key) {
      if (last_key == std::numeric_limits<std::uint64_t>::max())
        return error::invalid_state;
      key = render::instance_key{++last_key};
      commands.entries.emplace_back(render::create_instance{key, {mesh, material, *matrix}});
      updates[id] = key;
    } else {
      if (change.transform)
        commands.entries.emplace_back(render::set_transform{key, *matrix});
      if (changed_component != change.components.end())
        commands.entries.emplace_back(render::set_resources{key, mesh, material});
    }
  }
  if (!commands.entries.empty()) {
    auto submitted = impl_->render.submit(std::move(commands));
    if (!submitted)
      return submitted.error();
  }
  // 所有分配在接受前完成；成功后通过节点转移提交身份，避免再次分配。
  for (auto it = updates.begin(); it != updates.end();) {
    impl_->instances.erase(it->first);
    if (!it->second)
      it = updates.erase(it);
    else
      ++it;
  }
  impl_->instances.merge(updates);
  impl_->last_key = last_key;
  impl_->changes->objects.clear();
  return {};
}
}
