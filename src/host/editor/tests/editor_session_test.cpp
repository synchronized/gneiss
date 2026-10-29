// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/async_prefab_edit.hpp"
#include "commands/runtime_author_apply.hpp"
#include "documents/scene_document.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include "engine/core/error.hpp"

#include <string>
#include <string_view>
#include <thread>

// 临时输入在返回后销毁；后续 Undo/Redo 必须使用命令持有的身份副本。
static gneiss::engine::error
apply_temporary_transform(gneiss::editor::scene_document& session,
                          gneiss::editor::editor_command_history& history,
                          const gneiss::editor::runtime_author_transform& input) {
  const gneiss::engine::guid uuid(input.uuid);
  const gneiss::engine::guid instance(input.prefab_instance_uuid);
  const gneiss::engine::guid source(input.prefab_source_node_uuid);
  return gneiss::editor::apply_runtime_transform_to_author(
      session, history, {uuid, instance, source, input.local_transform});
}

int main() try {
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
  constexpr std::string_view missing_uri = "asset://scenes/missing.scene.json";
  const std::string asset_root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  gneiss::runtime::application_configuration desc;
  desc.asset_root = asset_root;
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::engine::world* world = nullptr;
  gneiss::editor::scene_document session;
  if (gneiss::engine::error{gneiss::runtime::application::create(desc, application)} !=
          gneiss::engine::error::success ||
      (world = application->native_world()) == nullptr ||
      gneiss::test_support::open_document(session, *application, world, scene_uri) !=
          gneiss::engine::error::success ||
      !session.is_open() || session.is_dirty() || session.uri() != scene_uri ||
      session.nodes().size() != 2U || session.nodes()[0].display_name != "Camera" ||
      session.nodes()[1].display_name != session.nodes()[1].uuid.string() ||
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
  auto foreign = gneiss::engine::error::success;
  std::thread worker([&] { foreign = session.rename_node(session.nodes()[1].node, "Rejected"); });
  worker.join();
  if (foreign != gneiss::engine::error::invalid_state || session.is_dirty())
    return 22;
  auto native = application;
  if (session.adopt(*native, world, gneiss::scene_instance_id{}, scene_uri) !=
          gneiss::engine::error::invalid_argument ||
      session.scene_handle() != original_scene)
    return 23;
  if (session.select(session.nodes()[1].node) != gneiss::engine::error::success ||
      session.selected_node() == nullptr) {
    return 2;
  }
  session.mark_dirty();
  if (!session.is_dirty() ||
      gneiss::test_support::open_document(session, *application, world, missing_uri) !=
          gneiss::engine::error::not_found ||
      !session.is_open() || !session.is_dirty() || session.uri() != scene_uri ||
      session.selected_node() == nullptr) {
    return 3;
  }
  const auto selected = session.selection();
  if (gneiss::engine::error{application->native_world()->node_destroy(selected)} !=
          gneiss::engine::error::success ||
      session.validate_selection() != gneiss::engine::error::invalid_handle ||
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
  if (gneiss::test_support::open_document(session, *application, world, prefab_scene_uri) !=
          gneiss::engine::error::success ||
      session.nodes().size() != 1U || session.prefab_nodes().size() != 2U ||
      !session.prefab_nodes()[0].is_instance_root || session.prefab_nodes()[0].is_read_only ||
      session.prefab_nodes()[1].is_instance_root || !session.prefab_nodes()[1].is_read_only ||
      session.prefab_nodes()[1].parent != session.prefab_nodes()[0].node) {
    return 6;
  }
  if (session.select(session.prefab_nodes()[1].node) != gneiss::engine::error::success ||
      session.selected_node() != nullptr || session.selected_prefab_node() == nullptr) {
    return 7;
  }
  auto source_moved = session.selected_prefab_node()->local_transform;
  source_moved.translation[1] = 5.0F;
  if (session.set_local_transform(session.selection(), source_moved) !=
          gneiss::engine::error::success ||
      session.selected_prefab_node() == nullptr ||
      session.selected_prefab_node()->local_transform.translation[1] != 5.0F ||
      (session.selected_prefab_node()->override_flags &
       static_cast<std::uint32_t>(gneiss::scene_prefab_flags::translation_overridden)) == 0U) {
    return 7;
  }
  gneiss::engine::math::transform previous_source{};
  if (session.restore_prefab_transform_field(session.selection(),
                                             gneiss::engine::transform_translation_field.get(),
                                             previous_source) != gneiss::engine::error::success ||
      previous_source.translation[1] != 5.0F || session.selected_prefab_node() == nullptr ||
      session.selected_prefab_node()->local_transform.translation[0] != 1.0F ||
      session.selected_prefab_node()->local_transform.translation[1] != 0.0F ||
      (session.selected_prefab_node()->override_flags &
       static_cast<std::uint32_t>(gneiss::scene_prefab_flags::translation_overridden)) != 0U ||
      session.set_local_transform(session.selection(), previous_source) !=
          gneiss::engine::error::success ||
      (session.selected_prefab_node()->override_flags &
       static_cast<std::uint32_t>(gneiss::scene_prefab_flags::translation_overridden)) == 0U ||
      session.restore_prefab_transform(session.selection(), previous_source) !=
          gneiss::engine::error::success ||
      session.selected_prefab_node()->override_flags != 0U) {
    return 8;
  }
  const auto* prefab_source = session.selected_prefab_node();
  const gneiss::engine::guid source_uuid = prefab_source->source_node_uuid;
  const gneiss::engine::guid instance_uuid = prefab_source->instance_uuid;
  gneiss::editor::runtime_author_transform runtime_source;
  runtime_source.uuid = source_uuid;
  runtime_source.prefab_instance_uuid = instance_uuid;
  runtime_source.prefab_source_node_uuid = source_uuid;
  runtime_source.local_transform = prefab_source->local_transform;
  runtime_source.local_transform.scale[0] = 2.0F;
  gneiss::editor::editor_command_history history;
  if (apply_temporary_transform(session, history, runtime_source) !=
          gneiss::engine::error::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 2.0F ||
      (session.selected_prefab_node()->override_flags &
       static_cast<std::uint32_t>(gneiss::scene_prefab_flags::scale_overridden)) == 0U ||
      history.undo() != gneiss::engine::error::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 1.0F ||
      history.redo() != gneiss::engine::error::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 2.0F) {
    return 9;
  }
  auto invalid_runtime_source = runtime_source;
  invalid_runtime_source.prefab_instance_uuid = {};
  auto stale_runtime_source = runtime_source;
  stale_runtime_source.prefab_instance_uuid =
      *gneiss::engine::guid::parse("ffffffff-ffff-4fff-8fff-ffffffffffff");
  if (gneiss::editor::apply_runtime_transform_to_author(session, history, invalid_runtime_source) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::editor::apply_runtime_transform_to_author(session, history, stale_runtime_source) !=
          gneiss::engine::error::not_found) {
    return 9;
  }
  gneiss::scene_node_id prefab_root;
  if (gneiss::test_support::create_prefab_document(*application, session, "Second Lamp", prefab_uri,
                                                   session.nodes()[0].node,
                                                   prefab_root) != gneiss::engine::error::success ||
      !session.is_dirty() || session.prefab_nodes().size() != 4U ||
      session.selected_prefab_node() == nullptr ||
      !session.selected_prefab_node()->is_instance_root ||
      session.selected_prefab_node()->node != prefab_root) {
    return 10;
  }
  gneiss::editor::editor_prefab_edit removal;
  if (removal.request_remove(*application, session, history, prefab_root).failed() ||
      gneiss::test_support::drive_prefab_edit(*application, removal,
                                              gneiss::test_support::application_tasks())
          .failed() ||
      session.prefab_nodes().size() != 2U || session.selection().is_valid() ||
      gneiss::test_support::finish_history(history, history.undo()).failed() ||
      session.prefab_nodes().size() != 4U || !session.selected_prefab_node())
    return 11;
  prefab_root = session.selected_prefab_node()->node;
  if (session.rename_node(prefab_root, "Restored Lamp").failed())
    return 11;
  auto moved = session.selected_prefab_node()->local_transform;
  moved.translation[1] = 3.0F;
  const auto stale_root = prefab_root;
  if (session.set_local_transform(prefab_root, moved) != gneiss::engine::error::success ||
      gneiss::test_support::refresh_prefab_document(*application, session, history, prefab_root) !=
          gneiss::engine::error::success ||
      session.selected_prefab_node()->node == stale_root ||
      session.selected_prefab_node() == nullptr ||
      session.selected_prefab_node()->display_name != "Restored Lamp" ||
      session.selected_prefab_node()->local_transform.translation[1] != 3.0F ||
      gneiss::test_support::finish_history(history, history.undo()) !=
          gneiss::engine::error::success ||
      gneiss::test_support::finish_history(history, history.redo()) !=
          gneiss::engine::error::success) {
    return 12;
  }
  return 0;
} catch (...) {
  return 99;
}
