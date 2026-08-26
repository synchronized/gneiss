// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>
#include <gneiss/scene.h>

#include <array>
#include <string_view>

int main() {
  constexpr std::string_view title = "Gneiss Granit Platform Smoke Test";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.window_width = 320;
  desc.window_height = 240;

  gneiss::application application;
  const auto create_result = gneiss::application::create(desc, application);
  if (create_result != gneiss::result::success) {
    return 1;
  }

  constexpr std::array vertices{gneiss::mesh_vertex{-0.6F, -0.5F, 0.0F},
                                gneiss::mesh_vertex{0.6F, -0.5F, 0.0F},
                                gneiss::mesh_vertex{0.0F, 0.6F, 0.0F}};
  gneiss::mesh_desc mesh_desc = GNEISS_MESH_DESC_INIT;
  mesh_desc.vertices = vertices.data();
  mesh_desc.vertex_count = static_cast<std::uint32_t>(vertices.size());
  gneiss::material_desc material_desc = GNEISS_MATERIAL_DESC_INIT;
  material_desc.red = 0.95F;
  material_desc.green = 0.35F;
  material_desc.blue = 0.12F;
  gneiss::mesh_id mesh;
  gneiss::material_id material;
  if (application.create_mesh(mesh_desc, mesh) != gneiss::result::success ||
      application.create_material(material_desc, material) != gneiss::result::success) {
    return 2;
  }

  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_entity_id camera_entity = GNEISS_NULL_ENTITY_ID;
  gneiss_entity_id mesh_entity = GNEISS_NULL_ENTITY_ID;
  gneiss_scene_node_id camera_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id mesh_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_camera camera = GNEISS_CAMERA_INIT;
  const gneiss_mesh_renderer mesh_renderer{.mesh = mesh.get(), .material = material.get()};
  gneiss_transform camera_transform = GNEISS_TRANSFORM_IDENTITY;
  camera_transform.translation[2] = 2.0F;
  if (application.get_world(world) != gneiss::result::success ||
      gneiss_world_entity_create(world, &camera_entity) != GNEISS_SUCCESS ||
      gneiss_world_entity_create(world, &mesh_entity) != GNEISS_SUCCESS ||
      gneiss_scene_node_create(world, GNEISS_NULL_SCENE_NODE_ID, camera_entity, &camera_node) !=
          GNEISS_SUCCESS ||
      gneiss_scene_node_create(world, GNEISS_NULL_SCENE_NODE_ID, mesh_entity, &mesh_node) !=
          GNEISS_SUCCESS ||
      gneiss_scene_node_set_local_transform(world, camera_node, &camera_transform) !=
          GNEISS_SUCCESS ||
      gneiss_world_entity_set_camera(world, camera_entity, &camera) != GNEISS_SUCCESS ||
      gneiss_world_entity_set_mesh_renderer(world, mesh_entity, &mesh_renderer) != GNEISS_SUCCESS) {
    return 3;
  }
  const auto run_result = application.run(3);
  if (run_result != gneiss::result::success) {
    return 4;
  }
  if (application.destroy_mesh(mesh) != gneiss::result::success ||
      application.destroy_material(material) != gneiss::result::success) {
    return 5;
  }
  return 0;
}
