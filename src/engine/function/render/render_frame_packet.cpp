// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_frame_packet.hpp"

#include <chrono>
#include <new>
#include <utility>

namespace gneiss::engine::render {
namespace {

template <typename Handle, typename Resource, typename Getter>
gneiss_result capture_resource(Handle handle,
                               std::unordered_map<Handle, std::shared_ptr<const Resource>>& output,
                               Getter&& getter, bool pending = false) {
  if (output.contains(handle)) {
    return GNEISS_SUCCESS;
  }
  auto source = getter(handle);
  if (!source && !pending) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  output.emplace(handle, std::move(source));
  return GNEISS_SUCCESS;
}

} // namespace

bool render_resource_snapshot::mesh_pending(engine::core::rid mesh) const noexcept {
  const auto found = meshes_.find(mesh);
  return found != meshes_.end() && !found->second;
}
bool render_resource_snapshot::texture_pending(engine::core::rid texture) const noexcept {
  const auto found = textures_.find(texture);
  return found != textures_.end() && !found->second;
}

const mesh_resource* render_resource_snapshot::get_mesh(engine::core::rid mesh) const noexcept {
  const auto found = meshes_.find(mesh);
  return found == meshes_.end() ? nullptr : found->second.get();
}

const material_resource*
render_resource_snapshot::get_material(engine::core::rid material) const noexcept {
  const auto found = materials_.find(material);
  if (found == materials_.end()) {
    return nullptr;
  }
  static const material_resource fallback{};
  return found->second ? found->second.get() : &fallback;
}

const texture_resource*
render_resource_snapshot::get_texture(engine::core::rid texture) const noexcept {
  const auto found = textures_.find(texture);
  return found == textures_.end() ? nullptr : found->second.get();
}

gneiss_result capture_render_frame_packet(const engine::platform::native_window_info& window,
                                          engine::render::render_snapshot scene,
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
      auto result = capture_resource(
          instance.mesh, candidate.resources.meshes_,
          [&](const auto handle) { return resources.share_mesh(handle); },
          resources.mesh_status(instance.mesh) == GNEISS_ERROR_NOT_READY);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      result = capture_resource(
          instance.material, candidate.resources.materials_,
          [&](const auto handle) { return resources.share_material(handle); },
          resources.material_status(instance.material) == GNEISS_ERROR_NOT_READY);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      const auto& material = *candidate.resources.get_material(instance.material);
      for (const auto texture : material.texture_handles()) {
        if (texture == engine::core::null_rid)
          continue;
        result = capture_resource(
            texture, candidate.resources.textures_,
            [&](const auto handle) { return resources.share_texture(handle); },
            resources.texture_status(texture) == GNEISS_ERROR_NOT_READY);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
      }
    }
    for (const auto& command : candidate.ui.commands()) {
      // UI 尚未接入占位策略，不能让场景中捕获的空纹理条目绕过 UI 就绪检查。
      if (resources.texture_status(command.texture) == GNEISS_ERROR_NOT_READY) {
        return GNEISS_ERROR_NOT_READY;
      }
      const auto result =
          capture_resource(command.texture, candidate.resources.textures_,
                           [&](const auto handle) { return resources.share_texture(handle); });
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    }
    std::size_t copied_payload_bytes =
        candidate.scene.instances.size() * sizeof(engine::render::render_instance_snapshot) +
        candidate.ui.vertices().size() * sizeof(ui_vertex) +
        candidate.ui.indices().size() * sizeof(std::uint32_t) +
        candidate.ui.commands().size() * sizeof(ui_draw_command) +
        candidate.debug.lines().size() * sizeof(debug_line);
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

} // namespace gneiss::engine::render
