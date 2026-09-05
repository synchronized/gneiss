// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_frame_packet.h"

#include <chrono>
#include <new>
#include <utility>

namespace gneiss::render_internal {
namespace {

template <typename Handle, typename Resource, typename Getter>
gneiss_result capture_resource(Handle handle,
                               std::unordered_map<Handle, std::shared_ptr<const Resource>>& output,
                               Getter&& getter) {
  if (output.contains(handle)) {
    return GNEISS_SUCCESS;
  }
  auto source = getter(handle);
  if (!source) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  output.emplace(handle, std::move(source));
  return GNEISS_SUCCESS;
}

} // namespace

const mesh_resource* render_resource_snapshot::get_mesh(gneiss_mesh mesh) const noexcept {
  const auto found = meshes_.find(mesh);
  return found == meshes_.end() ? nullptr : found->second.get();
}

const material_resource*
render_resource_snapshot::get_material(gneiss_material material) const noexcept {
  const auto found = materials_.find(material);
  return found == materials_.end() ? nullptr : found->second.get();
}

const texture_resource*
render_resource_snapshot::get_texture(gneiss_texture texture) const noexcept {
  const auto found = textures_.find(texture);
  return found == textures_.end() ? nullptr : found->second.get();
}

gneiss_result capture_render_frame_packet(const application_internal::native_window_info& window,
                                          world_internal::render_snapshot scene,
                                          const render_resource_service& resources,
                                          const ui_draw_list& ui, const debug_draw_list& debug,
                                          render_frame_packet& out_packet) noexcept {
  const auto capture_started = std::chrono::steady_clock::now();
  try {
    render_frame_packet candidate = std::move(out_packet);
    candidate.resources.meshes_.clear();
    candidate.resources.materials_.clear();
    candidate.resources.textures_.clear();
    candidate.capture = {};
    candidate.window = window;
    candidate.scene = std::move(scene);
    candidate.ui = ui;
    candidate.debug = debug;
    for (const auto& instance : candidate.scene.instances) {
      auto result =
          capture_resource(instance.mesh, candidate.resources.meshes_,
                           [&](const auto handle) { return resources.share_mesh(handle); });
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      result =
          capture_resource(instance.material, candidate.resources.materials_,
                           [&](const auto handle) { return resources.share_material(handle); });
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      const auto& material = *candidate.resources.materials_.at(instance.material);
      if (material.base_color_texture != GNEISS_NULL_TEXTURE) {
        result =
            capture_resource(material.base_color_texture, candidate.resources.textures_,
                             [&](const auto handle) { return resources.share_texture(handle); });
        if (result != GNEISS_SUCCESS) {
          return result;
        }
      }
    }
    for (const auto& command : candidate.ui.commands()) {
      const auto result =
          capture_resource(command.texture, candidate.resources.textures_,
                           [&](const auto handle) { return resources.share_texture(handle); });
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    }
    std::size_t copied_payload_bytes =
        candidate.scene.instances.size() * sizeof(world_internal::render_instance_snapshot) +
        candidate.ui.vertices().size() * sizeof(gneiss_ui_vertex) +
        candidate.ui.indices().size() * sizeof(std::uint32_t) +
        candidate.ui.commands().size() * sizeof(gneiss_ui_draw_command) +
        candidate.debug.lines().size() * sizeof(gneiss_debug_line);
    copied_payload_bytes +=
        candidate.resources.meshes_.size() * sizeof(std::shared_ptr<const mesh_resource>) +
        candidate.resources.materials_.size() * sizeof(std::shared_ptr<const material_resource>) +
        candidate.resources.textures_.size() * sizeof(std::shared_ptr<const texture_resource>);
    candidate.capture.capture_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - capture_started)
            .count();
    candidate.capture.copied_payload_bytes = copied_payload_bytes;
    out_packet = std::move(candidate);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::render_internal
