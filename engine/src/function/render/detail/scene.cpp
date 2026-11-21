// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/render/detail/scene.hpp"
#include <type_traits>
namespace gneiss::render {
void scene::apply(std::vector<prepared_scene_command> prepared) {
  for (auto& next : prepared) {
    std::visit(
        [&](const auto& update) {
          using T = std::decay_t<decltype(update)>;
          const auto key = update.id.value;
          if constexpr (std::is_same_v<T, render::create_instance>) {
            instances_.emplace(
                key, instance{update.description, std::move(next.resources), next.normal});
          } else if constexpr (std::is_same_v<T, render::destroy_instance>) {
            instances_.erase(key);
          } else {
            auto& entry = instances_.at(key);
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
}
result<void> scene::extract(const view& view, target_description size,
                            detail::frame_packet& packet) const {
  using namespace detail;
  draw_view output{.target = view.target, .width = size.width, .height = size.height, .scene = {}};
  output.scene.has_camera = true;
  output.scene.layers = view.layers;
  output.scene.camera.view = view.view_matrix;
  output.scene.camera.projection = view.projection_matrix;
  output.scene.camera.transform.translation = view.camera_position;
  output.scene.camera.viewport_width = size.width;
  output.scene.camera.viewport_height = size.height;
  output.scene.instances.reserve(instances_.size());
  for (const auto& [key, entry] : instances_) {
    const auto& value = entry.description;
    if (!value.visible || (value.layers & view.layers) == 0)
      continue;
    auto versions = entry.resources.capture();
    packet.resources.meshes_.try_emplace(value.mesh, std::move(versions.mesh));
    packet.resources.materials_.try_emplace(value.material, versions.material);
    if (versions.material) {
      const auto handles = versions.material->texture_handles();
      for (std::size_t slot = 0; slot < handles.size(); ++slot)
        if (handles[slot] != null_rid)
          packet.resources.textures_.try_emplace(handles[slot], std::move(versions.textures[slot]));
    }
    output.scene.instances.push_back({value.mesh,
                                      value.material,
                                      value.world_matrix,
                                      entry.normal_matrix,
                                      value.layers,
                                      value.casts_shadows,
                                      {key}});
  }
  packet.capture.copied_payload_bytes += output.scene.instances.size() * sizeof(draw_instance);
  packet.views.push_back(std::move(output));
  return {};
}
}
