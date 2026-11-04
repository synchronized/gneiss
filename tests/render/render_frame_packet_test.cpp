// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/function/render/render_frame_packet.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/resource/detail/resource_service_impl.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
bool pending_resources() {
  using namespace gneiss::engine;
  using gneiss::engine::rid;
  render_service resources;
  gneiss::engine::resource_service::implementation resources_factory{resources};
  render_service foreign;

  rid mesh{}, texture{}, material{};
  auto reserved = resources.reserve_mesh();
  if (!reserved || *reserved == null_rid)
    return false;
  const auto cancelled = *reserved;
  render_service closed;
  closed.close_resources();
  auto no_mesh = closed.reserve_mesh();
  auto no_texture = closed.reserve_texture();
  auto no_material = closed.reserve_material();
  if (no_mesh || no_texture || no_material || no_mesh.error() != error::invalid_argument ||
      no_texture.error() != error::invalid_argument ||
      no_material.error() != error::invalid_argument)
    return false;
  if (resources.destroy_mesh(cancelled) != gneiss::engine::error::success ||
      resources.mesh_status(cancelled) != gneiss::engine::error::invalid_handle ||
      resources.destroy_mesh(cancelled) != gneiss::engine::error::invalid_handle) {
    return false;
  }
  gneiss::engine::mesh mesh_owner;
  gneiss::engine::texture texture_owner;
  gneiss::engine::material material_owner;
  if (gneiss::test_support::take_value(resources_factory.reserve_mesh(), mesh_owner).failed() ||
      gneiss::test_support::take_value(resources_factory.reserve_texture(), texture_owner).failed())
    return false;
  mesh = resource_handle(mesh_owner);
  texture = resource_handle(texture_owner);
  if (resources.mesh_status(mesh) != gneiss::engine::error::not_ready ||
      resources.texture_status(texture) != gneiss::engine::error::not_ready ||
      resources.mesh_status(0U) != gneiss::engine::error::invalid_handle ||
      resources.mesh_status(texture) != gneiss::engine::error::invalid_handle ||
      resources.texture_status(mesh) != gneiss::engine::error::invalid_handle ||
      foreign.texture_status(texture) != gneiss::engine::error::invalid_handle ||
      foreign.mesh_status(mesh) != gneiss::engine::error::invalid_handle) {
    return false;
  }
  auto texture_ref = resources_factory.find_texture(texture);
  material_data description;
  description.base_color_texture = mesh;
  if (gneiss::test_support::assign_value(resources.create_material(description), material) !=
      gneiss::engine::error::invalid_argument) {
    return false;
  }
  description.base_color_texture = texture;
  if (gneiss::test_support::take_value(
          resources_factory.create_material(description.parameters, {texture_owner}),
          material_owner) != gneiss::engine::error::success) {
    return false;
  }
  material = resource_handle(material_owner);
  render_snapshot scene;
  scene.instances.push_back({.mesh = resources_factory.find_mesh(mesh),
                             .material = resources_factory.find_material(material),
                             .transform = {}});
  gneiss::engine::material pending_owner;
  if (gneiss::test_support::take_value(resources_factory.reserve_material(), pending_owner)
          .failed())
    return false;
  const auto pending_material = resource_handle(pending_owner);
  if (resources.material_status(pending_material) != gneiss::engine::error::not_ready ||
      foreign.material_status(pending_material) != gneiss::engine::error::invalid_handle ||
      resources.material_status(mesh) != gneiss::engine::error::invalid_handle) {
    return false;
  }
  auto pending_scene = scene;
  pending_scene.instances.front().material = resources_factory.find_material(pending_material);
  render_frame_packet fallback;
  if (capture_render_frame_packet({}, pending_scene, resources, {}, {}, fallback) !=
          gneiss::engine::error::success ||
      fallback.resources.get_material(pending_material) == nullptr ||
      fallback.resources.get_material(0U) != nullptr) {
    return false;
  }
  const auto* neutral = fallback.resources.get_material(pending_material);
  auto material_data = std::make_shared<gneiss::engine::material_data>(description);
  if (!resources.replace_material(pending_material, material_data) ||
      resources.material_status(pending_material) != gneiss::engine::error::success) {
    return false;
  }
  render_frame_packet filled;
  if (capture_render_frame_packet({}, pending_scene, resources, {}, {}, filled) !=
          gneiss::engine::error::success ||
      filled.resources.get_material(pending_material) != material_data.get() ||
      fallback.resources.get_material(pending_material) != neutral ||
      neutral->base_color_texture != 0U ||
      resources.destroy_material(pending_material) != gneiss::engine::error::success ||
      resources.destroy_material(pending_material) != gneiss::engine::error::invalid_handle ||
      resources.material_status(pending_material) != gneiss::engine::error::invalid_handle ||
      resources.replace_material(pending_material, material_data)) {
    return false;
  }
  render_frame_packet before;
  if (capture_render_frame_packet({}, scene, resources, {}, {}, before) !=
          gneiss::engine::error::success ||
      !before.resources.mesh_pending(mesh) || !before.resources.texture_pending(texture) ||
      before.resources.mesh_pending(texture) || before.resources.texture_pending(0U)) {
    return false;
  }
  auto mesh_data = std::make_shared<gneiss::engine::mesh_data>();
  mesh_data->vertices.resize(3U);
  auto texture_data = std::make_shared<gneiss::engine::texture_data>();
  texture_data->width = texture_data->height = 1U;
  texture_data->levels.push_back(
      {.width = 1U,
       .height = 1U,
       .pixels = {std::byte{255}, std::byte{}, std::byte{}, std::byte{255}}});
  if (!resources.replace_mesh(mesh, mesh_data) ||
      !resources.replace_texture(texture, texture_data) ||
      resources.mesh_status(mesh) != gneiss::engine::error::success ||
      resources.texture_status(texture) != gneiss::engine::error::success) {
    return false;
  }
  render_frame_packet after;
  if (capture_render_frame_packet({}, scene, resources, {}, {}, after) !=
          gneiss::engine::error::success ||
      after.resources.mesh_pending(mesh) || after.resources.texture_pending(texture) ||
      after.resources.get_mesh(mesh) != mesh_data.get() ||
      after.resources.get_texture(texture) != texture_data.get() ||
      !before.resources.mesh_pending(mesh) || !before.resources.texture_pending(texture)) {
    return false;
  }
  if (resources.destroy_texture(texture) != gneiss::engine::error::success ||
      resources.destroy_texture(texture) != gneiss::engine::error::invalid_handle ||
      resources.texture_status(texture) != gneiss::engine::error::invalid_handle) {
    return false;
  }
  render_frame_packet invalid;
  if (capture_render_frame_packet({}, scene, resources, {}, {}, invalid) !=
      gneiss::engine::error::invalid_handle) {
    return false;
  }
  rid replacement{};
  if (gneiss::test_support::assign_value(resources.reserve_texture(), replacement) !=
          gneiss::engine::error::success ||
      replacement == texture || resources.replace_texture(texture, texture_data) ||
      resources.destroy_mesh(mesh) != gneiss::engine::error::success ||
      resources.destroy_mesh(mesh) != gneiss::engine::error::invalid_handle ||
      resources.replace_mesh(mesh, mesh_data) ||
      resources.destroy_texture(replacement) != gneiss::engine::error::success ||
      resources.destroy_material(material) != gneiss::engine::error::success) {
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
  using namespace gneiss::engine;
  render_service resources;
  gneiss::engine::resource_service::implementation resources_factory{resources};
  std::vector<std::uint8_t> pixels(64U * 64U * 4U, 1U);
  gneiss::engine::texture_view texture_desc;
  texture_desc.width = 64U;
  texture_desc.height = 64U;
  texture_desc.row_stride_bytes = 64U * 4U;
  texture_desc.pixels = pixels;
  gneiss::engine::texture texture_owner;
  if (gneiss::test_support::take_value(resources_factory.create_texture(texture_desc),
                                       texture_owner) != gneiss::engine::error::success) {
    return 1;
  }

  const auto texture = resource_handle(texture_owner);
  constexpr std::array vertices{
      gneiss::engine::mesh_vertex{.position = {0.0F, 0.0F, 0.0F}, .uv = {0.0F, 0.0F}},
      gneiss::engine::mesh_vertex{.position = {1.0F, 0.0F, 0.0F}, .uv = {1.0F, 0.0F}},
      gneiss::engine::mesh_vertex{.position = {0.0F, 1.0F, 0.0F}, .uv = {0.0F, 1.0F}}};
  gneiss::engine::mesh_view mesh_desc;
  mesh_desc.vertices = vertices;
  gneiss::engine::mesh mesh_owner;
  if (gneiss::test_support::take_value(resources_factory.create_mesh(mesh_desc), mesh_owner) !=
      gneiss::engine::error::success) {
    return 2;
  }

  const auto mesh = resource_handle(mesh_owner);
  auto texture_ref = resources_factory.find_texture(texture);
  std::array<gneiss::engine::texture, 4> pbr_refs;
  material_data material_desc;
  material_desc.base_color_texture = texture;
  std::array<gneiss::engine::rid, 4> pbr_textures{};
  for (std::size_t i = 0; i < pbr_refs.size(); ++i) {
    if (gneiss::test_support::take_value(resources_factory.create_texture(texture_desc),
                                         pbr_refs[i])
            .failed())
      return 8;
    pbr_textures[i] = resource_handle(pbr_refs[i]);
  }
  material_desc.metallic_roughness_texture = pbr_textures[0];
  material_desc.normal_texture = pbr_textures[1];
  material_desc.occlusion_texture = pbr_textures[2];
  material_desc.emissive_texture = pbr_textures[3];
  gneiss::engine::material material_owner;
  if (gneiss::test_support::take_value(
          resources_factory.create_material(
              material_desc.parameters,
              {texture_owner, pbr_refs[0], pbr_refs[1], pbr_refs[2], pbr_refs[3]}),
          material_owner) != gneiss::engine::error::success) {
    return 3;
  }

  const auto material = resource_handle(material_owner);
  gneiss::engine::render_snapshot scene;
  scene.has_camera = true;
  scene.instances.push_back({.mesh = resources_factory.find_mesh(mesh),
                             .material = resources_factory.find_material(material),
                             .transform = {}});
  debug_draw_list debug;
  const std::array lines{gneiss::engine::debug_line{.start = {0.0F, 0.0F, 0.0F},
                                                    .end = {1.0F, 0.0F, 0.0F},
                                                    .color_rgba8 = UINT32_C(0xffffffff),
                                                    .width = 1.0F,
                                                    .depth_test = true}};
  if (debug.replace(lines) != gneiss::engine::error::success) {
    return 4;
  }

  gneiss::engine::native_window_info window;
  window.width = 640U;
  window.height = 480U;
  ui_draw_list ui;
  const auto* source_mesh = resources.get_mesh(mesh);
  const auto* source_material = resources.get_material(material);
  const auto* source_texture = resources.get_texture(texture);
  render_frame_packet packet;
  if (capture_render_frame_packet(window, std::move(scene), resources, ui, debug, packet) !=
      gneiss::engine::error::success) {
    return 5;
  }
  debug.clear();
  if (resources.destroy_material(material) != gneiss::engine::error::success ||
      resources.destroy_mesh(mesh) != gneiss::engine::error::success ||
      resources.destroy_texture(texture) != gneiss::engine::error::success) {
    return 6;
  }
  for (const auto handle : pbr_textures) {
    if (resources.destroy_texture(handle) != gneiss::engine::error::success ||
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
