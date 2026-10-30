// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_prefab.hpp"
#include "../support/prefab_query.hpp"
#include "../support/value_result.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include "runtime/application.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

int main() try {
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  constexpr std::string_view missing_scene_uri = "asset://scenes/missing-prefab.scene.json";
  gneiss::runtime::application_configuration application_desc;
  application_desc.asset_root = asset_root;
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::scene_instance_id scene = {};
  std::uint64_t entity_count = 0;
  if (gneiss::runtime::application::create(application_desc, application) !=
          gneiss::engine::error::success ||
      gneiss::test_support::load_scene(*application, scene_uri, &scene) !=
          gneiss::engine::error::success ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 3U) {
    return 1;
  }

  std::uint64_t prefab_node_count = 0;
  gneiss::engine::scene_node_view prefab_root;
  gneiss::engine::scene_node_view prefab_source;
  if (gneiss::test_support::prefab_node_count(*application->scenes(), scene, &prefab_node_count) !=
          gneiss::engine::error::success ||
      prefab_node_count != 2U ||
      gneiss::test_support::prefab_node_info(*application->scenes(), scene, 0U, prefab_root) !=
          gneiss::engine::error::success ||
      gneiss::test_support::prefab_node_info(*application->scenes(), scene, 1U, prefab_source) !=
          gneiss::engine::error::success ||
      prefab_root.prefab->flags != gneiss::scene_prefab_flags::instance_root ||
      (prefab_source.prefab->flags & gneiss::scene_prefab_flags::source_read_only) ==
          gneiss::scene_prefab_flags::none ||
      (prefab_source.prefab->flags & gneiss::scene_prefab_flags::translation_overridden) ==
          gneiss::scene_prefab_flags::none ||
      prefab_source.parent != prefab_root.node ||
      prefab_root.prefab->instance_uuid !=
          *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000012") ||
      prefab_source.prefab->source_node_uuid !=
          *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000002") ||
      prefab_source.local_transform.translation[0] != 3.0F) {
    return 2;
  }

  // 名称与删除入口按操作语义区分，不按 Prefab 类型区分。
  if (application->scenes()->destroy_node(scene, prefab_root.node) !=
          gneiss::engine::error::invalid_state ||
      application->scenes()->set_node_name(scene, prefab_source.node, "Forbidden") !=
          gneiss::engine::error::invalid_handle)
    return 13;

  gneiss::engine::math::transform source_transform;
  source_transform.translation[0] = 1.0F;
  if (application->scenes()->set_prefab_source_transform(
          scene, prefab_source.node, source_transform) != gneiss::engine::error::success) {
    return 2;
  }
  std::string sparse_json;
  if (application->scenes()->serialize(scene, sparse_json) != gneiss::engine::error::success ||
      sparse_json.empty() || sparse_json.find(R"("overrides":[])") == std::string::npos) {
    return 2;
  }
  source_transform.translation[0] = 4.0F;
  if (application->scenes()->set_prefab_source_transform(
          scene, prefab_source.node, source_transform) != gneiss::engine::error::success) {
    return 2;
  }

  const auto stale_loaded_root = prefab_root.node;
  if (gneiss::test_support::refresh_prefab(*application, scene, prefab_root.prefab->instance_uuid,
                                           prefab_root.node) != gneiss::engine::error::success ||
      prefab_root.node == gneiss::null_scene_node_id || prefab_root.node == stale_loaded_root ||
      gneiss::test_support::prefab_node_info(*application->scenes(), scene, 1U, prefab_source) !=
          gneiss::engine::error::success ||
      prefab_source.local_transform.translation[0] != 4.0F) {
    return 2;
  }

  gneiss::scene_node_id anchor;
  const gneiss::engine::guid anchor_uuid =
      *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000011");
  const gneiss::engine::guid created_uuid =
      *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000013");
  constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
  constexpr std::string_view created_name = "Second Lamp";
  gneiss::engine::prefab_creation create_desc;
  create_desc.instance_uuid = created_uuid;
  create_desc.name = created_name;
  create_desc.prefab_uri = prefab_uri;
  gneiss::scene_node_id created_root;
  if (application->scenes()->find_node(scene, anchor_uuid, anchor) !=
      gneiss::engine::error::success) {
    return 3;
  }
  create_desc.parent = prefab_source.node;
  if (gneiss::test_support::create_prefab(*application, scene, create_desc, created_root) !=
          gneiss::engine::error::invalid_handle ||
      created_root != gneiss::null_scene_node_id) {
    return 3;
  }
  constexpr std::string_view missing_prefab_uri = "asset://prefabs/not-found.prefab.json";
  create_desc.parent = anchor;
  create_desc.prefab_uri = missing_prefab_uri;
  if (gneiss::test_support::create_prefab(*application, scene, create_desc, created_root) !=
          gneiss::engine::error::not_found ||
      created_root != gneiss::null_scene_node_id ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 3U) {
    return 3;
  }
  create_desc.prefab_uri = prefab_uri;
  create_desc.parent = anchor;
  if (gneiss::test_support::create_prefab(*application, scene, create_desc, created_root) !=
          gneiss::engine::error::success ||
      created_root == gneiss::null_scene_node_id ||
      gneiss::test_support::prefab_node_count(*application->scenes(), scene, &prefab_node_count) !=
          gneiss::engine::error::success ||
      prefab_node_count != 4U ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 5U) {
    return 3;
  }
  constexpr std::string_view renamed = "Renamed Lamp";
  gneiss::engine::math::transform moved;
  moved.translation[0] = 4.0F;
  if (application->scenes()->set_node_name(scene, created_root, renamed) !=
          gneiss::engine::error::success ||
      application->native_world()->node_set_local_transform(created_root, moved) !=
          gneiss::engine::error::success) {
    return 4;
  }
  gneiss::scene_node_id attached;
  gneiss::engine::node_creation child{
      .uuid = *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000014"),
      .name = "Own child",
      .parent = {}};
  if (application->scenes()->create_node(scene, child, attached).failed() ||
      application->scenes()->reparent_node(scene, attached, created_root).failed())
    return 12;
  std::string with_child;
  if (application->scenes()->serialize(scene, with_child).failed())
    return 12;
  auto decoded_child = gneiss::engine::decode_scene(std::as_bytes(std::span(with_child)));
  if (!decoded_child || decoded_child->objects.size() != 4U ||
      decoded_child->objects.back().parent_uuid != created_uuid)
    return 12;
  const auto stale_root = created_root;
  gneiss::entity_id stale_entity;
  if (gneiss::test_support::refresh_prefab(*application, scene, created_uuid, created_root) !=
          gneiss::engine::error::success ||
      created_root == gneiss::null_scene_node_id || created_root == stale_root ||
      application->native_world()->node_get_entity(stale_root, stale_entity) !=
          gneiss::engine::error::invalid_handle) {
    return 4;
  }
  gneiss::scene_node_id attached_parent;
  if (application->native_world()->node_get_parent(attached, attached_parent).failed() ||
      attached_parent != created_root ||
      application->scenes()->destroy_node(scene, attached).failed())
    return 12;
  if (application->scenes()->destroy_subtree(scene, prefab_root.node) !=
          gneiss::engine::error::success ||
      gneiss::test_support::prefab_node_count(*application->scenes(), scene, &prefab_node_count) !=
          gneiss::engine::error::success ||
      prefab_node_count != 2U) {
    return 4;
  }

  std::string json;
  if (application->scenes()->serialize(scene, json) != gneiss::engine::error::success ||
      json.empty() || json.find("asset://prefabs/test.prefab.json") == std::string::npos ||
      json.find(created_uuid.string()) == std::string::npos ||
      json.find(renamed) == std::string::npos ||
      json.find("30000000-0000-4000-8000-000000000002") != std::string::npos) {
    return 6;
  }
  // 统一枚举包含普通父节点及 Prefab 全部节点；删除父节点同时清理来源关系。
  std::uint64_t node_count{};
  if (application->scenes()->get_node_count(scene, &node_count) != gneiss::engine::error::success) {
    std::fprintf(stderr, "子树生命周期检查 0 失败\n");
    return 10;
  }
  if (node_count != 3U) {
    std::fprintf(stderr, "子树生命周期检查 1 失败\n");
    return 10;
  }
  if (application->scenes()->destroy_subtree(scene, anchor) != gneiss::engine::error::success) {
    std::fprintf(stderr, "子树生命周期检查 2 失败\n");
    return 10;
  }
  if (application->scenes()->get_node_count(scene, &node_count) != gneiss::engine::error::success) {
    std::fprintf(stderr, "子树生命周期检查 3 失败\n");
    return 10;
  }
  if (node_count != 0U) {
    std::fprintf(stderr, "子树生命周期检查 4 失败\n");
    return 10;
  }
  if (gneiss::test_support::prefab_node_count(*application->scenes(), scene, &prefab_node_count) !=
      gneiss::engine::error::success) {
    std::fprintf(stderr, "子树生命周期检查 5 失败\n");
    return 10;
  }
  if (prefab_node_count != 0U) {
    std::fprintf(stderr, "子树生命周期检查 6 失败\n");
    return 10;
  }
  if (application->native_world()->node_get_entity(created_root, stale_entity) !=
      gneiss::engine::error::invalid_handle) {
    std::fprintf(stderr, "子树生命周期检查 7 失败\n");
    return 10;
  }
  if (application->scenes()->serialize(scene, json) != gneiss::engine::error::success) {
    std::fprintf(stderr, "子树生命周期检查 8 失败\n");
    return 10;
  }
  if (json.find(created_uuid.string()) != std::string::npos) {
    std::fprintf(stderr, "子树生命周期检查 9 失败\n");
    return 10;
  }
  if (gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success) {
    std::fprintf(stderr, "子树生命周期检查 10 失败\n");
    return 10;
  }
  if (entity_count != 0U) {
    std::fprintf(stderr, "子树生命周期检查 11 失败\n");
    return 10;
  }
  if (application->scenes()->unload(scene) != gneiss::engine::error::success ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 0U) {
    return 7;
  }
  scene = gneiss::scene_instance_id{};
  if (gneiss::test_support::load_scene(*application, missing_scene_uri, &scene) !=
          gneiss::engine::error::not_found ||
      scene != gneiss::scene_instance_id{} ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 0U) {
    return 8;
  }
  return application->shutdown() == gneiss::engine::error::success ? 0 : 9;
} catch (...) {
  return 99;
}
