// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>
#include <gneiss/scene.h>

#include <array>
#include <cstdio>
#include <string_view>

namespace {

struct smoke_context final {
  gneiss_texture texture = GNEISS_NULL_TEXTURE;
};

gneiss_result submit_ui(gneiss_application application, const gneiss_frame_time* /*time*/,
                        void* user_data) {
  const auto& context = *static_cast<const smoke_context*>(user_data);
  constexpr std::array vertices{
      gneiss_ui_vertex{{16.0F, 16.0F}, {0.0F, 0.0F}, UINT32_C(0xccffffff)},
      gneiss_ui_vertex{{144.0F, 16.0F}, {1.0F, 0.0F}, UINT32_C(0xccffffff)},
      gneiss_ui_vertex{{144.0F, 64.0F}, {1.0F, 1.0F}, UINT32_C(0xccffffff)},
      gneiss_ui_vertex{{16.0F, 64.0F}, {0.0F, 1.0F}, UINT32_C(0xccffffff)}};
  constexpr std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 2U, 3U, 0U};
  const std::array commands{
      gneiss_ui_draw_command{.texture = context.texture,
                             .clip_min = {16.0F, 16.0F},
                             .clip_max = {144.0F, 64.0F},
                             .first_index = 0U,
                             .index_count = static_cast<std::uint32_t>(indices.size()),
                             .vertex_offset = 0U,
                             .reserved = 0U}};
  gneiss_ui_draw_list_desc draw_list = GNEISS_UI_DRAW_LIST_DESC_INIT;
  draw_list.display_width = 320.0F;
  draw_list.display_height = 240.0F;
  draw_list.vertex_count = static_cast<std::uint32_t>(vertices.size());
  draw_list.vertices = vertices.data();
  draw_list.index_count = static_cast<std::uint32_t>(indices.size());
  draw_list.indices = indices.data();
  draw_list.command_count = static_cast<std::uint32_t>(commands.size());
  draw_list.commands = commands.data();
  return gneiss_application_submit_ui_draw_list(application, &draw_list);
}

} // namespace

int main() {
  constexpr std::string_view title = "Gneiss Granit Platform Smoke Test";
  smoke_context context;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &context;
  desc.update = submit_ui;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.window_width = 320;
  desc.window_height = 240;

  gneiss::application application;
  const auto create_result = gneiss::application::create(desc, application);
  if (create_result != gneiss::result::success) {
    std::fprintf(stderr, "Application 创建失败：%d\n", gneiss::to_native(create_result));
    return 1;
  }

  constexpr std::array vertices{
      gneiss::mesh_vertex{.x = -0.6F, .y = -0.5F, .z = 0.0F, .u = 0.0F, .v = 0.0F},
      gneiss::mesh_vertex{.x = 0.6F, .y = -0.5F, .z = 0.0F, .u = 1.0F, .v = 0.0F},
      gneiss::mesh_vertex{.x = 0.0F, .y = 0.6F, .z = 0.0F, .u = 0.5F, .v = 1.0F}};
  gneiss::mesh_desc mesh_desc = GNEISS_MESH_DESC_INIT;
  mesh_desc.vertices = vertices.data();
  mesh_desc.vertex_count = static_cast<std::uint32_t>(vertices.size());
  gneiss::material_desc material_desc = GNEISS_MATERIAL_DESC_INIT;
  material_desc.red = 0.95F;
  material_desc.green = 0.35F;
  material_desc.blue = 0.12F;
  constexpr std::array<std::uint8_t, 16> pixels{255, 255, 255, 255, 32,  64,  255, 255,
                                                32,  64,  255, 255, 255, 255, 255, 255};
  gneiss::texture_desc texture_desc = GNEISS_TEXTURE_DESC_INIT;
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.row_stride_bytes = 8;
  texture_desc.pixel_data_size = pixels.size();
  texture_desc.pixels = pixels.data();
  gneiss::mesh_id mesh;
  gneiss::material_id material;
  gneiss::material_id plain_material;
  gneiss::texture_id texture;
  if (application.create_texture(texture_desc, texture) != gneiss::result::success) {
    std::fprintf(stderr, "测试 Texture 创建失败\n");
    return 2;
  }
  context.texture = texture.get();
  if (application.create_mesh(mesh_desc, mesh) != gneiss::result::success ||
      application.create_material(material_desc, plain_material) != gneiss::result::success) {
    std::fprintf(stderr, "测试基础资源创建失败\n");
    return 2;
  }
  material_desc.base_color_texture = texture.get();
  if (application.create_material(material_desc, material) != gneiss::result::success) {
    std::fprintf(stderr, "测试资源创建失败\n");
    return 2;
  }

  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_entity_id camera_entity = GNEISS_NULL_ENTITY_ID;
  gneiss_entity_id mesh_entity = GNEISS_NULL_ENTITY_ID;
  gneiss_entity_id plain_mesh_entity = GNEISS_NULL_ENTITY_ID;
  gneiss_scene_node_id camera_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id mesh_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id plain_mesh_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_camera camera = GNEISS_CAMERA_INIT;
  gneiss_mesh_renderer mesh_renderer{.mesh = mesh.get(), .material = material.get()};
  gneiss_mesh_renderer plain_mesh_renderer{.mesh = mesh.get(), .material = plain_material.get()};
  gneiss_transform camera_transform = GNEISS_TRANSFORM_IDENTITY;
  camera_transform.translation[2] = 2.0F;
  gneiss_transform near_transform = GNEISS_TRANSFORM_IDENTITY;
  near_transform.translation[0] = -0.55F;
  near_transform.translation[2] = 0.5F;
  gneiss_transform plain_transform = GNEISS_TRANSFORM_IDENTITY;
  plain_transform.translation[0] = 0.55F;
  plain_transform.translation[2] = 0.5F;
  if (application.get_world(world) != gneiss::result::success ||
      gneiss_world_entity_create(world, &camera_entity) != GNEISS_SUCCESS ||
      gneiss_world_entity_create(world, &mesh_entity) != GNEISS_SUCCESS ||
      gneiss_world_entity_create(world, &plain_mesh_entity) != GNEISS_SUCCESS ||
      gneiss_scene_node_create(world, GNEISS_NULL_SCENE_NODE_ID, camera_entity, &camera_node) !=
          GNEISS_SUCCESS ||
      gneiss_scene_node_create(world, GNEISS_NULL_SCENE_NODE_ID, mesh_entity, &mesh_node) !=
          GNEISS_SUCCESS ||
      gneiss_scene_node_create(world, GNEISS_NULL_SCENE_NODE_ID, plain_mesh_entity,
                               &plain_mesh_node) != GNEISS_SUCCESS ||
      gneiss_scene_node_set_local_transform(world, camera_node, &camera_transform) !=
          GNEISS_SUCCESS ||
      gneiss_scene_node_set_local_transform(world, mesh_node, &near_transform) != GNEISS_SUCCESS ||
      gneiss_scene_node_set_local_transform(world, plain_mesh_node, &plain_transform) !=
          GNEISS_SUCCESS ||
      gneiss_world_entity_set_camera(world, camera_entity, &camera) != GNEISS_SUCCESS ||
      gneiss_world_entity_set_mesh_renderer(world, mesh_entity, &mesh_renderer) != GNEISS_SUCCESS ||
      gneiss_world_entity_set_mesh_renderer(world, plain_mesh_entity, &plain_mesh_renderer) !=
          GNEISS_SUCCESS) {
    std::fprintf(stderr, "测试场景构造失败\n");
    return 3;
  }
  auto run_result = application.run(2);
  if (run_result != gneiss::result::success) {
    std::fprintf(stderr, "Application 运行失败：%d\n", gneiss::to_native(run_result));
    return 4;
  }
  gneiss::mesh_id replacement_mesh;
  if (application.destroy_mesh(mesh) != gneiss::result::success ||
      application.create_mesh(mesh_desc, replacement_mesh) != gneiss::result::success) {
    return 5;
  }
  mesh_renderer.mesh = replacement_mesh.get();
  plain_mesh_renderer.mesh = replacement_mesh.get();
  if (gneiss_world_entity_set_mesh_renderer(world, mesh_entity, &mesh_renderer) != GNEISS_SUCCESS ||
      gneiss_world_entity_set_mesh_renderer(world, plain_mesh_entity, &plain_mesh_renderer) !=
          GNEISS_SUCCESS) {
    return 6;
  }
  run_result = application.run(1);
  if (run_result != gneiss::result::success) {
    std::fprintf(stderr, "Mesh generation 更新后运行失败：%d\n", gneiss::to_native(run_result));
    return 7;
  }
  if (application.destroy_mesh(replacement_mesh) != gneiss::result::success ||
      application.destroy_material(material) != gneiss::result::success ||
      application.destroy_material(plain_material) != gneiss::result::success ||
      application.destroy_texture(texture) != gneiss::result::success) {
    return 8;
  }
  return 0;
}
