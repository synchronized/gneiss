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
render_scene_state::prepare_capture(const render::frame_request& request,
                                    std::vector<target_description> sizes) const {
  if (closed_)
    return error::invalid_state;
  if (sizes.empty())
    sizes.assign(request.views.size(), {request.window.width, request.window.height});
  if (sizes.size() != request.views.size())
    return error::invalid_argument;
  if (request.views.empty() && (!request.window.width || !request.window.height))
    return error::invalid_argument;
  std::set<rid> targets;
  std::vector<std::shared_ptr<scene_content>> contents;
  for (std::size_t index = 0; index < request.views.size(); ++index) {
    const auto& view = request.views[index];
    if (!sizes[index].width || !sizes[index].height || !targets.insert(view.target).second)
      return error::invalid_argument;
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
    contents.push_back(group->content);
  }
  return capture_operation{[request, contents = std::move(contents), sizes = std::move(sizes)] {
    return capture_content(request, contents, sizes);
  }};
}
result<frame_packet> render_scene_state::capture(const render::frame_request& request) const {
  auto operation = prepare_capture(request);
  if (!operation)
    return operation.error();
  return (*operation)();
}
result<frame_packet>
render_scene_state::capture_content(const render::frame_request& request,
                                    const std::vector<std::shared_ptr<scene_content>>& contents,
                                    const std::vector<target_description>& sizes) {
  frame_packet packet;
  packet.window = request.window;
  packet.ui = request.ui;
  packet.debug = request.debug;
  if (request.views.empty()) {
    packet.views.push_back(
        {.width = request.window.width, .height = request.window.height, .scene = {}});
    return packet;
  }
  packet.views.reserve(request.views.size());
  for (std::size_t index = 0; index < request.views.size(); ++index) {
    draw_view output{.target = request.views[index].target,
                     .width = sizes[index].width,
                     .height = sizes[index].height,
                     .scene = {}};
    const auto& view = request.views[index];
    output.scene.has_camera = true;
    output.scene.layers = view.layers;
    output.scene.camera.view = view.view_matrix;
    output.scene.camera.projection = view.projection_matrix;
    output.scene.camera.transform.translation = view.camera_position;
    output.scene.camera.viewport_width = sizes[index].width;
    output.scene.camera.viewport_height = sizes[index].height;
    output.scene.instances.reserve(contents[index]->instances.size());
    for (const auto& [key, entry] : contents[index]->instances) {
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
            packet.resources.textures_.try_emplace(handles[slot],
                                                   std::move(versions.textures[slot]));
      }
      output.scene.instances.push_back(
          {value.mesh, value.material, value.world_matrix, entry.normal_matrix, value.layers});
    }
    packet.capture.copied_payload_bytes += output.scene.instances.size() * sizeof(draw_instance);
    packet.views.push_back(std::move(output));
  }
  return packet;
}
result<void> render_scene_state::destroy_scene(rid handle) {
  if (closed_)
    return error::invalid_state;
  auto* group = scenes_.get(handle, resource_type::render_scene);
  if (!group)
    return error::invalid_handle;
  // 立即撤销接受侧身份；在途命令和捕获任务各自保活内容，最后释放时回收。
  // 不排入可能拒绝的清空命令，保证绑定析构不受队列背压影响。
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
