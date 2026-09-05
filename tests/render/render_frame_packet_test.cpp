// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_frame_packet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

int main() {
  using namespace gneiss::render_internal;
  render_resource_service resources;
  std::vector<std::uint8_t> pixels(64U * 64U * 4U, 1U);
  gneiss_texture_desc texture_desc = GNEISS_TEXTURE_DESC_INIT;
  texture_desc.width = 64U;
  texture_desc.height = 64U;
  texture_desc.row_stride_bytes = 64U * 4U;
  texture_desc.pixel_data_size = pixels.size();
  texture_desc.pixels = pixels.data();
  gneiss_texture texture = GNEISS_NULL_TEXTURE;
  if (resources.create_texture(texture_desc, &texture) != GNEISS_SUCCESS) {
    return 1;
  }

  constexpr std::array vertices{
      gneiss_mesh_vertex{.x = 0.0F, .y = 0.0F, .z = 0.0F, .u = 0.0F, .v = 0.0F},
      gneiss_mesh_vertex{.x = 1.0F, .y = 0.0F, .z = 0.0F, .u = 1.0F, .v = 0.0F},
      gneiss_mesh_vertex{.x = 0.0F, .y = 1.0F, .z = 0.0F, .u = 0.0F, .v = 1.0F}};
  gneiss_mesh_desc mesh_desc = GNEISS_MESH_DESC_INIT;
  mesh_desc.vertex_count = static_cast<std::uint32_t>(vertices.size());
  mesh_desc.vertices = vertices.data();
  gneiss_mesh mesh = GNEISS_NULL_MESH;
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_SUCCESS) {
    return 2;
  }

  gneiss_material_desc material_desc = GNEISS_MATERIAL_DESC_INIT;
  material_desc.base_color_texture = texture;
  gneiss_material material = GNEISS_NULL_MATERIAL;
  if (resources.create_material(material_desc, &material) != GNEISS_SUCCESS) {
    return 3;
  }

  gneiss::world_internal::render_snapshot scene;
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

  gneiss::application_internal::native_window_info window;
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
  const auto* captured_mesh = packet.resources.get_mesh(mesh);
  const auto* captured_material = packet.resources.get_material(material);
  const auto* captured_texture = packet.resources.get_texture(texture);
  if (packet.window.width != 640U || packet.scene.instances.size() != 1U ||
      packet.debug.lines().size() != 1U || captured_mesh == nullptr ||
      captured_mesh->vertices.size() != 3U || captured_material == nullptr ||
      captured_material->base_color_texture != texture || captured_texture == nullptr ||
      captured_texture->pixels.front() != std::byte{1} || packet.capture.capture_ms < 0.0F ||
      packet.capture.copied_payload_bytes == 0U || captured_mesh != source_mesh ||
      captured_material != source_material || captured_texture != source_texture ||
      packet.capture.copied_payload_bytes >= pixels.size()) {
    return 7;
  }
  return 0;
}
