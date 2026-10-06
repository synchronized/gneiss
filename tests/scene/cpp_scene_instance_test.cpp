// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/application.hpp>

#include <string>
#include <string_view>
#include <thread>

namespace {
bool verify_mapping(gneiss::scene_instance& scene, std::string_view snapshot,
                    gneiss::scene_node_id parent, std::string_view source,
                    std::string_view target) {
  gneiss::scene_uuid_mapping mapping{.source_uuid = source, .target_uuid = "invalid"};
  gneiss::scene_node_id restored = parent;
  if (scene.restore_subtree(snapshot, parent, {&mapping, 1}, restored) !=
          gneiss::result::invalid_argument ||
      restored != parent) {
    return false;
  }
  mapping.target_uuid = target;
  gneiss::scene_node_id found;
  if (scene.restore_subtree(snapshot, parent, {&mapping, 1}, restored).failed() ||
      scene.find_node(target, found).failed() || found != restored) {
    return false;
  }
  return scene.destroy_subtree(restored).ok();
}
bool verify_failed_query(gneiss::scene_instance& scene,
                         const gneiss::scene_instance_node_info& source) {
  auto output = source;
  const auto status = scene.get_node_info(9999, output);
  return status.failed() && output.node == source.node && output.uuid == source.uuid;
}
} // namespace

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
  gneiss::application_desc desc{};
  desc.asset_root = asset_root;
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
  gneiss::scene_instance_node_info info{};
  if (scene.get_node_count(count).failed() || count != 1U ||
      scene.get_node_info(0U, info).failed()) {
    return 3;
  }
  if (!verify_failed_query(scene, info)) {
    return 20;
  }
  const gneiss::scene_node_id anchor{info.node};
  const std::string copied_name(info.name);
  const std::string copied_uuid(info.uuid);
  const std::string renamed(160U, 'a');
  if (scene.set_node_name(anchor, renamed).failed()) {
    return 4;
  }
  // 修改后不再解引用旧描述中的指针；重新枚举，先前复制的字符串仍可使用。
  info = {};
  gneiss::scene_node_id found;
  if (scene.get_node_info(0U, info).failed() || std::string_view(info.name) != renamed ||
      copied_name != "Anchor" || scene.find_node(copied_uuid, found).failed() || found != anchor ||
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

  gneiss::scene_node_desc node_desc{};
  std::string source_uuid(child_uuid);
  std::string source_name(160U, 'n');
  const auto expected_name = source_name;
  node_desc.uuid = source_uuid;

  node_desc.name = source_name;

  node_desc.parent = anchor;
  node_desc.uuid = "invalid";
  if (scene.create_node(node_desc, found) != result::invalid_argument || found != anchor) {
    return 18;
  }
  node_desc.uuid = source_uuid;
  gneiss::scene_node_id child;
  if (scene.create_node(node_desc, child).failed() ||
      scene.create_node(node_desc, found) != result::invalid_argument || found != anchor ||
      scene.reparent_node(anchor, child) != result::invalid_argument ||
      scene.reparent_node(child, {}).failed() || scene.reparent_node(child, anchor).failed()) {
    return 6;
  }
  source_uuid[0] = '7';
  source_name[0] = 'x';
  if (scene.get_node_info(1U, info).failed() || std::string_view(info.uuid) != child_uuid ||
      std::string_view(info.name) != expected_name) {
    return 19;
  }
  gneiss::scene_camera_desc camera{};
  gneiss::scene_mesh_renderer_desc renderer{};
  renderer.mesh_uri = mesh_uri;

  renderer.material_uri = material_uri;

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
  if (!verify_mapping(scene, snapshot, anchor, child_uuid, mesh_uuid)) {
    return 21;
  }
  gneiss::scene_mesh_renderer_node_desc mesh_desc{};
  mesh_desc.uuid = mesh_uuid;

  mesh_desc.renderer = renderer;
  if (scene.create_mesh_renderer_node(mesh_desc, child).failed() ||
      scene.get_node_count(count).failed() || count != 2U || scene.destroy_node(child).failed()) {
    return 9;
  }

  gneiss::scene_prefab_node_info prefab_info{};
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
  gneiss::scene_prefab_instance_desc prefab_desc{};
  prefab_desc.instance_uuid = prefab_uuid;

  prefab_desc.prefab_uri = prefab_uri;

  gneiss::scene_node_id prefab_root;
  if (scene.create_prefab_instance(prefab_desc, prefab_root).failed() ||
      scene.set_prefab_instance_name(prefab_root, "second").failed() ||
      scene.get_prefab_node_count(count).failed() || count != 4U ||
      scene.get_prefab_node_info(2U, prefab_info).failed() ||
      std::string_view(prefab_info.name) != "second" ||
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
