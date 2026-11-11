// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/render/detail/render_scene_state.hpp"
#include <cmath>
#include <set>
#include <type_traits>
namespace gneiss::render::detail {
namespace {
result<math::matrix4> prepare_normal_matrix(const math::matrix4& value) {
  for (auto element : math::matrix_elements(value))
    if (!std::isfinite(element))
      return error::invalid_argument;
  if (value[0][3] != 0.F || value[1][3] != 0.F || value[2][3] != 0.F || value[3][3] != 1.F)
    return error::invalid_argument;
  const auto determinant = glm::determinant(math::matrix3{value});
  if (!std::isfinite(determinant) || determinant == 0.F)
    return error::invalid_argument;
  const auto normal = math::matrix4{glm::transpose(glm::inverse(math::matrix3{value}))};
  for (auto element : math::matrix_elements(normal))
    if (!std::isfinite(element))
      return error::invalid_argument;
  return normal;
}
}
result<rid> render_scene_state::create_scene() {
  if (closed_)
    return error::invalid_state;
  return scenes_.create(resource_type::render_scene, scene{});
}
result<void> render_scene_state::dispatch(std::function<void()> operation) {
  if (dispatch_)
    return dispatch_(std::move(operation));
  operation();
  return {};
}
result<void> render_scene_state::submit(render::scene_commands&& commands) {
  if (closed_)
    return error::invalid_state;
  auto* group = scenes_.get(commands.scene, resource_type::render_scene);
  if (!group)
    return error::invalid_handle;
  struct prepared_command {
    render::command input;
    scene_resource_pin resources;
    math::matrix4 normal{1.F};
  };
  std::vector<prepared_command> prepared;
  prepared.reserve(commands.entries.size());
  std::set<std::uint64_t> added;
  std::set<std::uint64_t> removed;
  auto last_key = group->last_key;
  for (const auto& command : commands.entries) {
    prepared_command next{command, {}, math::matrix4{1.F}};
    auto checked = std::visit(
        [&](const auto& update) -> result<void> {
          using T = std::decay_t<decltype(update)>;
          const auto key = update.id.value;
          if (!update.id)
            return error::invalid_handle;
          if constexpr (std::is_same_v<T, render::create_instance>) {
            if (key <= last_key)
              return error::invalid_argument;
            if (update.description.mesh == null_rid || update.description.material == null_rid)
              return error::invalid_argument;
            auto normal = prepare_normal_matrix(update.description.world_matrix);
            if (!normal)
              return normal.error();
            auto pinned = pin_(update.description);
            if (!pinned)
              return pinned.error();
            next.normal = *normal;
            next.resources = std::move(*pinned);
            added.insert(key);
            last_key = key;
          } else {
            if (removed.contains(key) || (!added.contains(key) && !group->accepted.contains(key)))
              return error::invalid_handle;
            if constexpr (std::is_same_v<T, render::set_transform>) {
              auto normal = prepare_normal_matrix(update.world_matrix);
              if (!normal)
                return normal.error();
              next.normal = *normal;
            } else if constexpr (std::is_same_v<T, render::set_resources>) {
              if (update.mesh == null_rid || update.material == null_rid)
                return error::invalid_handle;
              auto pinned = pin_({.mesh = update.mesh, .material = update.material});
              if (!pinned)
                return pinned.error();
              next.resources = std::move(*pinned);
            } else if constexpr (std::is_same_v<T, render::destroy_instance>) {
              added.erase(key);
              removed.insert(key);
            }
          }
          return {};
        },
        command);
    if (!checked)
      return checked.error();
    prepared.push_back(std::move(next));
  }
  // 执行数据保活依赖，不捕获接受侧、RID 表或调用方内存。
  auto accepted = dispatch([content = group->content, prepared = std::move(prepared)]() mutable {
    for (auto& next : prepared) {
      std::visit(
          [&](const auto& update) {
            using T = std::decay_t<decltype(update)>;
            const auto key = update.id.value;
            if constexpr (std::is_same_v<T, render::create_instance>) {
              content->instances.emplace(
                  key, instance{update.description, std::move(next.resources), next.normal});
            } else if constexpr (std::is_same_v<T, render::destroy_instance>) {
              content->instances.erase(key);
            } else {
              auto& entry = content->instances.at(key);
              if constexpr (std::is_same_v<T, render::set_transform>) {
                entry.description.world_matrix = update.world_matrix;
                entry.normal_matrix = next.normal;
              } else if constexpr (std::is_same_v<T, render::set_resources>) {
                entry.description.mesh = update.mesh;
                entry.description.material = update.material;
                entry.resources = std::move(next.resources);
              } else if constexpr (std::is_same_v<T, render::set_visibility>) {
                entry.description.layers = update.visibility.layers;
                entry.description.visible = update.visibility.visible;
                entry.description.casts_shadows = update.visibility.casts_shadows;
              }
            }
          },
          next.input);
    }
  });
  if (!accepted)
    return accepted.error();
  for (auto key : removed)
    group->accepted.erase(key);
  group->accepted.merge(added);
  group->last_key = last_key;
  commands.entries.clear();
  commands.scene = null_rid;
  return {};
}
result<render_scene_state::capture_operation>
render_scene_state::prepare_capture(const render::frame_request& request) const {
  if (closed_)
    return error::invalid_state;
  if (request.views.size() > 1)
    return error::unsupported;
  if (request.window.width == 0 || request.window.height == 0)
    return error::invalid_argument;
  std::shared_ptr<scene_content> content;
  if (!request.views.empty()) {
    const auto& view = request.views.front();
    for (auto element : math::matrix_elements(view.view_matrix))
      if (!std::isfinite(element))
        return error::invalid_argument;
    for (auto element : math::matrix_elements(view.projection_matrix))
      if (!std::isfinite(element))
        return error::invalid_argument;
    for (auto element : math::components(view.camera_position))
      if (!std::isfinite(element))
        return error::invalid_argument;
    const auto* group = scenes_.get(view.scene, resource_type::render_scene);
    if (!group)
      return error::invalid_handle;
    content = group->content;
  }
  return capture_operation{
      [request, content = std::move(content)] { return capture_content(request, content.get()); }};
}
result<render_frame_packet>
render_scene_state::capture(const render::frame_request& request) const {
  auto operation = prepare_capture(request);
  if (!operation)
    return operation.error();
  return (*operation)();
}
result<render_frame_packet>
render_scene_state::capture_content(const render::frame_request& request,
                                    const scene_content* content) {
  render_frame_packet packet;
  packet.window = request.window;
  packet.ui = request.ui;
  packet.debug = request.debug;
  if (request.views.empty())
    return packet;
  const auto& view = request.views.front();
  packet.scene.has_camera = true;
  packet.scene.layers = view.layers;
  packet.scene.camera.view = view.view_matrix;
  packet.scene.camera.projection = view.projection_matrix;
  packet.scene.camera.transform.translation = view.camera_position;
  packet.scene.camera.viewport_width = request.window.width;
  packet.scene.camera.viewport_height = request.window.height;
  packet.scene.instances.reserve(content->instances.size());
  for (const auto& [key, entry] : content->instances) {
    (void)key;
    const auto& value = entry.description;
    if (!value.visible || (value.layers & view.layers) == 0)
      continue;
    // 当前 Granit 场景接口没有逐实例阴影开关；不得静默绘制成不同语义。
    if (!value.casts_shadows)
      return error::unsupported;
    auto versions = entry.resources.capture();
    packet.resources.meshes_.try_emplace(value.mesh, std::move(versions.mesh));
    packet.resources.materials_.try_emplace(value.material, versions.material);
    if (versions.material) {
      const auto handles = versions.material->texture_handles();
      for (std::size_t slot = 0; slot < handles.size(); ++slot)
        if (handles[slot] != null_rid)
          packet.resources.textures_.try_emplace(handles[slot], std::move(versions.textures[slot]));
    }
    packet.scene.instances.push_back(
        {value.mesh, value.material, value.world_matrix, entry.normal_matrix, value.layers});
  }
  packet.capture.copied_payload_bytes =
      packet.scene.instances.size() * sizeof(render_draw_instance);
  return packet;
}
result<void> render_scene_state::destroy_scene(rid handle) {
  if (closed_)
    return error::invalid_state;
  auto* group = scenes_.get(handle, resource_type::render_scene);
  if (!group)
    return error::invalid_handle;
  auto accepted = dispatch([content = group->content] { content->instances.clear(); });
  if (!accepted)
    return accepted.error();
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
  const auto found = group->content->instances.find(key.value);
  return found == group->content->instances.end() ? nullptr : &found->second;
}
}
