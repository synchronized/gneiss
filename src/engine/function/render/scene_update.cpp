// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/render/detail/render_scene_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
namespace gneiss {
namespace {
bool valid_matrix(const math::matrix4& value) {
  for (auto element : math::matrix_elements(value))
    if (!std::isfinite(element))
      return false;
  return value[0][3] == 0.F && value[1][3] == 0.F && value[2][3] == 0.F && value[3][3] == 1.F &&
         std::isfinite(glm::determinant(value)) && glm::determinant(value) != 0.F;
}
}
struct scene_update::implementation {
  enum class operation { create, transform, mesh, material, layers, visible, shadows, destroy };
  struct command {
    operation op;
    rid handle;
    mesh_instance_description description;
    detail::scene_resource_pin resources;
  };
  std::weak_ptr<detail::render_scene_state> owner;
  rid scene{};
  std::uint64_t token{};
  std::vector<rid> reservations;
  std::vector<command> commands;
  std::unordered_set<rid> deleted;
  bool submitted{};
  ~implementation() {
    if (submitted)
      return;
    if (auto state = owner.lock()) {
      for (auto id : reservations) {
        auto* entry = state->instances_.get(id, resource_type::render_instance);
        if (!entry || entry->reservation != token)
          continue;
        if (auto* group = state->scenes_.get(scene, resource_type::render_scene))
          std::erase(group->instances, id);
        (void)state->instances_.destroy(id, resource_type::render_instance);
      }
    }
  }
  result<detail::render_scene_state::instance*> target(rid id) {
    auto state = owner.lock();
    if (submitted || !state || state->closed_)
      return error::invalid_state;
    if (!state->scenes_.get(scene, resource_type::render_scene))
      return error::invalid_handle;
    auto* entry = state->instances_.get(id, resource_type::render_instance);
    if (!entry || entry->scene != scene || deleted.contains(id) ||
        (entry->reservation != 0 && entry->reservation != token))
      return error::invalid_handle;
    return entry;
  }
  result<void> record(operation op, rid id, const mesh_instance_description& value) {
    auto found = target(id);
    if (!found)
      return found.error();
    detail::scene_resource_pin resources;
    if (op == operation::mesh || op == operation::material) {
      // 只固定被换绑的资源；未修改的依赖不重新通过外部 RID 查询。
      auto pinned = owner.lock()->pin_(value);
      if (!pinned)
        return pinned.error();
      resources = std::move(*pinned);
    }
    commands.push_back({op, id, value, std::move(resources)});
    if (op == operation::destroy)
      deleted.insert(id);
    return {};
  }
};
scene_update::scene_update(std::unique_ptr<implementation> impl) : impl_(std::move(impl)) {}
scene_update::~scene_update() = default;
scene_update::scene_update(scene_update&&) noexcept = default;
scene_update& scene_update::operator=(scene_update&&) noexcept = default;
result<rid> scene_update::create_mesh(const mesh_instance_description& value) {
  if (!impl_ || impl_->submitted)
    return error::invalid_state;
  auto state = impl_->owner.lock();
  if (!state || state->closed_)
    return error::invalid_state;
  auto* group = state->scenes_.get(impl_->scene, resource_type::render_scene);
  if (!group)
    return error::invalid_handle;
  if (value.mesh == null_rid || value.material == null_rid || !valid_matrix(value.world_matrix))
    return error::invalid_argument;
  auto pinned = state->pin_(value);
  if (!pinned)
    return pinned.error();
  auto handle = state->instances_.create(
      resource_type::render_instance,
      detail::render_scene_state::instance{impl_->scene, impl_->token, value, std::move(*pinned)});
  if (!handle)
    return handle.error();
  impl_->reservations.push_back(*handle);
  group->instances.push_back(*handle);
  impl_->commands.push_back({implementation::operation::create, *handle, value, {}});
  return *handle;
}
result<void> scene_update::set_transform(rid id, const math::matrix4& matrix) {
  if (!impl_)
    return error::invalid_state;
  if (!valid_matrix(matrix))
    return error::invalid_argument;
  mesh_instance_description value;
  value.world_matrix = matrix;
  return impl_->record(implementation::operation::transform, id, value);
}
result<void> scene_update::set_mesh(rid id, rid mesh) {
  if (mesh == null_rid)
    return error::invalid_handle;
  if (!impl_)
    return error::invalid_state;
  mesh_instance_description value;
  value.mesh = mesh;
  return impl_->record(implementation::operation::mesh, id, value);
}
result<void> scene_update::set_material(rid id, rid material) {
  if (material == null_rid)
    return error::invalid_handle;
  if (!impl_)
    return error::invalid_state;
  mesh_instance_description value;
  value.material = material;
  return impl_->record(implementation::operation::material, id, value);
}
result<void> scene_update::set_layers(rid id, std::uint64_t layers) {
  if (!impl_)
    return error::invalid_state;
  mesh_instance_description value;
  value.layers = layers;
  return impl_->record(implementation::operation::layers, id, value);
}
result<void> scene_update::set_visible(rid id, bool visible) {
  if (!impl_)
    return error::invalid_state;
  mesh_instance_description value;
  value.visible = visible;
  return impl_->record(implementation::operation::visible, id, value);
}
result<void> scene_update::set_casts_shadows(rid id, bool shadows) {
  if (!impl_)
    return error::invalid_state;
  mesh_instance_description value;
  value.casts_shadows = shadows;
  return impl_->record(implementation::operation::shadows, id, value);
}
result<void> scene_update::destroy(rid id) {
  if (!impl_)
    return error::invalid_state;
  return impl_->record(implementation::operation::destroy, id, {});
}
namespace detail {
result<rid> render_scene_state::create_scene() {
  if (closed_)
    return error::invalid_state;
  return scenes_.create(resource_type::render_scene, scene{});
}
result<scene_update> render_scene_state::begin_update(rid scene_id) {
  if (closed_)
    return error::invalid_state;
  if (!scenes_.get(scene_id, resource_type::render_scene))
    return error::invalid_handle;
  if (next_batch_ == std::numeric_limits<std::uint64_t>::max())
    return error::invalid_state;
  auto batch = std::make_unique<scene_update::implementation>();
  batch->owner = shared_from_this();
  batch->scene = scene_id;
  batch->token = next_batch_++;
  return scene_update{std::move(batch)};
}
result<void> render_scene_state::submit(scene_update&& batch) {
  auto* impl = batch.impl_.get();
  if (closed_ || !impl || impl->submitted)
    return error::invalid_state;
  if (impl->owner.lock().get() != this)
    return error::invalid_argument;
  auto* group = scenes_.get(impl->scene, resource_type::render_scene);
  if (!group)
    return error::invalid_handle;
  // 全部前端校验通过后才发布；不构造整个场景的副本。
  for (const auto& command : impl->commands) {
    const auto* entry = instances_.get(command.handle, resource_type::render_instance);
    if (!entry || entry->scene != impl->scene ||
        (entry->reservation != 0 && entry->reservation != impl->token))
      return error::invalid_handle;
  }
  using op = scene_update::implementation::operation;
  for (auto& command : impl->commands) {
    auto* entry = instances_.get(command.handle, resource_type::render_instance);
    switch (command.op) {
    case op::create:
      entry->reservation = 0;
      break;
    case op::transform:
      entry->description.world_matrix = command.description.world_matrix;
      break;
    case op::mesh:
      entry->description.mesh = command.description.mesh;
      entry->resources.mesh = std::move(command.resources.mesh);
      break;
    case op::material:
      entry->description.material = command.description.material;
      entry->resources.material = std::move(command.resources.material);
      entry->resources.textures = std::move(command.resources.textures);
      break;
    case op::layers:
      entry->description.layers = command.description.layers;
      break;
    case op::visible:
      entry->description.visible = command.description.visible;
      break;
    case op::shadows:
      entry->description.casts_shadows = command.description.casts_shadows;
      break;
    case op::destroy:
      std::erase(group->instances, command.handle);
      (void)instances_.destroy(command.handle, resource_type::render_instance);
      break;
    }
  }
  impl->submitted = true;
  batch.impl_.reset();
  return {};
}
result<void> render_scene_state::destroy_scene(rid handle) {
  if (closed_)
    return error::invalid_state;
  auto* group = scenes_.get(handle, resource_type::render_scene);
  if (!group)
    return error::invalid_handle;
  for (auto id : group->instances)
    (void)instances_.destroy(id, resource_type::render_instance);
  (void)scenes_.destroy(handle, resource_type::render_scene);
  return {};
}
void render_scene_state::close() noexcept {
  closed_ = true;
  instances_.close();
  scenes_.close();
  pin_ = {};
}
const render_scene_state::instance* render_scene_state::find(rid handle) const noexcept {
  const auto* entry = instances_.get(handle, resource_type::render_instance);
  return entry && entry->reservation == 0 ? entry : nullptr;
}
}
}
