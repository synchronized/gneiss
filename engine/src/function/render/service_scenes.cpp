// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/render/detail/service_impl.hpp"
#include <cmath>
#include <type_traits>
namespace gneiss::render {
result<rid> service::create_scene() { return impl_->create_scene(); }
result<void> service::submit(render::scene_commands&& update) {
  return impl_->submit_scene(std::move(update));
}
result<void> service::destroy_scene(rid scene) { return impl_->destroy_scene(scene); }

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

result<detail::scene_resource_pin>
service::implementation::pin(const mesh_instance_description& description) {
  const auto* mesh = resources.meshes_.get(description.mesh, resource_type::mesh);
  const auto* material = resources.materials_.get(description.material, resource_type::material);
  if (!mesh || !material)
    return error::invalid_handle;
  return detail::scene_resource_pin{mesh->identity, material->identity};
}
result<rid> service::implementation::create_scene() {
  if (scenes_closed)
    return error::invalid_state;
  return scenes.create(resource_type::render_scene, scene_slot{});
}
result<void> service::implementation::submit_scene(render::scene_commands&& commands) {
  if (scenes_closed)
    return error::invalid_state;
  auto* group = scenes.get(commands.scene, resource_type::render_scene);
  if (!group)
    return error::invalid_handle;
  std::vector<prepared_scene_command> prepared;
  prepared.reserve(commands.entries.size());
  std::set<std::uint64_t> added;
  std::set<std::uint64_t> removed;
  auto last_key = group->last_key;
  for (const auto& command : commands.entries) {
    prepared_scene_command next{command, {}, math::matrix4{1.F}};
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
            auto pinned = pin(update.description);
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
              auto pinned = pin({.mesh = update.mesh, .material = update.material});
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
  const auto bytes = prepared.capacity() * sizeof(prepared_scene_command);
  auto accepted =
      dispatch([content = group->content,
                prepared = std::move(prepared)]() mutable { content->apply(std::move(prepared)); },
               bytes);
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
result<service::implementation::capture_operation>
service::implementation::prepare_capture(const render::frame_request& request,
                                         std::vector<target_description> sizes) {
  if (scenes_closed)
    return error::invalid_state;
  if (sizes.empty())
    sizes.assign(request.views.size(), {request.surface.width, request.surface.height});
  if (sizes.size() != request.views.size() && !(request.views.empty() && sizes.size() == 1))
    return error::invalid_argument;
  std::set<rid> targets;
  std::vector<std::shared_ptr<render::scene>> contents;
  for (std::size_t index = 0; index < request.views.size(); ++index) {
    const auto& view = request.views[index];
    if ((view.target && (!sizes[index].width || !sizes[index].height)) ||
        !targets.insert(view.target).second)
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
    if (!view.scene) {
      contents.push_back({});
      continue;
    }
    const auto* group = scenes.get(view.scene, resource_type::render_scene);
    if (!group)
      return error::invalid_handle;
    contents.push_back(group->content);
  }
  return capture_operation{[request, contents = std::move(contents), sizes = std::move(sizes)] {
    detail::frame_packet packet;
    packet.surface = request.surface;
    packet.ui = request.ui;
    packet.debug = request.debug;
    if (request.views.empty())
      packet.views.push_back(
          {.width = sizes.empty() ? request.surface.width : sizes.front().width,
           .height = sizes.empty() ? request.surface.height : sizes.front().height,
           .scene = {}});
    for (std::size_t i = 0; i < contents.size(); ++i) {
      if (!sizes[i].width || !sizes[i].height)
        continue;
      if (!contents[i]) {
        packet.views.push_back({.target = request.views[i].target,
                                .width = sizes[i].width,
                                .height = sizes[i].height,
                                .scene = {}});
        continue;
      }
      auto extracted = contents[i]->extract(request.views[i], sizes[i], packet);
      if (!extracted)
        return result<detail::frame_packet>{extracted.error()};
    }
    return result<detail::frame_packet>{std::move(packet)};
  }};
}
result<void> service::implementation::destroy_scene(rid handle) {
  if (scenes_closed)
    return error::invalid_state;
  auto* group = scenes.get(handle, resource_type::render_scene);
  if (!group)
    return error::invalid_handle;
  // 立即撤销接受侧身份；在途命令和捕获任务各自保活内容，最后释放时回收。
  // 不排入可能拒绝的清空命令，保证绑定析构不受队列背压影响。
  (void)scenes.destroy(handle, resource_type::render_scene);
  return {};
}
}
