// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace {

std::shared_ptr<gneiss::runtime::application> create_application(std::string_view asset_root) {
  gneiss::runtime::application_configuration desc;
  desc.asset_root = asset_root;
  std::shared_ptr<gneiss::runtime::application> application;
  return (gneiss::runtime::application::create(desc, application) ==
          gneiss::engine::result::success)
             ? application
             : nullptr;
}

} // namespace

int main() try {
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view failure_root = GNEISS_TEST_FAILURE_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
  constexpr std::string_view missing_uri = "asset://scenes/missing-asset.scene.json";
  constexpr std::string_view corrupt_uri = "asset://scenes/corrupt.scene.json";
  constexpr std::string_view triangle_uuid = "00000000-0000-4000-8000-000000000003";
  constexpr std::string_view camera_uuid = "00000000-0000-4000-8000-000000000002";
  constexpr std::string_view created_uuid = "00000000-0000-4000-8000-000000000004";
  constexpr std::string_view generic_uuid = "00000000-0000-4000-8000-000000000005";
  constexpr std::string_view copied_generic_uuid = "00000000-0000-4000-8000-000000000006";
  constexpr std::string_view copied_triangle_uuid = "00000000-0000-4000-8000-000000000007";
  constexpr std::string_view mesh_uri = "asset://models/triangle.mesh.json";
  constexpr std::string_view material_uri = "asset://materials/triangle.material.json";
  constexpr std::string_view missing_mesh_uri = "asset://models/missing.mesh.json";
  const auto application = create_application(asset_root);
  const auto second_application = create_application(asset_root);
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  gneiss::scene_node_id triangle;
  gneiss::scene_node_id camera_node;
  std::uint64_t entity_count = 0;
  std::uint64_t node_count = 0;
  if (application == nullptr || second_application == nullptr ||
      application->scenes()->load(scene_uri, &scene) != gneiss::engine::result::success ||
      scene == GNEISS_NULL_SCENE_INSTANCE ||
      application->native_world()->entity_count(entity_count) != gneiss::engine::result::success ||
      entity_count != 2U ||
      application->scenes()->find_node(scene, triangle_uuid, triangle) !=
          gneiss::engine::result::success ||
      triangle == gneiss::null_scene_node_id ||
      application->scenes()->find_node(scene, camera_uuid, camera_node) !=
          gneiss::engine::result::success) {
    return 1;
  }
  gneiss::engine::scene_node_view camera_info;
  gneiss::engine::scene_node_view triangle_info;
  if (application->scenes()->get_node_count(scene, &node_count) !=
          gneiss::engine::result::success ||
      node_count != 2U ||
      application->scenes()->get_node_info(scene, 0U, camera_info) !=
          gneiss::engine::result::success ||
      camera_info.node != camera_node || camera_info.parent != gneiss::null_scene_node_id ||
      camera_info.uuid != camera_uuid || camera_info.name != "Camera" ||
      camera_info.component_flags !=
          (gneiss::scene_node_components::camera | gneiss::scene_node_components::primary_camera) ||
      application->scenes()->get_node_info(scene, 1U, triangle_info) !=
          gneiss::engine::result::success ||
      triangle_info.node != triangle || triangle_info.parent != camera_node ||
      triangle_info.uuid != triangle_uuid || !triangle_info.name.empty() ||
      triangle_info.mesh_uri != mesh_uri || triangle_info.material_uri != material_uri ||
      triangle_info.component_flags != gneiss::scene_node_components::mesh_renderer ||
      application->scenes()->get_node_info(scene, 2U, triangle_info) !=
          gneiss::engine::result::not_found ||
      second_application->scenes()->get_node_count(scene, &node_count) !=
          gneiss::engine::result::invalid_handle) {
    return 2;
  }
  gneiss::engine::scene_node_view invalid_info;
  if (application->scenes()->get_node_info(GNEISS_NULL_SCENE_INSTANCE, 0U, invalid_info) !=
      gneiss::engine::result::invalid_handle) {
    return 3;
  }
  gneiss::engine::mesh_renderer_node_creation create_desc;
  create_desc.parent = camera_node;
  create_desc.uuid = created_uuid;
  create_desc.name = "Created Mesh";
  create_desc.mesh_uri = mesh_uri;
  create_desc.material_uri = material_uri;
  gneiss::scene_node_id created_node;
  if (application->scenes()->create_mesh_renderer_node(scene, create_desc, created_node) !=
          gneiss::engine::result::success ||
      created_node == gneiss::null_scene_node_id ||
      application->scenes()->get_node_count(scene, &node_count) !=
          gneiss::engine::result::success ||
      node_count != 3U ||
      application->native_world()->entity_count(entity_count) != gneiss::engine::result::success ||
      entity_count != 3U ||
      application->scenes()->get_node_info(scene, 2U, triangle_info) !=
          gneiss::engine::result::success ||
      triangle_info.node != created_node || triangle_info.parent != camera_node ||
      triangle_info.name != "Created Mesh") {
    return 4;
  }
  gneiss::engine::camera_description author_camera{1.04719755F, 0.2F, 1000.0F, false};
  author_camera.near_plane = -1.0F;
  if (application->scenes()->set_camera(scene, created_node, author_camera) !=
          gneiss::engine::result::invalid_argument ||
      application->scenes()->set_camera(GNEISS_NULL_SCENE_INSTANCE, created_node, author_camera) !=
          gneiss::engine::result::invalid_handle ||
      application->scenes()->set_camera(scene, gneiss::scene_node_id{UINT64_MAX}, author_camera) !=
          gneiss::engine::result::invalid_handle) {
    return 34;
  }
  author_camera.near_plane = 0.2F;
  if (application->scenes()->set_camera(scene, created_node, author_camera) !=
          gneiss::engine::result::success ||
      application->scenes()->remove_camera(scene, created_node) !=
          gneiss::engine::result::success ||
      application->scenes()->remove_camera(scene, created_node) !=
          gneiss::engine::result::not_found ||
      application->scenes()->remove_mesh_renderer(scene, created_node) !=
          gneiss::engine::result::success ||
      application->scenes()->remove_mesh_renderer(scene, created_node) !=
          gneiss::engine::result::not_found) {
    return 27;
  }
  if (application->scenes()->set_mesh_renderer(scene, created_node, mesh_uri, material_uri) !=
      gneiss::engine::result::success) {
    return 28;
  }
  gneiss::engine::node_creation generic_desc;
  generic_desc.uuid = generic_uuid;
  generic_desc.name = "Empty";
  generic_desc.local_transform.translation[1] = 2.0F;
  gneiss::scene_node_id generic_node;
  if (application->scenes()->create_node(scene, generic_desc, generic_node) !=
          gneiss::engine::result::success ||
      application->scenes()->set_node_name(scene, generic_node, "Group") !=
          gneiss::engine::result::success ||
      application->scenes()->reparent_node(scene, triangle, generic_node) !=
          gneiss::engine::result::success ||
      application->scenes()->reparent_node(scene, generic_node, triangle) !=
          gneiss::engine::result::invalid_argument ||
      application->scenes()->get_node_info(scene, 3U, triangle_info) !=
          gneiss::engine::result::success ||
      triangle_info.node != generic_node || triangle_info.name != "Group" ||
      triangle_info.local_transform.translation[1] != 2.0F ||
      triangle_info.component_flags != gneiss::scene_node_components::none) {
    return 13;
  }
  // 捕获非根子树时不能保留快照外的父引用；恢复时由显式 parent 重新挂接。
  if (application->scenes()->reparent_node(scene, generic_node, camera_node) !=
      gneiss::engine::result::success) {
    return 29;
  }
  std::string subtree_snapshot;
  const auto old_generic_node = generic_node;
  const auto old_triangle_node = triangle;
  if (application->scenes()->capture_subtree(scene, generic_node, subtree_snapshot) !=
          gneiss::engine::result::success ||
      subtree_snapshot.empty() ||
      application->scenes()->destroy_subtree(scene, generic_node) !=
          gneiss::engine::result::success) {
    return 15;
  }
  if (application->native_world()->node_get_parent(old_generic_node, generic_node) !=
          gneiss::engine::result::invalid_handle ||
      application->native_world()->node_get_parent(old_triangle_node, triangle) !=
          gneiss::engine::result::invalid_handle) {
    return 17;
  }
  if (application->scenes()->restore_subtree(scene, subtree_snapshot, camera_node, {},
                                             generic_node) != gneiss::engine::result::success) {
    return 18;
  }
  if (generic_node == old_generic_node) {
    return 19;
  }
  if (application->scenes()->find_node(scene, triangle_uuid, triangle) !=
      gneiss::engine::result::success) {
    return 20;
  }
  if (triangle == old_triangle_node) {
    return 21;
  }
  const gneiss::engine::uuid_mapping mappings[] = {{generic_uuid, copied_generic_uuid},
                                                   {triangle_uuid, copied_triangle_uuid}};
  gneiss::scene_node_id copied_root;
  if (application->scenes()->restore_subtree(scene, subtree_snapshot, camera_node,
                                             std::span{mappings}.first(1U), copied_root) !=
      gneiss::engine::result::invalid_argument) {
    return 16;
  }
  auto missing_asset_snapshot = subtree_snapshot;
  const auto mesh_position = missing_asset_snapshot.find(mesh_uri);
  if (mesh_position == std::string::npos) {
    return 25;
  }
  missing_asset_snapshot.replace(mesh_position, mesh_uri.size(), missing_mesh_uri);
  if (application->scenes()->restore_subtree(scene, missing_asset_snapshot, camera_node,
                                             std::span{mappings}.first(2U),
                                             copied_root) != gneiss::engine::result::not_found ||
      application->scenes()->get_node_count(scene, &node_count) !=
          gneiss::engine::result::success ||
      node_count != 4U) {
    return 26;
  }
  const auto copy_result = application->scenes()->restore_subtree(
      scene, subtree_snapshot, camera_node, std::span{mappings}.first(2U), copied_root);
  if (copy_result != gneiss::engine::result::success) {
    return 22;
  }
  if (copied_root == gneiss::null_scene_node_id) {
    return 22;
  }
  if (application->scenes()->destroy_subtree(scene, copied_root) !=
      gneiss::engine::result::success) {
    return 23;
  }
  if (application->scenes()->reparent_node(scene, triangle, camera_node) !=
      gneiss::engine::result::success) {
    return 24;
  }
  if (application->scenes()->create_mesh_renderer_node(scene, create_desc, triangle) !=
      gneiss::engine::result::invalid_argument) {
    return 5;
  }
  if (application->scenes()->set_mesh_renderer(scene, created_node, missing_mesh_uri,
                                               material_uri) != gneiss::engine::result::not_found) {
    return 6;
  }
  gneiss::entity_id camera_entity;
  gneiss::engine::transform transform;
  transform.translation[0] = 5.0F;
  gneiss::engine::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  camera.near_plane = 0.25F;
  std::string json;
  if (application->native_world()->node_get_entity(camera_node, camera_entity) !=
          gneiss::engine::result::success ||
      application->native_world()->entity_set_local_transform(camera_entity, transform) !=
          gneiss::engine::result::success ||
      application->native_world()->entity_configure_camera(camera_entity, camera) !=
          gneiss::engine::result::success ||
      application->scenes()->serialize(scene, json) != gneiss::engine::result::success ||
      json.empty()) {
    return 7;
  }
  if (json.find(R"("name":"Camera")") == std::string::npos ||
      json.find(R"("translation":[5.0,0.0,0.0])") == std::string::npos ||
      json.find(R"("near_plane":0.25)") == std::string::npos ||
      json.find(created_uuid) == std::string::npos ||
      json.find(R"("name":"Created Mesh")") == std::string::npos ||
      json.find(generic_uuid) == std::string::npos ||
      json.find(R"("name":"Group")") == std::string::npos ||
      second_application->scenes()->serialize(scene, json) !=
          gneiss::engine::result::invalid_handle) {
    return 8;
  }
  if (application->scenes()->destroy_node(scene, camera_node) !=
          gneiss::engine::result::invalid_state ||
      application->scenes()->destroy_node(scene, created_node) != gneiss::engine::result::success ||
      application->scenes()->destroy_node(scene, generic_node) != gneiss::engine::result::success ||
      application->scenes()->destroy_node(scene, created_node) !=
          gneiss::engine::result::invalid_handle ||
      application->scenes()->get_node_count(scene, &node_count) !=
          gneiss::engine::result::success ||
      node_count != 2U ||
      application->native_world()->entity_count(entity_count) != gneiss::engine::result::success ||
      entity_count != 2U) {
    return 9;
  }
  constexpr std::string_view empty_uuid = "00000000-0000-4000-8000-000000000099";
  gneiss_scene_instance empty_scene = GNEISS_NULL_SCENE_INSTANCE;
  std::uint64_t empty_count = 1U;
  if (application->scenes()->create_empty("invalid", &empty_scene) !=
          gneiss::engine::result::invalid_argument ||
      empty_scene != GNEISS_NULL_SCENE_INSTANCE ||
      application->scenes()->create_empty(empty_uuid, &empty_scene) !=
          gneiss::engine::result::success ||
      application->scenes()->get_node_count(empty_scene, &empty_count) !=
          gneiss::engine::result::success ||
      empty_count != 0U ||
      application->scenes()->serialize(empty_scene, json) != gneiss::engine::result::success ||
      json.empty() ||
      application->scenes()->unload(empty_scene) != gneiss::engine::result::success) {
    return 13;
  }
  if (second_application->scenes()->unload(scene) != gneiss::engine::result::invalid_handle ||
      application->scenes()->unload(scene) != gneiss::engine::result::success ||
      application->scenes()->unload(scene) != gneiss::engine::result::invalid_handle ||
      application->native_world()->entity_count(entity_count) != gneiss::engine::result::success ||
      entity_count != 0U) {
    return 10;
  }
  if (second_application->shutdown(0) != gneiss::engine::result::success ||
      application->shutdown(0) != gneiss::engine::result::success) {
    return 11;
  }

  const auto failing_application = create_application(failure_root);
  scene = GNEISS_NULL_SCENE_INSTANCE;
  if (failing_application == nullptr ||
      failing_application->scenes()->load(missing_uri, &scene) !=
          gneiss::engine::result::not_found ||
      scene != GNEISS_NULL_SCENE_INSTANCE ||
      failing_application->scenes()->load(corrupt_uri, &scene) !=
          gneiss::engine::result::invalid_argument ||
      scene != GNEISS_NULL_SCENE_INSTANCE ||
      failing_application->scenes()->create_empty(empty_uuid, &scene) !=
          gneiss::engine::result::success ||
      failing_application->scenes()->unload(scene) != gneiss::engine::result::success ||
      failing_application->native_world()->entity_count(entity_count) !=
          gneiss::engine::result::success ||
      entity_count != 0U || failing_application->shutdown(0) != gneiss::engine::result::success) {
    return 12;
  }
  return 0;
} catch (...) {
  return 99;
}
