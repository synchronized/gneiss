// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>

#include <string>
#include <string_view>
#include <thread>

int main() try {
  using gneiss::result;
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  constexpr std::string_view child_uuid = "50000000-0000-4000-8000-000000000001";
  constexpr std::string_view prefab_uuid = "50000000-0000-4000-8000-000000000002";
  constexpr std::string_view mesh_uuid = "50000000-0000-4000-8000-000000000003";
  constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
  constexpr std::string_view mesh_uri = "asset://models/triangle.mesh.json";
  constexpr std::string_view material_uri = "asset://materials/triangle.material.json";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss::application app;
  gneiss::scene_instance scene;
  if (gneiss::application::create(desc, app).failed() ||
      gneiss::scene_instance::load(app.get(), scene_uri, scene).failed()) {
    return 1;
  }
  const auto original_scene = scene.get();
  if (gneiss::scene_instance::load(app.get(), "asset://scenes/missing.scene.json", scene) !=
          result::not_found ||
      scene.get() != original_scene) {
    return 2;
  }
  std::uint64_t count{};
  gneiss::scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  if (scene.get_node_count(count).failed() || count != 1U ||
      scene.get_node_info(0U, info).failed()) {
    return 3;
  }
  const gneiss::scene_node_id anchor{info.node};
  const std::string copied_name(info.name, info.name_length);
  const std::string copied_uuid(info.uuid, info.uuid_length);
  const std::string renamed(160U, 'a');
  if (scene.set_node_name(anchor, renamed).failed()) {
    return 4;
  }
  // 修改后不再解引用旧描述中的指针；重新枚举，先前复制的字符串仍可使用。
  info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  gneiss::scene_node_id found;
  if (scene.get_node_info(0U, info).failed() ||
      std::string_view(info.name, info.name_length) != renamed || copied_name != "Anchor" ||
      scene.find_node(copied_uuid, found).failed() || found != anchor ||
      scene.find_node(child_uuid, found) != result::not_found || found != anchor) {
    return 5;
  }
  std::string before;
  std::string rejected_snapshot = "unchanged";
  if (scene.serialize(before).failed() ||
      scene.capture_subtree(anchor, rejected_snapshot) != result::unsupported ||
      rejected_snapshot != "unchanged" || scene.destroy_subtree(anchor) != result::unsupported ||
      scene.destroy_node(anchor) != result::invalid_state ||
      scene.restore_subtree(before, {}, {}, found) != result::unsupported || found != anchor) {
    return 16;
  }
  std::string after;
  if (scene.serialize(after).failed() || after != before) {
    return 17;
  }

  gneiss::scene_node_desc node_desc = GNEISS_SCENE_NODE_DESC_INIT;
  node_desc.uuid = child_uuid.data();
  node_desc.uuid_length = child_uuid.size();
  node_desc.parent = anchor.get();
  gneiss::scene_node_id child;
  if (scene.create_node(node_desc, child).failed() ||
      scene.create_node(node_desc, found) != result::invalid_argument || found != anchor ||
      scene.reparent_node(anchor, child) != result::invalid_argument ||
      scene.reparent_node(child, {}).failed() || scene.reparent_node(child, anchor).failed()) {
    return 6;
  }
  gneiss::scene_camera_desc camera = GNEISS_SCENE_CAMERA_DESC_INIT;
  gneiss::scene_mesh_renderer_desc renderer = GNEISS_SCENE_MESH_RENDERER_DESC_INIT;
  renderer.mesh_uri = mesh_uri.data();
  renderer.mesh_uri_length = mesh_uri.size();
  renderer.material_uri = material_uri.data();
  renderer.material_uri_length = material_uri.size();
  if (scene.set_camera(child, camera).failed() || scene.remove_camera(child).failed() ||
      scene.remove_camera(child) != result::not_found ||
      scene.set_mesh_renderer(child, renderer).failed() ||
      scene.remove_mesh_renderer(child).failed() ||
      scene.remove_mesh_renderer(child) != result::not_found ||
      scene.set_mesh_renderer(child, renderer).failed()) {
    return 7;
  }
  std::string snapshot;
  if (scene.capture_subtree(child, snapshot).failed() || snapshot.empty()) {
    return 81;
  }
  if (scene.destroy_subtree(child).failed()) {
    return 82;
  }
  if (!scene.restore_subtree("invalid", anchor, {}, found).failed() || found != anchor) {
    return 83;
  }
  if (scene.restore_subtree(snapshot, anchor, {}, found).failed()) {
    return 84;
  }
  if (found == child || scene.destroy_node(found).failed() ||
      scene.destroy_node(found) != result::invalid_handle) {
    return 85;
  }
  gneiss::scene_mesh_renderer_node_desc mesh_desc = GNEISS_SCENE_MESH_RENDERER_NODE_DESC_INIT;
  mesh_desc.uuid = mesh_uuid.data();
  mesh_desc.uuid_length = mesh_uuid.size();
  mesh_desc.renderer = renderer;
  if (scene.create_mesh_renderer_node(mesh_desc, child).failed() ||
      scene.get_node_count(count).failed() || count != 2U || scene.destroy_node(child).failed()) {
    return 9;
  }

  gneiss::scene_prefab_node_info prefab_info = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  if (scene.get_prefab_node_count(count).failed() || count != 2U ||
      scene.get_prefab_node_info(1U, prefab_info).failed()) {
    return 10;
  }
  const gneiss::scene_node_id source{prefab_info.node};
  auto transform = prefab_info.local_transform;
  transform.translation[0] = 6.0F;
  if (scene.set_prefab_source_transform(source, transform).failed() ||
      scene.get_prefab_node_info(1U, prefab_info).failed() ||
      prefab_info.local_transform.translation[0] != 6.0F) {
    return 11;
  }
  gneiss::scene_prefab_instance_desc prefab_desc = GNEISS_SCENE_PREFAB_INSTANCE_DESC_INIT;
  prefab_desc.instance_uuid = prefab_uuid.data();
  prefab_desc.instance_uuid_length = prefab_uuid.size();
  prefab_desc.prefab_uri = prefab_uri.data();
  prefab_desc.prefab_uri_length = prefab_uri.size();
  gneiss::scene_node_id prefab_root;
  if (scene.create_prefab_instance(prefab_desc, prefab_root).failed() ||
      scene.set_prefab_instance_name(prefab_root, "second").failed() ||
      scene.get_prefab_node_count(count).failed() || count != 4U ||
      scene.get_prefab_node_info(2U, prefab_info).failed() ||
      std::string_view(prefab_info.name, prefab_info.name_length) != "second" ||
      scene.destroy_prefab_instance(prefab_root).failed() ||
      scene.destroy_prefab_instance(prefab_root) != result::invalid_handle) {
    return 12;
  }
  std::string serialized;
  if (scene.serialize(serialized).failed() || serialized.find(renamed) == std::string::npos) {
    return 13;
  }
  const auto saved = serialized;
  result wrong_thread;
  std::thread worker([&] { wrong_thread = scene.serialize(serialized); });
  worker.join();
  if (wrong_thread != result::invalid_state || serialized != saved || scene.reset().failed() ||
      scene.serialize(serialized) != result::invalid_handle || serialized != saved) {
    return 14;
  }
  return 0;
} catch (...) {
  return 15;
}
