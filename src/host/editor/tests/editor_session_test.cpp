// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/c_owner.hpp"
#include "commands/runtime_author_apply.hpp"
#include "documents/scene_document.hpp"
#include "engine/core/result.hpp"
#include "runtime/application_registry.hpp"

#include "engine/values/core/result.hpp"
#include "engine/values/log.hpp"
#include "engine/values/render.hpp"
#include "engine/values/scene.hpp"
#include <gneiss/engine/application.h>

#include <string>
#include <string_view>
#include <thread>

// 临时输入在返回后销毁；后续 Undo/Redo 必须使用命令持有的身份副本。
static gneiss::engine::result
apply_temporary_transform(gneiss::editor::scene_document& session,
                          gneiss::editor::editor_command_history& history,
                          const gneiss::editor::runtime_author_transform& input) {
  const std::string uuid(input.uuid);
  const std::string instance(input.prefab_instance_uuid);
  const std::string source(input.prefab_source_node_uuid);
  return gneiss::editor::apply_runtime_transform_to_author(
      session, history, {uuid, instance, source, input.local_transform});
}

int main() try {
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
  constexpr std::string_view missing_uri = "asset://scenes/missing.scene.json";
  const std::string asset_root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_root.c_str();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss::test_support::application_owner application;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::editor::scene_document session;
  if (gneiss::engine::result{gneiss_application_create(&desc, application.put())} !=
          gneiss::engine::result::success ||
      gneiss::engine::result{gneiss_application_get_world(application.get(), &world)} !=
          gneiss::engine::result::success ||
      session.open(*gneiss::runtime::find_application(application.get()), world, scene_uri) !=
          gneiss::engine::result::success ||
      !session.is_open() || session.is_dirty() || session.uri() != scene_uri ||
      session.nodes().size() != 2U || session.nodes()[0].display_name != "Camera" ||
      session.nodes()[1].display_name != session.nodes()[1].uuid ||
      session.nodes()[1].parent != session.nodes()[0].node) {
    return 1;
  }
  // 原生会话移动只转移一次释放责任；外线程编辑不得改变文档。
  const auto original_scene = session.scene_handle();
  gneiss::editor::scene_document moved_session{std::move(session)};
  if (session.is_open() || !moved_session.is_open() ||
      moved_session.scene_handle() != original_scene)
    return 20;
  session = std::move(moved_session);
  if (moved_session.is_open() || !session.is_open())
    return 21;
  auto foreign = gneiss::engine::result::success;
  std::thread worker([&] { foreign = session.rename_node(session.nodes()[1].node, "Rejected"); });
  worker.join();
  if (foreign != gneiss::engine::result::invalid_state || session.is_dirty())
    return 22;
  auto native = gneiss::runtime::find_application(application.get());
  if (session.adopt(*native, world, GNEISS_NULL_SCENE_INSTANCE, scene_uri) !=
          gneiss::engine::result::invalid_argument ||
      session.scene_handle() != original_scene)
    return 23;
  if (session.select(session.nodes()[1].node) != gneiss::engine::result::success ||
      session.selected_node() == nullptr) {
    return 2;
  }
  session.mark_dirty();
  if (!session.is_dirty() ||
      session.open(*gneiss::runtime::find_application(application.get()), world, missing_uri) !=
          gneiss::engine::result::not_found ||
      !session.is_open() || !session.is_dirty() || session.uri() != scene_uri ||
      session.selected_node() == nullptr) {
    return 3;
  }
  const auto selected = session.selection();
  if (gneiss::engine::result{gneiss_scene_node_destroy(world, selected.get())} !=
          gneiss::engine::result::success ||
      session.validate_selection() != gneiss::engine::result::invalid_handle ||
      session.selection().is_valid()) {
    return 4;
  }
  session.close();
  if (session.is_open() || session.is_dirty() || !session.nodes().empty() ||
      !session.prefab_nodes().empty() || session.selection().is_valid()) {
    return 5;
  }
  constexpr std::string_view prefab_scene_uri = "asset://scenes/prefab.scene.json";
  constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
  if (session.open(*gneiss::runtime::find_application(application.get()), world,
                   prefab_scene_uri) != gneiss::engine::result::success ||
      session.nodes().size() != 1U || session.prefab_nodes().size() != 2U ||
      !session.prefab_nodes()[0].is_instance_root || session.prefab_nodes()[0].is_read_only ||
      session.prefab_nodes()[1].is_instance_root || !session.prefab_nodes()[1].is_read_only ||
      session.prefab_nodes()[1].parent != session.prefab_nodes()[0].node) {
    return 6;
  }
  if (session.select(session.prefab_nodes()[1].node) != gneiss::engine::result::success ||
      session.selected_node() != nullptr || session.selected_prefab_node() == nullptr) {
    return 7;
  }
  auto source_moved = session.selected_prefab_node()->local_transform;
  source_moved.translation[1] = 5.0F;
  if (session.set_local_transform(session.selection(), source_moved) !=
          gneiss::engine::result::success ||
      session.selected_prefab_node() == nullptr ||
      session.selected_prefab_node()->local_transform.translation[1] != 5.0F ||
      (session.selected_prefab_node()->override_flags &
       GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN) == 0U) {
    return 7;
  }
  gneiss::transform previous_source{};
  if (session.restore_prefab_transform_field(session.selection(),
                                             GNEISS_TRANSFORM_FIELD_TRANSLATION,
                                             previous_source) != gneiss::engine::result::success ||
      previous_source.translation[1] != 5.0F || session.selected_prefab_node() == nullptr ||
      session.selected_prefab_node()->local_transform.translation[0] != 1.0F ||
      session.selected_prefab_node()->local_transform.translation[1] != 0.0F ||
      (session.selected_prefab_node()->override_flags &
       GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN) != 0U ||
      session.set_local_transform(session.selection(), previous_source) !=
          gneiss::engine::result::success ||
      (session.selected_prefab_node()->override_flags &
       GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN) == 0U ||
      session.restore_prefab_transform(session.selection(), previous_source) !=
          gneiss::engine::result::success ||
      session.selected_prefab_node()->override_flags != 0U) {
    return 8;
  }
  const auto* prefab_source = session.selected_prefab_node();
  const std::string source_uuid = prefab_source->source_node_uuid;
  const std::string instance_uuid = prefab_source->instance_uuid;
  gneiss::editor::runtime_author_transform runtime_source;
  runtime_source.uuid = source_uuid;
  runtime_source.prefab_instance_uuid = instance_uuid;
  runtime_source.prefab_source_node_uuid = source_uuid;
  runtime_source.local_transform = prefab_source->local_transform;
  runtime_source.local_transform.scale[0] = 2.0F;
  gneiss::editor::editor_command_history history;
  if (apply_temporary_transform(session, history, runtime_source) !=
          gneiss::engine::result::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 2.0F ||
      (session.selected_prefab_node()->override_flags &
       GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN) == 0U ||
      history.undo() != gneiss::engine::result::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 1.0F ||
      history.redo() != gneiss::engine::result::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 2.0F) {
    return 9;
  }
  auto invalid_runtime_source = runtime_source;
  invalid_runtime_source.prefab_instance_uuid = {};
  auto stale_runtime_source = runtime_source;
  stale_runtime_source.prefab_instance_uuid = "ffffffff-ffff-4fff-8fff-ffffffffffff";
  if (gneiss::editor::apply_runtime_transform_to_author(session, history, invalid_runtime_source) !=
          gneiss::engine::result::invalid_argument ||
      gneiss::editor::apply_runtime_transform_to_author(session, history, stale_runtime_source) !=
          gneiss::engine::result::not_found) {
    return 9;
  }
  gneiss::scene_node_id prefab_root;
  if (session.create_prefab_instance("Second Lamp", prefab_uri, session.nodes()[0].node,
                                     prefab_root) != gneiss::engine::result::success ||
      !session.is_dirty() || session.prefab_nodes().size() != 4U ||
      session.selected_prefab_node() == nullptr ||
      !session.selected_prefab_node()->is_instance_root ||
      session.selected_prefab_node()->node != prefab_root) {
    return 10;
  }
  gneiss::editor::prefab_instance_snapshot snapshot;
  if (session.destroy_prefab_instance(prefab_root, snapshot) != gneiss::engine::result::success ||
      session.prefab_nodes().size() != 2U || session.selection().is_valid() ||
      session.restore_prefab_instance(snapshot, prefab_root) != gneiss::engine::result::success ||
      session.prefab_nodes().size() != 4U ||
      session.rename_prefab_instance(prefab_root, "Restored Lamp") !=
          gneiss::engine::result::success) {
    return 11;
  }
  auto moved = session.selected_prefab_node()->local_transform;
  moved.translation[1] = 3.0F;
  const auto stale_root = prefab_root;
  gneiss_scene_prefab_refresh_token refresh_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  if (session.set_local_transform(prefab_root, moved) != gneiss::engine::result::success ||
      session.refresh_prefab_instance(prefab_root, prefab_root, refresh_token) !=
          gneiss::engine::result::success ||
      prefab_root == stale_root || session.selected_prefab_node() == nullptr ||
      session.selected_prefab_node()->display_name != "Restored Lamp" ||
      session.selected_prefab_node()->local_transform.translation[1] != 3.0F ||
      session.toggle_prefab_refresh(refresh_token, prefab_root) !=
          gneiss::engine::result::success ||
      session.toggle_prefab_refresh(refresh_token, prefab_root) !=
          gneiss::engine::result::success) {
    return 12;
  }
  session.release_prefab_refresh(refresh_token);
  return 0;
} catch (...) {
  return 99;
}
