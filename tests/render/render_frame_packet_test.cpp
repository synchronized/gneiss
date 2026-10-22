// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_frame_packet.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
bool pending_resources() {
  using namespace gneiss::engine::render;
  using gneiss::engine::core::rid;
  render_resource_service resources;
  render_resource_service foreign;
  rid mesh{}, texture{}, material{};
  rid cancelled{};
  if (resources.reserve_mesh(nullptr) != GNEISS_ERROR_INVALID_ARGUMENT ||
      resources.reserve_texture(nullptr) != GNEISS_ERROR_INVALID_ARGUMENT ||
      resources.reserve_mesh(&cancelled) != GNEISS_SUCCESS ||
      resources.destroy_mesh(cancelled) != GNEISS_SUCCESS ||
      resources.mesh_status(cancelled) != GNEISS_ERROR_INVALID_HANDLE ||
      resources.destroy_mesh(cancelled) != GNEISS_ERROR_INVALID_HANDLE) {
    return false;
  }
  if (resources.reserve_mesh(&mesh) != GNEISS_SUCCESS ||
      resources.reserve_texture(&texture) != GNEISS_SUCCESS ||
      resources.mesh_status(mesh) != GNEISS_ERROR_NOT_READY ||
      resources.texture_status(texture) != GNEISS_ERROR_NOT_READY ||
      resources.mesh_status(0U) != GNEISS_ERROR_INVALID_HANDLE ||
      resources.mesh_status(texture) != GNEISS_ERROR_INVALID_HANDLE ||
      resources.texture_status(mesh) != GNEISS_ERROR_INVALID_HANDLE ||
      foreign.texture_status(texture) != GNEISS_ERROR_INVALID_HANDLE ||
      foreign.mesh_status(mesh) != GNEISS_ERROR_INVALID_HANDLE) {
    return false;
  }
  material_resource description;
  description.base_color_texture = mesh;
  if (resources.create_material(description, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return false;
  }
  description.base_color_texture = texture;
  if (resources.create_material(description, &material) != GNEISS_SUCCESS) {
    return false;
  }
  render_snapshot scene;
  scene.instances.push_back({.mesh = mesh, .material = material, .transform = {}});
  render_frame_packet before;
  if (capture_render_frame_packet({}, scene, resources, {}, {}, before) != GNEISS_SUCCESS ||
      !before.resources.mesh_pending(mesh) || !before.resources.texture_pending(texture) ||
      before.resources.mesh_pending(texture) || before.resources.texture_pending(0U)) {
    return false;
  }
  auto mesh_data = std::make_shared<mesh_resource>();
  mesh_data->vertices.resize(3U);
  auto texture_data = std::make_shared<texture_resource>();
  texture_data->width = texture_data->height = 1U;
  texture_data->levels.push_back(
      {.width = 1U,
       .height = 1U,
       .pixels = {std::byte{255}, std::byte{}, std::byte{}, std::byte{255}}});
  if (!resources.replace_mesh(mesh, mesh_data) ||
      !resources.replace_texture(texture, texture_data) ||
      resources.mesh_status(mesh) != GNEISS_SUCCESS ||
      resources.texture_status(texture) != GNEISS_SUCCESS) {
    return false;
  }
  render_frame_packet after;
  if (capture_render_frame_packet({}, scene, resources, {}, {}, after) != GNEISS_SUCCESS ||
      after.resources.mesh_pending(mesh) || after.resources.texture_pending(texture) ||
      after.resources.get_mesh(mesh) != mesh_data.get() ||
      after.resources.get_texture(texture) != texture_data.get() ||
      !before.resources.mesh_pending(mesh) || !before.resources.texture_pending(texture)) {
    return false;
  }
  if (resources.destroy_texture(texture) != GNEISS_SUCCESS ||
      resources.destroy_texture(texture) != GNEISS_ERROR_INVALID_HANDLE ||
      resources.texture_status(texture) != GNEISS_ERROR_INVALID_HANDLE) {
    return false;
  }
  render_frame_packet invalid;
  if (capture_render_frame_packet({}, scene, resources, {}, {}, invalid) !=
      GNEISS_ERROR_INVALID_HANDLE) {
    return false;
  }
  rid replacement{};
  if (resources.reserve_texture(&replacement) != GNEISS_SUCCESS || replacement == texture ||
      resources.replace_texture(texture, texture_data) ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS ||
      resources.destroy_mesh(mesh) != GNEISS_ERROR_INVALID_HANDLE ||
      resources.replace_mesh(mesh, mesh_data) ||
      resources.destroy_texture(replacement) != GNEISS_SUCCESS ||
      resources.destroy_material(material) != GNEISS_SUCCESS) {
    return false;
  }
  return resources.live_resource_count() == 0U &&
         after.resources.get_texture(texture) == texture_data.get();
}
} // namespace

int main() {
  if (!pending_resources()) {
    return 10;
  }
  using namespace gneiss::engine::render;
  render_resource_service resources;
  std::vector<std::uint8_t> pixels(64U * 64U * 4U, 1U);
  gneiss::engine::render::texture_view texture_desc;
  texture_desc.width = 64U;
  texture_desc.height = 64U;
  texture_desc.row_stride_bytes = 64U * 4U;
  texture_desc.pixels = pixels;
  gneiss::engine::core::rid texture = gneiss::engine::core::null_rid;
  if (resources.create_texture(texture_desc, &texture) != GNEISS_SUCCESS) {
    return 1;
  }

  constexpr std::array vertices{
      gneiss::engine::resource::mesh_vertex{.x = 0.0F, .y = 0.0F, .z = 0.0F, .u = 0.0F, .v = 0.0F},
      gneiss::engine::resource::mesh_vertex{.x = 1.0F, .y = 0.0F, .z = 0.0F, .u = 1.0F, .v = 0.0F},
      gneiss::engine::resource::mesh_vertex{.x = 0.0F, .y = 1.0F, .z = 0.0F, .u = 0.0F, .v = 1.0F}};
  gneiss::engine::resource::mesh_view mesh_desc;
  mesh_desc.vertices = vertices;
  gneiss::engine::core::rid mesh = gneiss::engine::core::null_rid;
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_SUCCESS) {
    return 2;
  }

  material_resource material_desc;
  material_desc.base_color_texture = texture;
  std::array<gneiss::engine::core::rid, 4> pbr_textures{};
  for (auto& handle : pbr_textures) {
    if (resources.create_texture(texture_desc, &handle) != GNEISS_SUCCESS)
      return 8;
  }
  material_desc.metallic_roughness_texture = pbr_textures[0];
  material_desc.normal_texture = pbr_textures[1];
  material_desc.occlusion_texture = pbr_textures[2];
  material_desc.emissive_texture = pbr_textures[3];
  gneiss::engine::core::rid material = gneiss::engine::core::null_rid;
  if (resources.create_material(material_desc, &material) != GNEISS_SUCCESS) {
    return 3;
  }

  gneiss::engine::render::render_snapshot scene;
  scene.has_camera = true;
  scene.instances.push_back({.mesh = mesh, .material = material, .transform = {}});
  debug_draw_list debug;
  const std::array lines{gneiss::engine::render::debug_line{.start = {0.0F, 0.0F, 0.0F},
                                                            .end = {1.0F, 0.0F, 0.0F},
                                                            .color_rgba8 = UINT32_C(0xffffffff),
                                                            .width = 1.0F,
                                                            .depth_test = true}};
  if (debug.replace(lines) != GNEISS_SUCCESS) {
    return 4;
  }

  gneiss::engine::platform::native_window_info window;
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
