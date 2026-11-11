// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/render/render_frame_packet.hpp"
namespace gneiss {
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

} // namespace gneiss
