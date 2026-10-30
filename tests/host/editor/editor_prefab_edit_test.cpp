// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/async_prefab_edit.hpp"
#include "../../support/value_result.hpp"
#include "commands/editor_prefab_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace {
using namespace gneiss;
void check(bool value, std::source_location location = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("Prefab 异步编辑验证失败：" + std::to_string(location.line()));
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-prefab-edit-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() {
    std::filesystem::create_directories(root);
    std::ofstream(root / "mesh.json")
        << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})";
    std::ofstream(root / "material.json")
        << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["","","","",""]})";
    std::ofstream(root / "scene.json")
        << R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[]})";
    std::ofstream prefab(root / "prefab.json");
    prefab
        << R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"30000000-0000-4000-8000-000000000001","root_uuid":"30000000-0000-4000-8000-000000000151","objects":[)";
    for (unsigned i = 0; i < 150U; ++i) {
      char uuid[37]{};
      std::snprintf(uuid, sizeof(uuid), "30000000-0000-4000-8000-%012u", i + 2U);
      if (i != 0U) {
        prefab << ',';
      }
      prefab
          << "{\"uuid\":\"" << uuid << "\",\"name\":\"Source\",\"parent\":"
          << (i == 149U ? "null" : "\"30000000-0000-4000-8000-000000000151\"")
          << R"(,"transform":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://mesh.json","material":"asset://material.json"}}]})";
    }
    prefab << "]}";
  }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
void run_refresh(gneiss::engine::execution_mode mode) {
  fixture files;
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration desc;
  const auto path = files.root.string();
  desc.asset_root = path;
  check(gneiss::engine::error{gneiss::runtime::application::create(desc, app)} ==
        gneiss::engine::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::error::success);
  gneiss::engine::world* world{};
  check((world = app->native_world()) != nullptr);
  editor::scene_document session;
  editor::editor_command_history history;
  editor::editor_prefab_edit edit;
  check(gneiss::test_support::open_document(session, *app, world, "asset://scene.json",
                                            &scheduler) == gneiss::engine::error::success);
  scene_node_id first, second;
  check(test_support::create_prefab_document(*app, session, "First", "asset://prefab.json", {},
                                             first, &scheduler) == gneiss::engine::error::success);
  check(test_support::create_prefab_document(*app, session, "Second", "asset://prefab.json", {},
                                             second, &scheduler) == gneiss::engine::error::success);
  // 成员允许交错，不能假设所有实例根排列在展开成员之前。
  const auto first_record =
      std::ranges::find(session.prefab_nodes(), first, &editor::prefab_node_record::node);
  const auto second_record =
      std::ranges::find(session.prefab_nodes(), second, &editor::prefab_node_record::node);
  check(first_record != session.prefab_nodes().end() &&
        second_record != session.prefab_nodes().end());
  const auto first_uuid = first_record->instance_uuid;
  const auto second_uuid = second_record->instance_uuid;
  const auto source_record = std::ranges::find_if(session.prefab_nodes(), [&](const auto& record) {
    return record.instance_uuid == first_uuid && !record.is_instance_root;
  });
  check(source_record != session.prefab_nodes().end());
  const auto source_uuid = source_record->source_node_uuid;
  const auto old_selected = session.find_prefab_source(first_uuid, source_uuid)->node;
  auto transform = session.find_prefab_source(first_uuid, source_uuid)->local_transform;
  transform.translation[0] = 17.0F;
  check(session.set_local_transform(old_selected, transform) == gneiss::engine::error::success);
  check(session.select(old_selected) == gneiss::engine::error::success);
  const auto step = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    app->advance_scene_load();
    edit.advance();
    std::this_thread::yield();
  };
  const auto drive = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (edit.active()) {
      step();
      check(std::chrono::steady_clock::now() < deadline);
    }
  };
  const auto drive_history = [&](gneiss::engine::error expected = gneiss::engine::error::success) {
    gneiss::engine::error outcome = gneiss::engine::error::not_ready;
    unsigned turns{};
    while (history.is_pending()) {
      outcome = history.advance_pending();
      check(++turns < 1000U);
    }
    check(outcome == expected);
  };
  std::ifstream input(files.root / "prefab.json");
  std::string updated{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  input.close();
  for (std::size_t position{};
       (position = updated.find("Source", position)) != std::string::npos;) {
    updated.replace(position, 6U, "Updated");
    position += 7U;
  }
  std::ofstream(files.root / "prefab.json") << updated;
  check(edit.request_refresh(*app, scheduler, session, history, first) ==
        gneiss::engine::error::success);
  // 异步来源准备仍可取消；同步实例化开始后不提供跨帧取消点。
  check(edit.active() && edit.can_cancel());
  edit.cancel();
  drive();
  check(history.size() == 0U && session.selection() == old_selected);
  check(session.find_prefab_root(first_uuid)->node == first &&
        session.find_prefab_root(second_uuid)->node == second);
  check(edit.request_refresh(*app, scheduler, session, history, first) ==
        gneiss::engine::error::success);
  drive();
  if (edit.operation().failed())
    std::fprintf(stderr, "refresh result=%d nodes=%zu history=%zu\n", edit.operation().code(),
                 session.prefab_nodes().size(), history.size());
  check(edit.operation() == gneiss::engine::error::success && history.size() == 1U &&
        session.prefab_nodes().size() == 300U);
  check(session.find_prefab_root(first_uuid)->node != first &&
        session.find_prefab_root(second_uuid)->node != second);
  check(session.find_prefab_source(first_uuid, source_uuid)->local_transform.translation[0] ==
        17.0F);
  check(session.find_prefab_source(second_uuid, source_uuid)->local_transform.translation[0] ==
        1.0F);
  check(session.find_prefab_source(first_uuid, source_uuid)->display_name == "Updated");
  check(session.find_prefab_source(second_uuid, source_uuid)->display_name == "Updated");
  check(session.selection() == session.find_prefab_source(first_uuid, source_uuid)->node);
  // 历史保留真实资源版本；即使所有源文件失效也能撤销和重做。
  std::ofstream(files.root / "prefab.json") << "broken";
  std::ofstream(files.root / "mesh.json") << "broken";
  std::ofstream(files.root / "material.json") << "broken";
  check(history.undo() == gneiss::engine::error::not_ready);
  drive_history();
  check(session.find_prefab_source(first_uuid, source_uuid)->display_name == "Source");
  check(session.find_prefab_source(second_uuid, source_uuid)->display_name == "Source");
  check(session.selection() == session.find_prefab_source(first_uuid, source_uuid)->node);
  check(history.redo() == gneiss::engine::error::not_ready);
  drive_history();
  check(session.find_prefab_source(first_uuid, source_uuid)->display_name == "Updated");
  check(session.find_prefab_source(first_uuid, source_uuid)->local_transform.translation[0] ==
        17.0F);
  const auto preserved = session.selection();
  check(history.undo() == gneiss::engine::error::not_ready);
  history.cancel_pending();
  drive_history(gneiss::engine::error::invalid_state);
  check(session.selection() == preserved && !history.can_redo());
  check(edit.request_refresh(*app, scheduler, session, history,
                             session.find_prefab_root(first_uuid)->node) ==
        gneiss::engine::error::success);
  drive();
  check(edit.operation().failed() && history.size() == 1U && session.selection() == preserved);
  history.clear();
  std::ofstream(files.root / "mesh.json")
      << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})";
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["","","","",""]})";
  // 第二个实例覆盖失效：第一个替换保留，第二个旧实例删除，镜像反映部分结果。
  const gneiss::engine::guid removed_uuid =
      *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000003");
  const auto second_node = session.find_prefab_source(second_uuid, removed_uuid)->node;
  const auto original = session.find_prefab_source(second_uuid, removed_uuid)->local_transform;
  auto changed = original;
  changed.translation[1] = 29.0F;
  check(session.set_local_transform(second_node, changed) == gneiss::engine::error::success);
  const auto first_root = session.find_prefab_root(first_uuid)->node;

  const auto removed_start = updated.find(R"(,{"uuid":")");
  const auto removed_end = updated.find(R"(,{"uuid":")", removed_start + 1U);
  check(removed_start != std::string::npos && removed_end != std::string::npos);
  updated.erase(removed_start, removed_end - removed_start);
  std::ofstream(files.root / "prefab.json") << updated;
  check(edit.request_refresh(*app, scheduler, session, history, first_root) ==
        gneiss::engine::error::success);
  drive();
  check(edit.operation() == gneiss::engine::error::not_found && history.size() == 0U);
  check(session.find_prefab_root(first_uuid)->node != first_root);
  check(session.find_prefab_root(second_uuid) == nullptr);
  check(session.find_prefab_source(first_uuid, removed_uuid) == nullptr);
  check(session.prefab_nodes().size() == 149U && session.is_dirty());
  std::uint64_t count{};
  check(gneiss::test_support::assign_value(app->native_world()->entity_count(), count).ok() &&
        count == 149U);
  gneiss::engine::math::transform invalidated;
  check(app->native_world()->node_get_local_transform(second_node, invalidated) ==
        gneiss::engine::error::invalid_handle);
  // 部分失败后仍能再次编辑，并对后续成功操作执行撤销。
  check(edit.request_refresh(*app, scheduler, session, history,
                             session.find_prefab_root(first_uuid)->node)
            .ok());
  drive();
  check(edit.operation().ok() && history.size() == 1U);
  check(history.undo() == gneiss::engine::error::not_ready);
  drive_history();
  check(session.prefab_nodes().size() == 149U);
  history.clear();
  check(edit.request_refresh(*app, scheduler, session, history,
                             session.find_prefab_root(first_uuid)->node) ==
        gneiss::engine::error::success);
  session.close();
  drive();
  check(edit.operation() == gneiss::engine::error::invalid_state && history.size() == 0U);
}
void run(gneiss::engine::execution_mode mode) {
  fixture files;
  std::ifstream input(files.root / "prefab.json");
  const std::string source{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  input.close();
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration desc;
  const auto root = files.root.string();
  desc.asset_root = root;
  check(gneiss::engine::error{gneiss::runtime::application::create(desc, app)} ==
        gneiss::engine::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::error::success);
  gneiss::engine::world* world{};
  check((world = app->native_world()) != nullptr);
  editor::scene_document session;
  editor::editor_command_history history;
  editor::editor_prefab_edit edit;
  check(gneiss::test_support::open_document(session, *app, world, "asset://scene.json",
                                            &scheduler) == gneiss::engine::error::success);
  const auto request = [&](std::string_view uri = "asset://prefab.json") {
    return edit.request(*app, scheduler, session, history, "Placed", uri, {});
  };
  const auto step = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    app->advance_scene_load();
    edit.advance();
    std::this_thread::yield();
  };
  const auto drive = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (edit.active()) {
      step();
      check(std::chrono::steady_clock::now() < deadline);
    }
  };
  const auto drive_history = [&](gneiss::engine::error expected = gneiss::engine::error::success) {
    gneiss::engine::error outcome = gneiss::engine::error::not_ready;
    unsigned steps{};
    while (history.is_pending()) {
      outcome = history.advance_pending();
      check(++steps < 1000U);
    }
    check(outcome == expected);
  };
  // 已加载实例支持直接删除与基于快照的历史恢复。
  scene_node_id legacy;
  check(test_support::create_prefab_document(*app, session, "Legacy", "asset://prefab.json", {},
                                             legacy, &scheduler) == gneiss::engine::error::success);
  check(edit.request_remove(*app, session, history, legacy) == gneiss::engine::error::success);
  drive();
  check(edit.operation() == gneiss::engine::error::success && session.prefab_nodes().empty());
  check(history.undo() == gneiss::engine::error::not_ready);
  drive_history();
  check(session.prefab_nodes().size() == 150U);
  check(history.redo() == gneiss::engine::error::not_ready);
  drive_history();
  check(session.prefab_nodes().empty());
  history.clear();
  check(request() == gneiss::engine::error::success);
  const auto preparation_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (edit.active() && edit.phase() != editor::prefab_edit_phase::constructing) {
    step();
    check(std::chrono::steady_clock::now() < preparation_deadline);
  }
  check(edit.active());
  std::uint64_t entity_count{};
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            app->native_world()->entity_count(), entity_count)} == gneiss::engine::error::success &&
        entity_count == 0U);
  edit.cancel();
  drive();
  check(!edit.visible() && session.prefab_nodes().empty() && history.size() == 0U);
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            app->native_world()->entity_count(), entity_count)} == gneiss::engine::error::success &&
        entity_count == 0U);
  edit.dismiss();
  check(request() == gneiss::engine::error::success && edit.active());
  check(request() == gneiss::engine::error::not_ready);
  check(session.prefab_nodes().empty() && history.size() == 0U);
  drive();
  if (edit.operation().failed()) {
    std::fprintf(stderr, "placement result=%d\n", (edit.operation()).code());
  }
  check(edit.operation() == gneiss::engine::error::success && !edit.visible());
  check(session.prefab_nodes().size() == 150U && history.size() == 1U);
  const auto uuid = session.prefab_nodes().front().instance_uuid;
  // 删除全部来源文件后，历史仍持有来源描述及实际 Mesh/Material 版本。
  std::ofstream(files.root / "prefab.json") << "broken";
  std::ofstream(files.root / "mesh.json") << "broken";
  std::ofstream(files.root / "material.json") << "broken";
  check(history.undo() == gneiss::engine::error::not_ready && history.is_pending());
  check(session.prefab_nodes().size() == 150U);
  history.cancel_pending();
  drive_history(gneiss::engine::error::invalid_state);
  check(session.prefab_nodes().size() == 150U && history.can_undo());
  check(history.undo() == gneiss::engine::error::not_ready);
  drive_history();
  check(session.prefab_nodes().empty() && history.can_redo());
  check(history.redo() == gneiss::engine::error::not_ready);
  check(session.prefab_nodes().empty());
  history.cancel_pending();
  drive_history(gneiss::engine::error::invalid_state);
  check(session.prefab_nodes().empty() && history.can_redo());
  check(history.redo() == gneiss::engine::error::not_ready);
  drive_history();
  check(session.prefab_nodes().size() == 150U &&
        session.prefab_nodes().front().instance_uuid == uuid);
  // 删除保存实际版本和覆盖；恢复不依赖来源文件或旧节点 ID。
  editor::editor_command_history deletion_history;
  auto root_node = session.find_prefab_root(uuid)->node;
  const auto source_uuid = session.prefab_nodes()[1U].source_node_uuid;
  auto local = session.find_prefab_source(uuid, source_uuid)->local_transform;
  local.translation[0] = 17.0F;
  check(session.set_local_transform(session.find_prefab_source(uuid, source_uuid)->node, local) ==
        gneiss::engine::error::success);
  auto root_local = session.find_prefab_root(uuid)->local_transform;
  root_local.translation[1] = 7.0F;
  check(session.set_local_transform(root_node, root_local) == gneiss::engine::error::success);
  check(session.rename_node(root_node, "Renamed") == gneiss::engine::error::success);
  scene_node_id own_child;
  check(session.create_node("Own child", root_node, own_child).ok());
  check(edit.request_remove(*app, session, deletion_history, root_node).ok());
  drive();
  check(edit.operation().ok() && deletion_history.size() == 1U && session.prefab_nodes().empty());
  check(deletion_history.undo() == gneiss::engine::error::not_ready);
  while (deletion_history.is_pending())
    check(deletion_history.advance_pending().ok());
  root_node = session.find_prefab_root(uuid)->node;
  check(session.nodes().size() == 1U);
  const auto restored_child = session.nodes().front().node;
  scene_node_id retained_parent;
  check(world->node_get_parent(restored_child, retained_parent).ok() &&
        retained_parent == root_node && restored_child != own_child);
  check(app->scenes()->destroy_node(session.scene_handle(), restored_child).ok());
  check(session.synchronize_nodes().ok());
  deletion_history.clear();

  check(edit.request_remove(*app, session, deletion_history, root_node) ==
        gneiss::engine::error::success);
  step();
  edit.cancel();
  drive();
  check(deletion_history.size() == 0U && session.prefab_nodes().size() == 150U);
  check(edit.request_remove(*app, session, deletion_history, root_node) ==
        gneiss::engine::error::success);
  drive();
  check(edit.operation() == gneiss::engine::error::success && session.prefab_nodes().empty() &&
        deletion_history.size() == 1U);
  const auto restore_deleted = [&] {
    check(deletion_history.undo() == gneiss::engine::error::not_ready);
    gneiss::engine::error restored = gneiss::engine::error::not_ready;
    unsigned turns{};
    while (deletion_history.is_pending()) {
      restored = deletion_history.advance_pending();
      check(++turns < 1000U);
    }
    check(restored == gneiss::engine::error::success);
    check(session.find_prefab_root(uuid)->display_name == "Renamed");
    check(session.find_prefab_root(uuid)->local_transform.translation[1] == 7.0F);
    check(session.find_prefab_source(uuid, source_uuid)->local_transform.translation[0] == 17.0F);
    check(session.find_prefab_source(uuid, source_uuid)->override_flags != 0U);
  };
  restore_deleted();
  check(session.find_prefab_root(uuid)->node != root_node);
  check(deletion_history.redo() == gneiss::engine::error::not_ready);
  while (deletion_history.is_pending()) {
    (void)deletion_history.advance_pending();
  }
  check(session.prefab_nodes().empty());
  restore_deleted();
  deletion_history.clear();
  check(request() == gneiss::engine::error::success);
  drive();
  check(edit.operation().failed() && history.size() == 1U && session.prefab_nodes().size() == 150U);
  edit.dismiss();
  check(request("asset://missing.json") == gneiss::engine::error::success);
  edit.cancel();
  drive();
  check(!edit.visible() && session.prefab_nodes().size() == 150U);
  edit.dismiss();
  auto missing_source = source;
  for (std::size_t position{};
       (position = missing_source.find("asset://material.json", position)) != std::string::npos;) {
    missing_source.replace(position, std::string_view("asset://material.json").size(),
                           "asset://missing-material.json");
    ++position;
  }
  std::ofstream(files.root / "prefab.json") << missing_source;
  check(request() == gneiss::engine::error::success);
  drive();
  check(edit.operation().failed() && session.prefab_nodes().size() == 150U && history.size() == 1U);
  edit.dismiss();
  check(history.undo() == gneiss::engine::error::not_ready);
  // 历史操作在一次 advance 中完成；完成后取消不撤回已经发生的删除。
  check(history.advance_pending().ok() && !history.is_pending());
  history.cancel_pending();
  check(session.prefab_nodes().empty());
  history.clear();
  std::ofstream(files.root / "prefab.json") << source;
  check(request() == gneiss::engine::error::success);
  session.close();
  drive();
  check(edit.operation() == gneiss::engine::error::invalid_state && history.size() == 0U);
  edit.dismiss();
}
void run_mixed_subtree() {
  fixture files;
  engine::task_scheduler scheduler({.workers = 1U, .mode = engine::execution_mode::cooperative});
  std::shared_ptr<runtime::application> app;
  runtime::application_configuration config;
  const auto path = files.root.string();
  config.asset_root = path;
  check(runtime::application::create(config, app).ok());
  check(app->attach_task_executor(scheduler).ok());
  editor::scene_document document;
  check(test_support::open_document(document, *app, app->native_world(), "asset://scene.json",
                                    &scheduler)
            .ok());
  scene_node_id parent, first, second, child;
  check(document.create_node("Parent", {}, parent).ok());
  check(test_support::create_prefab_document(*app, document, "First", "asset://prefab.json", parent,
                                             first, &scheduler)
            .ok());
  check(test_support::create_prefab_document(*app, document, "Second", "asset://prefab.json",
                                             parent, second, &scheduler)
            .ok());
  check(document.create_node("Own child", first, child).ok());
  const auto first_uuid =
      std::ranges::find(document.prefab_nodes(), first, &editor::prefab_node_record::node)
          ->instance_uuid;
  const auto source = std::ranges::find_if(document.prefab_nodes(), [&](const auto& node) {
    return node.instance_uuid == first_uuid && !node.is_instance_root;
  });
  check(source != document.prefab_nodes().end());
  auto modified = source->local_transform;
  modified.translation[0] = 31;
  const auto source_uuid = source->source_node_uuid;
  check(document.set_local_transform(source->node, modified).ok());
  editor::scene_subtree_snapshot snapshot;
  check(document.destroy_subtree(parent, snapshot).ok());
  check(snapshot.description.objects.size() == 4U && snapshot.resources.size() == 2U);
  std::ofstream(files.root / "prefab.json") << "broken";
  std::ofstream(files.root / "mesh.json") << "broken";
  std::ofstream(files.root / "material.json") << "broken";
  // 来源已失效，恢复和复制仍只使用快照保活的实际资源版本。
  scene_node_id restored;
  auto invalid = snapshot;
  invalid.resources.erase(first_uuid);
  check(document.restore_subtree(invalid, restored) == engine::error::not_found);
  check(!restored && *app->native_world()->entity_count() == 0U);
  check(document.restore_subtree(snapshot, restored).ok() && restored != parent);
  check(document.find_prefab_source(first_uuid, source_uuid)->local_transform.translation[0] == 31);
  const auto new_first = document.find_prefab_root(first_uuid)->node;
  const auto own = std::ranges::find(document.nodes(), std::string("Own child"),
                                     &editor::scene_node_record::display_name);
  check(own != document.nodes().end() && own->parent == new_first && own->node != child);
  scene_node_id copy;
  check(document.duplicate_subtree(restored, {}, copy).ok() && copy != restored);
  engine::scene_subtree_snapshot copied;
  check(app->scenes()->capture_subtree(document.scene_handle(), copy, copied).ok());
  check(copied.description.objects.size() == 4U && copied.resources.size() == 2U);
  const auto copied_first = std::ranges::find(copied.description.objects, std::string("First"),
                                              &engine::object_description::name);
  const auto copied_child = std::ranges::find(copied.description.objects, std::string("Own child"),
                                              &engine::object_description::name);
  check(copied_first != copied.description.objects.end() &&
        copied_child != copied.description.objects.end());
  check(copied_child->parent_uuid == copied_first->uuid);
  const auto* copied_source = document.find_prefab_source(copied_first->uuid, source_uuid);
  check(copied_source && copied_source->local_transform.translation[0] == 31);
  auto independent = copied_source->local_transform;
  independent.translation[0] = 42;
  check(document.set_local_transform(copied_source->node, independent).ok());
  check(document.find_prefab_source(first_uuid, source_uuid)->local_transform.translation[0] == 31);

  for (const auto& object : copied.description.objects)
    check(std::ranges::find(snapshot.description.objects, object.uuid,
                            &engine::object_description::uuid) ==
          snapshot.description.objects.end());
  check(*app->native_world()->entity_count() == 604U);
  check(document.restore_subtree(snapshot, parent) == engine::error::invalid_argument);
  check(*app->native_world()->entity_count() == 604U);
  document.close();
  check(app->shutdown().ok());
}
}
int main() try {
  run_mixed_subtree();
  run_refresh(gneiss::engine::execution_mode::cooperative);
  run_refresh(gneiss::engine::execution_mode::thread_pool);
  run(gneiss::engine::execution_mode::cooperative);
  run(gneiss::engine::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
