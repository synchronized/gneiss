// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>
#include <gneiss/scene.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace {

struct example_state {
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_scene_node_id triangle_node = GNEISS_NULL_SCENE_NODE_ID;
};

gneiss_result update_triangle(gneiss_application /*application*/, const gneiss_frame_time* time,
                              void* user_data) {
  if (time == nullptr || user_data == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  const auto* state = static_cast<const example_state*>(user_data);
  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  const auto angle = static_cast<double>(time->elapsed_ns) / nanoseconds_per_second;
  const auto half_angle = angle * 0.5;
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.rotation[2] = static_cast<float>(std::sin(half_angle));
  transform.rotation[3] = static_cast<float>(std::cos(half_angle));
  return gneiss_scene_node_set_local_transform(state->world, state->triangle_node, &transform);
}

bool create_scene(gneiss::application& application, example_state& state, gneiss::mesh_id mesh,
                  gneiss::material_id material) {
  gneiss_entity_id camera_entity = GNEISS_NULL_ENTITY_ID;
  gneiss_entity_id triangle_entity = GNEISS_NULL_ENTITY_ID;
  gneiss_scene_node_id camera_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_camera camera = GNEISS_CAMERA_INIT;
  const gneiss_mesh_renderer renderer{.mesh = mesh.get(), .material = material.get()};
  gneiss_transform camera_transform = GNEISS_TRANSFORM_IDENTITY;
  camera_transform.translation[2] = 2.0F;

  return application.get_world(state.world) == gneiss::result::success &&
         gneiss_world_entity_create(state.world, &camera_entity) == GNEISS_SUCCESS &&
         gneiss_world_entity_create(state.world, &triangle_entity) == GNEISS_SUCCESS &&
         gneiss_scene_node_create(state.world, GNEISS_NULL_SCENE_NODE_ID, camera_entity,
                                  &camera_node) == GNEISS_SUCCESS &&
         gneiss_scene_node_create(state.world, GNEISS_NULL_SCENE_NODE_ID, triangle_entity,
                                  &state.triangle_node) == GNEISS_SUCCESS &&
         gneiss_scene_node_set_local_transform(state.world, camera_node, &camera_transform) ==
             GNEISS_SUCCESS &&
         gneiss_world_entity_set_camera(state.world, camera_entity, &camera) == GNEISS_SUCCESS &&
         gneiss_world_entity_set_mesh_renderer(state.world, triangle_entity, &renderer) ==
             GNEISS_SUCCESS;
}

} // namespace

int main() {
  example_state state;
  constexpr std::string_view title = "Gneiss Triangle";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = update_triangle;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());

  gneiss::application application;
  if (gneiss::application::create(desc, application) != gneiss::result::success) {
    return 1;
  }

  constexpr std::array vertices{gneiss::mesh_vertex{.x = -0.6F, .y = -0.5F, .z = 0.0F},
                                gneiss::mesh_vertex{.x = 0.6F, .y = -0.5F, .z = 0.0F},
                                gneiss::mesh_vertex{.x = 0.0F, .y = 0.6F, .z = 0.0F}};
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
      application.create_material(material_desc, material) != gneiss::result::success ||
      !create_scene(application, state, mesh, material)) {
    return 2;
  }

  if (application.run() != gneiss::result::success) {
    return 3;
  }
  if (application.destroy_mesh(mesh) != gneiss::result::success ||
      application.destroy_material(material) != gneiss::result::success) {
    return 4;
  }
  return 0;
}
