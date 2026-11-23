// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_prefab_edit.hpp"
#include "../support/scene_query.hpp"
#include "../support/value_result.hpp"
#include "documents/commands/runtime_author_apply.hpp"
#include "documents/scene_edit_session.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/resolve.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/scene/scene_types.hpp>
#include <gneiss/engine/function/scene/world.hpp>

#include <gneiss/engine/core/base/error.hpp>

#include <string>
#include <string_view>
#include <thread>

// 临时输入在返回后销毁；后续 Undo/Redo 必须使用命令持有的身份副本。
static gneiss::error
apply_temporary_transform(gneiss::editor::scene_edit_session& session,
                          gneiss::editor::editor_command_history& history,
                          const gneiss::editor::runtime_author_transform& input) {
  const gneiss::guid uuid(input.uuid);
  const gneiss::guid instance(input.prefab_instance_uuid);
  const gneiss::guid source(input.prefab_source_node_uuid);
  return gneiss::editor::apply_runtime_transform_to_author(
      session, history, {uuid, instance, source, input.local_transform});
}

int main() try {
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
  constexpr std::string_view missing_uri = "asset://scenes/missing.scene.json";
  const std::string asset_root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  gneiss::app::configuration desc;
  desc.executor = &gneiss::test_support::application_tasks();
  desc.asset_root = asset_root;
  std::shared_ptr<gneiss::app::application> application;
  gneiss::world_owner world_owner;
  gneiss::world* world = nullptr;
  gneiss::editor::scene_edit_session session;
  if (gneiss::error{gneiss::test_support::assign_value(gneiss::app::application::create(desc),
                                                       application)} != gneiss::error::success ||
      gneiss::test_support::assign_value(gneiss::create_native(), world_owner).failed() ||
      (world = world_owner.get()) == nullptr ||
      gneiss::test_support::open_document(session, *application, world, scene_uri) !=
          gneiss::error::success ||
      !session.is_open() || session.is_dirty() || session.uri() != scene_uri ||
      gneiss::test_support::scene_query(session).nodes.size() != 2U ||
      gneiss::test_support::scene_query(session).nodes[0].display_name != "Camera" ||
      gneiss::test_support::scene_query(session).nodes[1].display_name !=
          gneiss::test_support::scene_query(session).nodes[1].uuid.string() ||
      gneiss::test_support::scene_query(session).nodes[1].parent !=
          gneiss::test_support::scene_query(session).nodes[0].node) {
    return 1;
  }
  // 直接修改运行时不改变文档内容；查询和保存以文档版本为准。
  {
    auto before = session.capture_nodes();
    if (!before)
      return 30;
    const auto original = before->nodes.front();
    world->object(gneiss::entity_id{original.node.get()})->set_name("Direct");
    auto latest = session.find_node(original.uuid);
    if (!latest || latest->display_name != original.display_name ||
        before->nodes.front().display_name != original.display_name)
      return 32;
    auto transform = original.local_transform;
    transform.translation[0] += 7;
    if (world->find(gneiss::entity_id{(original.node).get()})
            .set_local_transform(transform)
            .failed())
      return 33;
    latest = session.find_node(original.uuid);
    if (!latest ||
        latest->local_transform.translation[0] != original.local_transform.translation[0])
      return 34;
    world->object(gneiss::entity_id{original.node.get()})->set_name(original.display_name);
    if (world->find(gneiss::entity_id{(original.node).get()})
            .set_local_transform(original.local_transform)
            .failed())
      return 35;
    const auto uuid = gneiss::guid::from_bytes({72, 93});
    auto destination = gneiss::scene_instance::borrow(*world, session.scene_handle().id());
    if (!destination)
      return 36;
    auto created = destination->create_object({"Direct child", {}}, world->find(original.node));
    if (!created || session.find_node(uuid) || session.select(created->entity()).ok())
      return 36;
    if (session.capture_nodes()->nodes.size() != before->nodes.size())
      return 37;
    created->destroy();
    if (created->is_valid() || session.is_dirty())
      return 38;
  }
  // 接管时拒绝同 World 的其他场景对象，失败不改变既有会话。
  {
    auto target = world->create_scene();
    if (!target)
      return 100;
    auto member = target->create_object({"Target"});
    auto content = session.capture_content();
    auto nodes = session.capture_nodes();
    if (!member || !content || !nodes || nodes->nodes.empty())
      return 101;
    const auto& foreign_object = nodes->nodes.front();
    const gneiss::scene_object_binding wrong{foreign_object.uuid, {}, foreign_object.entity};
    auto before = session.scene_handle();
    if (session.adopt(*application, *target, *content, {&wrong, 1}) !=
            gneiss::error::invalid_handle ||
        session.scene_handle() != before || !session.is_open() || !member->is_valid())
      return 102;
    target->unload();
  }
  // 外部卸载立即使编辑会话失效，关闭不能影响另一场景。
  {
    gneiss::editor::scene_edit_session transient;
    if (transient.create_empty(*application, world, gneiss::guid::generate()).failed())
      return 104;
    auto instance = gneiss::scene_instance::borrow(*world, transient.scene_handle().id());
    if (!instance)
      return 105;
    instance->unload();
    if (transient.is_open())
      return 105;
    transient.close();
    if (!session.is_open())
      return 106;
  }
  // 原生会话移动只转移一次释放责任；外线程编辑不得改变文档。
  const auto original_scene = session.scene_handle();
  gneiss::editor::scene_edit_session moved_session{std::move(session)};
  if (session.is_open() || !moved_session.is_open() ||
      moved_session.scene_handle() != original_scene)
    return 20;
  session = std::move(moved_session);
  if (moved_session.is_open() || !session.is_open())
    return 21;
  auto foreign = gneiss::error::success;
  const auto foreign_node = gneiss::test_support::scene_query(session).nodes[1].node;
  std::thread worker([&] { foreign = session.rename_node(foreign_node, "Rejected"); });
  worker.join();
  if (foreign != gneiss::error::invalid_state || session.is_dirty())
    return 22;
  auto native = application;
  if (session.adopt(*native, gneiss::scene_instance{}, {}, {}) != gneiss::error::invalid_argument ||
      session.scene_handle() != original_scene)
    return 23;
  if (session.select(gneiss::test_support::scene_query(session).nodes[1].node) !=
          gneiss::error::success ||
      session.selected_node().failed()) {
    return 2;
  }
  session.mark_dirty();
  if (!session.is_dirty() ||
      gneiss::test_support::open_document(session, *application, world, missing_uri) !=
          gneiss::error::not_found ||
      !session.is_open() || !session.is_dirty() || session.uri() != scene_uri ||
      session.selected_node().failed()) {
    return 3;
  }
  const auto selected = session.selection();
  world->find(selected).destroy();
  if (session.validate_selection() != gneiss::error::success || session.selection().is_valid()) {
    return 4;
  }
  session.close();
  if (session.is_open() || session.is_dirty() ||
      !gneiss::test_support::scene_query(session).nodes.empty() ||
      !gneiss::test_support::scene_query(session).prefab_nodes.empty() ||
      session.selection().is_valid()) {
    return 5;
  }
  constexpr std::string_view prefab_scene_uri = "asset://scenes/prefab.scene.json";
  constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
  if (gneiss::test_support::open_document(session, *application, world, prefab_scene_uri) !=
          gneiss::error::success ||
      gneiss::test_support::scene_query(session).nodes.size() != 1U ||
      gneiss::test_support::scene_query(session).prefab_nodes.size() != 2U ||
      !gneiss::test_support::scene_query(session).prefab_nodes[0].is_instance_root ||
      gneiss::test_support::scene_query(session).prefab_nodes[1].is_instance_root ||
      gneiss::test_support::scene_query(session).prefab_nodes[1].parent !=
          gneiss::test_support::scene_query(session).prefab_nodes[0].node) {
    return 6;
  }
  if (session.select(gneiss::test_support::scene_query(session).prefab_nodes[1].node) !=
          gneiss::error::success ||
      session.selected_node().ok() || session.selected_prefab_node().failed()) {
    return 7;
  }
  auto source_moved = session.selected_prefab_node()->local_transform;
  source_moved.translation[1] = 5.0F;
  source_moved.scale[0] = 2.0F;
  if (session.set_local_transform(session.selection(), source_moved) != gneiss::error::success ||
      session.selected_prefab_node().failed() ||
      session.selected_prefab_node()->local_transform.translation[1] != 5.0F ||
      !session.selected_prefab_node()->has_override(gneiss::math::transform::type_guid,
                                                    gneiss::meta::resolve<gneiss::math::transform>()
                                                        .capability<gneiss::meta::cap::object>()
                                                        .find_property("translation")
                                                        .id())) {
    return 7;
  }
  gneiss::math::transform previous_source{};
  previous_source.translation[0] = 42.0F;
  if (session.restore_prefab_transform_field(session.selection(), 0U, previous_source) !=
          gneiss::error::invalid_argument ||
      previous_source.translation[0] != 42.0F ||
      session.selected_prefab_node()->local_transform.translation[1] != 5.0F) {
    return 8;
  }
  if (session.restore_prefab_transform_field(session.selection(),
                                             gneiss::meta::resolve<gneiss::math::transform>()
                                                 .capability<gneiss::meta::cap::object>()
                                                 .find_property("translation")
                                                 .id()
                                                 .get(),
                                             previous_source) != gneiss::error::success ||
      previous_source.translation[1] != 5.0F || session.selected_prefab_node().failed() ||
      session.selected_prefab_node()->local_transform.translation[0] != 1.0F ||
      session.selected_prefab_node()->local_transform.translation[1] != 0.0F ||
      session.selected_prefab_node()->local_transform.scale[0] != 2.0F ||
      session.selected_prefab_node()->has_override(gneiss::math::transform::type_guid,
                                                   gneiss::meta::resolve<gneiss::math::transform>()
                                                       .capability<gneiss::meta::cap::object>()
                                                       .find_property("translation")
                                                       .id()) ||
      session.set_local_transform(session.selection(), previous_source) != gneiss::error::success ||
      !session.selected_prefab_node()->has_override(gneiss::math::transform::type_guid,
                                                    gneiss::meta::resolve<gneiss::math::transform>()
                                                        .capability<gneiss::meta::cap::object>()
                                                        .find_property("translation")
                                                        .id()) ||
      session.restore_prefab_transform(session.selection(), previous_source) !=
          gneiss::error::success ||
      session.selected_prefab_node()->has_overrides(gneiss::math::transform::type_guid)) {
    return 8;
  }
  const auto prefab_source = session.selected_prefab_node();
  const gneiss::guid source_uuid = prefab_source->source_node_uuid;
  const gneiss::guid instance_uuid = prefab_source->instance_uuid;
  gneiss::editor::runtime_author_transform runtime_source;
  runtime_source.uuid = source_uuid;
  runtime_source.prefab_instance_uuid = instance_uuid;
  runtime_source.prefab_source_node_uuid = source_uuid;
  runtime_source.local_transform = prefab_source->local_transform;
  runtime_source.local_transform.scale[0] = 2.0F;
  gneiss::editor::editor_command_history history;
  if (apply_temporary_transform(session, history, runtime_source) != gneiss::error::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 2.0F ||
      !session.selected_prefab_node()->has_override(gneiss::math::transform::type_guid,
                                                    gneiss::meta::resolve<gneiss::math::transform>()
                                                        .capability<gneiss::meta::cap::object>()
                                                        .find_property("scale")
                                                        .id()) ||
      history.undo() != gneiss::error::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 1.0F ||
      history.redo() != gneiss::error::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 2.0F) {
    return 9;
  }
  auto invalid_runtime_source = runtime_source;
  invalid_runtime_source.prefab_instance_uuid = {};
  auto stale_runtime_source = runtime_source;
  stale_runtime_source.prefab_instance_uuid =
      *gneiss::guid::parse("ffffffff-ffff-4fff-8fff-ffffffffffff");
  if (gneiss::editor::apply_runtime_transform_to_author(session, history, invalid_runtime_source) !=
          gneiss::error::invalid_argument ||
      gneiss::editor::apply_runtime_transform_to_author(session, history, stale_runtime_source) !=
          gneiss::error::not_found) {
    return 9;
  }
  gneiss::entity_id prefab_root;
  if (gneiss::test_support::create_prefab_document(
          *application, session, "Second Lamp", prefab_uri,
          gneiss::test_support::scene_query(session).nodes[0].node,
          prefab_root) != gneiss::error::success ||
      !session.is_dirty() || gneiss::test_support::scene_query(session).prefab_nodes.size() != 4U ||
      session.selected_prefab_node().failed() ||
      !session.selected_prefab_node()->is_instance_root ||
      session.selected_prefab_node()->node != prefab_root) {
    return 10;
  }
  gneiss::editor::editor_prefab_edit removal;
  if (removal.request_remove(*application, session, history, prefab_root).failed() ||
      gneiss::test_support::drive_prefab_edit(*application, removal,
                                              gneiss::test_support::application_tasks())
          .failed() ||
      gneiss::test_support::scene_query(session).prefab_nodes.size() != 2U ||
      session.selection().is_valid() ||
      gneiss::test_support::finish_history(history, history.undo()).failed() ||
      gneiss::test_support::scene_query(session).prefab_nodes.size() != 4U ||
      !session.selected_prefab_node())
    return 11;
  prefab_root = session.selected_prefab_node()->node;
  if (session.rename_node(prefab_root, "Restored Lamp").failed())
    return 11;
  auto moved = session.selected_prefab_node()->local_transform;
  moved.translation[1] = 3.0F;
  const auto retained_root = prefab_root;
  if (session.set_local_transform(prefab_root, moved) != gneiss::error::success ||
      gneiss::test_support::refresh_prefab_document(*application, session, history, prefab_root) !=
          gneiss::error::success ||
      session.selected_prefab_node()->node != retained_root ||
      session.selected_prefab_node().failed() ||
      session.selected_prefab_node()->display_name != "Restored Lamp" ||
      session.selected_prefab_node()->local_transform.translation[1] != 3.0F ||
      gneiss::test_support::finish_history(history, history.undo()) != gneiss::error::success ||
      gneiss::test_support::finish_history(history, history.redo()) != gneiss::error::success) {
    return 12;
  }
  return 0;
} catch (...) {
  return 99;
}
