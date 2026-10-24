// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_frame_packet.hpp"
#include "engine/core/result.hpp"

#include <chrono>
#include <new>
#include <utility>

namespace gneiss::engine::render {
namespace {

template <typename Handle, typename Resource, typename Getter>
gneiss::engine::core::result
capture_resource(Handle handle, std::unordered_map<Handle, std::shared_ptr<const Resource>>& output,
                 Getter&& getter, bool pending = false) {
  if (output.contains(handle)) {
    return gneiss::engine::core::result::success;
  }
  auto source = getter(handle);
  if (!source && !pending) {
    return gneiss::engine::core::result::invalid_handle;
  }
  output.emplace(handle, std::move(source));
  return gneiss::engine::core::result::success;
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

gneiss::engine::core::result capture_render_frame_packet(
    const engine::platform::native_window_info& window, engine::render::render_snapshot scene,
    const render_resource_service& resources, const ui_draw_list& ui, const debug_draw_list& debug,
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
          resources.mesh_status(instance.mesh) == gneiss::engine::core::result::not_ready);
      if (result != gneiss::engine::core::result::success) {
        return result;
      }
      result = capture_resource(
          instance.material, candidate.resources.materials_,
          [&](const auto handle) { return resources.share_material(handle); },
          resources.material_status(instance.material) == gneiss::engine::core::result::not_ready);
      if (result != gneiss::engine::core::result::success) {
        return result;
      }
      const auto& material = *candidate.resources.get_material(instance.material);
      for (const auto texture : material.texture_handles()) {
        if (texture == engine::core::null_rid)
          continue;
        result = capture_resource(
            texture, candidate.resources.textures_,
            [&](const auto handle) { return resources.share_texture(handle); },
            resources.texture_status(texture) == gneiss::engine::core::result::not_ready);
        if (result != gneiss::engine::core::result::success) {
          return result;
        }
      }
    }
    for (const auto& command : candidate.ui.commands()) {
      // UI 尚未接入占位策略，不能让场景中捕获的空纹理条目绕过 UI 就绪检查。
      if (resources.texture_status(command.texture) == gneiss::engine::core::result::not_ready) {
        return gneiss::engine::core::result::not_ready;
      }
      const auto result =
          capture_resource(command.texture, candidate.resources.textures_,
                           [&](const auto handle) { return resources.share_texture(handle); });
      if (result != gneiss::engine::core::result::success) {
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
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

} // namespace gneiss::engine::render
