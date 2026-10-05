// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_frame_packet.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

int main() {
  using namespace gneiss::render_internal;
  render_resource_service resources;
  std::vector<std::uint8_t> pixels(64U * 64U * 4U, 1U);
  gneiss::render_internal::texture_view texture_desc;
  texture_desc.width = 64U;
  texture_desc.height = 64U;
  texture_desc.row_stride_bytes = 64U * 4U;
  texture_desc.pixels = pixels;
  gneiss_texture texture = GNEISS_NULL_TEXTURE;
  if (resources.create_texture(texture_desc, &texture) != GNEISS_SUCCESS) {
    return 1;
  }

  constexpr std::array vertices{
      gneiss_mesh_vertex{.x = 0.0F, .y = 0.0F, .z = 0.0F, .u = 0.0F, .v = 0.0F},
      gneiss_mesh_vertex{.x = 1.0F, .y = 0.0F, .z = 0.0F, .u = 1.0F, .v = 0.0F},
      gneiss_mesh_vertex{.x = 0.0F, .y = 1.0F, .z = 0.0F, .u = 0.0F, .v = 1.0F}};
  gneiss::asset_internal::mesh_view mesh_desc;
  mesh_desc.vertices = vertices;
  gneiss_mesh mesh = GNEISS_NULL_MESH;
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_SUCCESS) {
    return 2;
  }

  material_resource material_desc;
  material_desc.base_color_texture = texture;
  std::array<gneiss_texture, 4> pbr_textures{};
  for (auto& handle : pbr_textures) {
    if (resources.create_texture(texture_desc, &handle) != GNEISS_SUCCESS)
      return 8;
  }
  material_desc.metallic_roughness_texture = pbr_textures[0];
  material_desc.normal_texture = pbr_textures[1];
  material_desc.occlusion_texture = pbr_textures[2];
  material_desc.emissive_texture = pbr_textures[3];
  gneiss_material material = GNEISS_NULL_MATERIAL;
  if (resources.create_material(material_desc, &material) != GNEISS_SUCCESS) {
    return 3;
  }

  gneiss::render_internal::render_snapshot scene;
  scene.has_camera = true;
  scene.instances.push_back(
      {.mesh = mesh, .material = material, .transform = GNEISS_TRANSFORM_IDENTITY});
  debug_draw_list debug;
  const std::array lines{gneiss_debug_line{.start = {0.0F, 0.0F, 0.0F},
                                           .end = {1.0F, 0.0F, 0.0F},
                                           .color_rgba8 = UINT32_C(0xffffffff),
                                           .width = 1.0F,
                                           .depth_test = 1U,
                                           .reserved = {}}};
  gneiss_debug_draw_list_desc debug_desc = GNEISS_DEBUG_DRAW_LIST_DESC_INIT;
  debug_desc.line_count = static_cast<std::uint32_t>(lines.size());
  debug_desc.lines = lines.data();
  if (debug.replace(debug_desc) != GNEISS_SUCCESS) {
    return 4;
  }

  gneiss::platform::native_window_info window;
  window.width = 640U;
  window.height = 480U;
  ui_draw_list ui;
  const auto* source_mesh = resources.get_mesh(mesh);
  const auto* source_material = resources.get_material(material);
  const auto* source_texture = resources.get_texture(texture);
  render_frame_packet packet;
  if (capture_render_frame_packet(window, std::move(scene), resources, ui, debug, packet) !=
      GNEISS_SUCCESS) {
    return 5;
  }
  debug.clear();
  if (resources.destroy_material(material) != GNEISS_SUCCESS ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS ||
      resources.destroy_texture(texture) != GNEISS_SUCCESS) {
    return 6;
  }
  for (const auto handle : pbr_textures) {
    if (resources.destroy_texture(handle) != GNEISS_SUCCESS ||
        packet.resources.get_texture(handle) == nullptr ||
        packet.resources.get_texture(handle)->levels.front().pixels.front() != std::byte{1})
      return 9;
  }
  const auto* captured_mesh = packet.resources.get_mesh(mesh);
  const auto* captured_material = packet.resources.get_material(material);
  const auto* captured_texture = packet.resources.get_texture(texture);
  if (packet.window.width != 640U || packet.scene.instances.size() != 1U ||
      packet.debug.lines().size() != 1U || captured_mesh == nullptr ||
      captured_mesh->vertices.size() != 3U || captured_material == nullptr ||
      captured_material->base_color_texture != texture || captured_texture == nullptr ||
      captured_texture->levels.front().pixels.front() != std::byte{1} ||
      packet.capture.capture_ms < 0.0F || packet.capture.copied_payload_bytes == 0U ||
      captured_mesh != source_mesh || captured_material != source_material ||
      captured_texture != source_texture || packet.capture.copied_payload_bytes >= pixels.size()) {
    return 7;
  }
  return 0;
}
