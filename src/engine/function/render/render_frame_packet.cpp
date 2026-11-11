// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_frame_packet.hpp"
#include "engine/core/error.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/function/render/render_service.hpp"

#include <chrono>
#include <new>
#include <utility>

namespace gneiss {
namespace {

template <typename Handle, typename Resource, typename Getter>
gneiss::error capture_resource(Handle handle,
                               std::unordered_map<Handle, std::shared_ptr<const Resource>>& output,
                               Getter&& getter, bool pending = false) {
  if (output.contains(handle)) {
    return gneiss::error::success;
  }
  auto source = getter(handle);
  if (!source && !pending) {
    return gneiss::error::invalid_handle;
  }
  output.emplace(handle, std::move(source));
  return gneiss::error::success;
}

} // namespace

bool render_resource_snapshot::mesh_pending(rid mesh) const noexcept {
  const auto found = meshes_.find(mesh);
  return found != meshes_.end() && !found->second;
}
bool render_resource_snapshot::texture_pending(rid texture) const noexcept {
  const auto found = textures_.find(texture);
  return found != textures_.end() && !found->second;
}

const mesh_data* render_resource_snapshot::get_mesh(rid mesh) const noexcept {
  const auto found = meshes_.find(mesh);
  return found == meshes_.end() ? nullptr : found->second.get();
}

const material_data* render_resource_snapshot::get_material(rid material) const noexcept {
  const auto found = materials_.find(material);
  if (found == materials_.end()) {
    return nullptr;
  }
  static const material_data fallback{};
  return found->second ? found->second.get() : &fallback;
}

const texture_data* render_resource_snapshot::get_texture(rid texture) const noexcept {
  const auto found = textures_.find(texture);
  return found == textures_.end() ? nullptr : found->second.get();
}

gneiss::error capture_render_frame_packet(const native_window_info& window, render_snapshot scene,
                                          const render_service& resources, const ui_draw_list& ui,
                                          const debug_draw_list& debug,
                                          render_frame_packet& out_packet) {
  const auto capture_started = std::chrono::steady_clock::now();
  {
    render_frame_packet candidate = std::move(out_packet);
    candidate.resources.meshes_.clear();
    candidate.resources.materials_.clear();
    candidate.resources.textures_.clear();
    candidate.capture = {};
    candidate.window = window;
    candidate.scene.camera = scene.camera;
    candidate.scene.has_camera = scene.has_camera;
    candidate.scene.layers = ~UINT64_C(0);
    candidate.scene.instances.clear();
    candidate.ui = ui;
    candidate.debug = debug;
    for (const auto& instance : scene.instances) {
      auto result = capture_resource(
          resource_handle(instance.mesh), candidate.resources.meshes_,
          [&](const auto handle) { return resources.share_mesh(handle); },
          resources.mesh_status(resource_handle(instance.mesh)) == gneiss::error::not_ready);
      if (result != gneiss::error::success) {
        return result;
      }
      result = capture_resource(
          resource_handle(instance.material), candidate.resources.materials_,
          [&](const auto handle) { return resources.share_material(handle); },
          resources.material_status(resource_handle(instance.material)) ==
              gneiss::error::not_ready);
      if (result != gneiss::error::success) {
        return result;
      }
      const auto& material = *candidate.resources.get_material(resource_handle(instance.material));
      for (const auto texture : material.texture_handles()) {
        if (texture == null_rid)
          continue;
        result = capture_resource(
            texture, candidate.resources.textures_,
            [&](const auto handle) { return resources.share_texture(handle); },
            resources.texture_status(texture) == gneiss::error::not_ready);
        if (result != gneiss::error::success) {
          return result;
        }
      }
    }
    candidate.scene.instances.reserve(scene.instances.size());
    for (const auto& instance : scene.instances) {
      render_draw_instance draw;
      draw.mesh = resource_handle(instance.mesh);
      draw.material = resource_handle(instance.material);
      if (!math::model_and_normal_matrices(instance.transform, draw.world_matrix,
                                           draw.normal_matrix))
        return error::invalid_argument;
      candidate.scene.instances.push_back(draw);
    }
    for (const auto& command : candidate.ui.commands()) {
      // UI 尚未接入占位策略，不能让场景中捕获的空纹理条目绕过 UI 就绪检查。
      if (resources.texture_status(resource_handle(command.texture)) == gneiss::error::not_ready) {
        return gneiss::error::not_ready;
      }
      const auto result =
          capture_resource(resource_handle(command.texture), candidate.resources.textures_,
                           [&](const auto handle) { return resources.share_texture(handle); });
      if (result != gneiss::error::success) {
        return result;
      }
    }
    std::size_t copied_payload_bytes =
        candidate.scene.instances.size() * sizeof(render_draw_instance) +
        candidate.ui.vertices().size() * sizeof(ui_vertex) +
        candidate.ui.indices().size() * sizeof(std::uint32_t) +
        candidate.ui.commands().size() * sizeof(ui_draw_command) +
        candidate.debug.lines().size() * sizeof(debug_line);
    copied_payload_bytes +=
        candidate.resources.meshes_.size() * sizeof(std::shared_ptr<const mesh_data>) +
        candidate.resources.materials_.size() * sizeof(std::shared_ptr<const material_data>) +
        candidate.resources.textures_.size() * sizeof(std::shared_ptr<const texture_data>);
    candidate.capture.capture_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - capture_started)
            .count();
    candidate.capture.copied_payload_bytes = copied_payload_bytes;
    out_packet = std::move(candidate);
    return gneiss::error::success;
  }
}

} // namespace gneiss
