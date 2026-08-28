// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.h>
#include <gneiss/render.h>
#include <gneiss/scene.h>
#include <gneiss/world.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

gneiss_application create_application(std::string_view asset_root) {
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss_application application = GNEISS_NULL_APPLICATION;
  return gneiss_application_create(&desc, &application) == GNEISS_SUCCESS ? application
                                                                          : GNEISS_NULL_APPLICATION;
}

} // namespace

int main() try {
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view failure_root = GNEISS_TEST_FAILURE_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
  constexpr std::string_view missing_uri = "asset://scenes/missing-asset.scene.json";
  constexpr std::string_view triangle_uuid = "00000000-0000-4000-8000-000000000003";
  constexpr std::string_view camera_uuid = "00000000-0000-4000-8000-000000000002";
  constexpr std::string_view created_uuid = "00000000-0000-4000-8000-000000000004";
  constexpr std::string_view generic_uuid = "00000000-0000-4000-8000-000000000005";
  constexpr std::string_view mesh_uri = "asset://models/triangle.mesh.json";
  constexpr std::string_view material_uri = "asset://materials/triangle.material.json";
  constexpr std::string_view missing_mesh_uri = "asset://models/missing.mesh.json";
  const auto application = create_application(asset_root);
  const auto second_application = create_application(asset_root);
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  gneiss_scene_node_id triangle = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id camera_node = GNEISS_NULL_SCENE_NODE_ID;
  std::uint64_t entity_count = 0;
  std::uint64_t node_count = 0;
  if (application == GNEISS_NULL_APPLICATION || second_application == GNEISS_NULL_APPLICATION ||
      gneiss_application_get_world(application, &world) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(application, scene_uri.data(), scene_uri.size(), &scene) !=
          GNEISS_SUCCESS ||
      scene == GNEISS_NULL_SCENE_INSTANCE ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 2U ||
      gneiss_scene_instance_find_node(application, scene, triangle_uuid.data(),
                                      triangle_uuid.size(), &triangle) != GNEISS_SUCCESS ||
      triangle == GNEISS_NULL_SCENE_NODE_ID ||
      gneiss_scene_instance_find_node(application, scene, camera_uuid.data(), camera_uuid.size(),
                                      &camera_node) != GNEISS_SUCCESS) {
    return 1;
  }
  gneiss_scene_instance_node_info camera_info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  gneiss_scene_instance_node_info triangle_info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  if (gneiss_scene_instance_get_node_count(application, scene, &node_count) != GNEISS_SUCCESS ||
      node_count != 2U ||
      gneiss_scene_instance_get_node_info(application, scene, 0U, &camera_info) != GNEISS_SUCCESS ||
      camera_info.node != camera_node || camera_info.parent != GNEISS_NULL_SCENE_NODE_ID ||
      std::string_view{camera_info.uuid, camera_info.uuid_length} != camera_uuid ||
      std::string_view{camera_info.name, camera_info.name_length} != "Camera" ||
      camera_info.component_flags != GNEISS_SCENE_NODE_COMPONENT_CAMERA ||
      gneiss_scene_instance_get_node_info(application, scene, 1U, &triangle_info) !=
          GNEISS_SUCCESS ||
      triangle_info.node != triangle || triangle_info.parent != camera_node ||
      std::string_view{triangle_info.uuid, triangle_info.uuid_length} != triangle_uuid ||
      triangle_info.name != nullptr || triangle_info.name_length != 0U ||
      std::string_view{triangle_info.mesh_uri, triangle_info.mesh_uri_length} != mesh_uri ||
      std::string_view{triangle_info.material_uri, triangle_info.material_uri_length} !=
          material_uri ||
      triangle_info.component_flags != GNEISS_SCENE_NODE_COMPONENT_MESH_RENDERER ||
      gneiss_scene_instance_get_node_info(application, scene, 2U, &triangle_info) !=
          GNEISS_ERROR_NOT_FOUND ||
      gneiss_scene_instance_get_node_count(second_application, scene, &node_count) !=
          GNEISS_ERROR_INVALID_HANDLE) {
    return 2;
  }
  constexpr char legacy_sentinel[] = "sentinel";
  auto legacy_info = triangle_info;
  legacy_info.struct_size = GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_1_SIZE;
  legacy_info.mesh_uri = legacy_sentinel;
  if (gneiss_scene_instance_get_node_info(application, scene, 1U, &legacy_info) != GNEISS_SUCCESS ||
      legacy_info.mesh_uri != legacy_sentinel) {
    return 3;
  }
  gneiss_scene_mesh_renderer_node_desc create_desc = GNEISS_SCENE_MESH_RENDERER_NODE_DESC_INIT;
  create_desc.parent = camera_node;
  create_desc.uuid = created_uuid.data();
  create_desc.uuid_length = created_uuid.size();
  create_desc.name = "Created Mesh";
  create_desc.name_length = 12U;
  create_desc.renderer.mesh_uri = mesh_uri.data();
  create_desc.renderer.mesh_uri_length = mesh_uri.size();
  create_desc.renderer.material_uri = material_uri.data();
  create_desc.renderer.material_uri_length = material_uri.size();
  gneiss_scene_node_id created_node = GNEISS_NULL_SCENE_NODE_ID;
  if (gneiss_scene_instance_create_mesh_renderer_node(application, scene, &create_desc,
                                                      &created_node) != GNEISS_SUCCESS ||
      created_node == GNEISS_NULL_SCENE_NODE_ID ||
      gneiss_scene_instance_get_node_count(application, scene, &node_count) != GNEISS_SUCCESS ||
      node_count != 3U || gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS ||
      entity_count != 3U ||
      gneiss_scene_instance_get_node_info(application, scene, 2U, &triangle_info) !=
          GNEISS_SUCCESS ||
      triangle_info.node != created_node || triangle_info.parent != camera_node ||
      std::string_view{triangle_info.name, triangle_info.name_length} != "Created Mesh") {
    return 4;
  }
  gneiss_scene_node_desc generic_desc = GNEISS_SCENE_NODE_DESC_INIT;
  generic_desc.uuid = generic_uuid.data();
  generic_desc.uuid_length = generic_uuid.size();
  generic_desc.name = "Empty";
  generic_desc.name_length = 5U;
  generic_desc.local_transform.translation[1] = 2.0F;
  gneiss_scene_node_id generic_node = GNEISS_NULL_SCENE_NODE_ID;
  if (gneiss_scene_instance_create_node(application, scene, &generic_desc, &generic_node) !=
          GNEISS_SUCCESS ||
      gneiss_scene_instance_set_node_name(application, scene, generic_node, "Group", 5U) !=
          GNEISS_SUCCESS ||
      gneiss_scene_instance_reparent_node(application, scene, triangle, generic_node) !=
          GNEISS_SUCCESS ||
      gneiss_scene_instance_reparent_node(application, scene, generic_node, triangle) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_scene_instance_get_node_info(application, scene, 3U, &triangle_info) !=
          GNEISS_SUCCESS ||
      triangle_info.node != generic_node ||
      std::string_view{triangle_info.name, triangle_info.name_length} != "Group" ||
      triangle_info.local_transform.translation[1] != 2.0F || triangle_info.component_flags != 0U ||
      gneiss_scene_instance_reparent_node(application, scene, triangle, camera_node) !=
          GNEISS_SUCCESS) {
    return 13;
  }
  if (gneiss_scene_instance_create_mesh_renderer_node(application, scene, &create_desc,
                                                      &triangle) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 5;
  }
  gneiss_scene_mesh_renderer_desc renderer_desc = GNEISS_SCENE_MESH_RENDERER_DESC_INIT;
  renderer_desc.mesh_uri = missing_mesh_uri.data();
  renderer_desc.mesh_uri_length = missing_mesh_uri.size();
  renderer_desc.material_uri = material_uri.data();
  renderer_desc.material_uri_length = material_uri.size();
  if (gneiss_scene_instance_set_mesh_renderer(application, scene, created_node, &renderer_desc) !=
      GNEISS_ERROR_NOT_FOUND) {
    return 6;
  }
  gneiss_entity_id camera_entity = GNEISS_NULL_ENTITY_ID;
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.translation[0] = 5.0F;
  gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  camera.near_plane = 0.25F;
  std::uint64_t json_length = 0;
  if (gneiss_scene_node_get_entity(world, camera_node, &camera_entity) != GNEISS_SUCCESS ||
      gneiss_world_entity_set_local_transform(world, camera_entity, &transform) != GNEISS_SUCCESS ||
      gneiss_world_entity_configure_camera(world, camera_entity, &camera) != GNEISS_SUCCESS ||
      gneiss_scene_instance_serialize(application, scene, nullptr, 0U, &json_length) !=
          GNEISS_SUCCESS ||
      json_length == 0U) {
    return 7;
  }
  std::string json(json_length, '\0');
  std::uint64_t required_length = 0;
  if (gneiss_scene_instance_serialize(application, scene, json.data(), json.size() - 1U,
                                      &required_length) != GNEISS_ERROR_INVALID_ARGUMENT ||
      required_length != json_length ||
      gneiss_scene_instance_serialize(application, scene, json.data(), json.size(), &json_length) !=
          GNEISS_SUCCESS ||
      json.find(R"("name":"Camera")") == std::string::npos ||
      json.find(R"("translation":[5.0,0.0,0.0])") == std::string::npos ||
      json.find(R"("near_plane":0.25)") == std::string::npos ||
      json.find(created_uuid) == std::string::npos ||
      json.find(R"("name":"Created Mesh")") == std::string::npos ||
      json.find(generic_uuid) == std::string::npos ||
      json.find(R"("name":"Group")") == std::string::npos ||
      gneiss_scene_instance_serialize(second_application, scene, nullptr, 0U, &json_length) !=
          GNEISS_ERROR_INVALID_HANDLE) {
    return 8;
  }
  if (gneiss_scene_instance_destroy_node(application, scene, camera_node) !=
          GNEISS_ERROR_INVALID_STATE ||
      gneiss_scene_instance_destroy_node(application, scene, created_node) != GNEISS_SUCCESS ||
      gneiss_scene_instance_destroy_node(application, scene, generic_node) != GNEISS_SUCCESS ||
      gneiss_scene_instance_destroy_node(application, scene, created_node) !=
          GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_scene_instance_get_node_count(application, scene, &node_count) != GNEISS_SUCCESS ||
      node_count != 2U || gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS ||
      entity_count != 2U) {
    return 9;
  }
  if (gneiss_scene_instance_unload(second_application, scene) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_scene_instance_unload(application, scene) != GNEISS_SUCCESS ||
      gneiss_scene_instance_unload(application, scene) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 0U) {
    return 10;
  }
  if (gneiss_application_destroy(second_application) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 11;
  }

  const auto failing_application = create_application(failure_root);
  world = GNEISS_NULL_WORLD;
  scene = GNEISS_NULL_SCENE_INSTANCE;
  if (failing_application == GNEISS_NULL_APPLICATION ||
      gneiss_application_get_world(failing_application, &world) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(failing_application, missing_uri.data(), missing_uri.size(),
                                 &scene) != GNEISS_ERROR_NOT_FOUND ||
      scene != GNEISS_NULL_SCENE_INSTANCE ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 0U ||
      gneiss_application_destroy(failing_application) != GNEISS_SUCCESS) {
    return 12;
  }
  return 0;
} catch (...) {
  return 99;
}
