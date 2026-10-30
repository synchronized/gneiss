// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/editor_asset_edit.hpp"
#include "commands/editor_prefab_edit.hpp"
#include "documents/editor_scene_loader.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

#include "engine/core/math/transform.hpp"

#include <stdexcept>
#include <string>

int main() try {
  using namespace gneiss;
  using namespace gneiss::editor;
  const std::string assets = GNEISS_EDITOR_TEST_ASSET_ROOT;
  gneiss::runtime::application_configuration desc;
  desc.asset_root = assets;
  engine::task_scheduler scheduler({.mode = engine::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::engine::world* world = nullptr;
  document_editing editing;
  auto& session = editing.document();
  auto& history = editing.history();
  editor_camera camera;
  editor_scene_loader loader;
  if (gneiss::engine::error{gneiss::runtime::application::create(desc, app)}.failed() ||
      (world = app->native_world()) == nullptr || app->attach_task_executor(scheduler).failed() ||
      camera.initialize().failed() ||
      loader.request(*app, editing, "asset://scenes/triangle.scene.json").failed()) {
    return 1;
  }
  const auto wait = [&](auto pending, auto advance) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (pending()) {
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("异步编辑超时");
      (void)scheduler.run_ready();
      app->advance_scene_load();
      advance();
      std::this_thread::yield();
    }
  };
  wait([&] { return loader.active() || app->visual_assets_loading(); },
       [&] {
         bool changed{};
         if (loader.advance(*app, editing, world, camera, changed).failed())
           throw std::runtime_error("场景加载失败");
       });
  if (loader.operation().failed())
    return 1;
  const auto add_mesh_asset = [&](scene_document& document, editor_command_history& commands,
                                  std::string_view name, mesh_asset_reference refs) {
    editor_asset_edit operation;
    auto status = operation.request_add(*app, document, commands, name, refs);
    if (status.failed())
      return status;
    wait([&] { return operation.active(); }, [&] { operation.advance(*app, document, commands); });
    return operation.operation();
  };
  const auto replace_mesh_assets = [&](scene_document& document, editor_command_history& commands,
                                       gneiss::engine::guid uuid, mesh_asset_reference refs) {
    editor_asset_edit operation;
    auto status = operation.request(*app, document, commands, uuid, refs);
    if (status.failed())
      return status;
    wait([&] { return operation.active(); }, [&] { operation.advance(*app, document, commands); });
    return operation.operation();
  };
  const auto add_prefab_asset = [&](scene_document& document, editor_command_history& commands,
                                    std::string_view name, std::string_view uri,
                                    scene_node_id parent) {
    editor_prefab_edit operation;
    auto status = operation.request(*app, scheduler, document, commands, name, uri, parent);
    if (status.failed())
      return status;
    wait([&] { return operation.active(); }, [&] { operation.advance(); });
    return operation.operation();
  };
  const auto run_history = [&](bool undo) {
    auto status = undo ? history.undo() : history.redo();
    wait([&] { return history.is_pending(); }, [&] { status = history.advance_pending(); });
    return status;
  };
  const auto initial_count = session.nodes().size();
  const auto parent = session.nodes().front().node;
  constexpr auto mesh = "asset://models/triangle.mesh.json";
  constexpr auto material = "asset://materials/triangle.material.json";
  constexpr auto prefab = "asset://prefabs/test.prefab.json";
  const auto add_temporary_mesh = [&] {
    const std::string name = "Mesh";
    const std::string mesh_uri = mesh;
    const std::string material_uri = material;
    return add_mesh_asset(session, history, name,
                          {.mesh_uri = mesh_uri, .material_uri = material_uri});
  };
  if (add_temporary_mesh().failed() || session.nodes().size() != initial_count + 1U ||
      !session.is_dirty() || history.size() != 1U) {
    return 2;
  }
  const auto uuid = session.selected_node()->uuid;
  if (session.select(parent).failed() || run_history(true).failed() ||
      session.find_node(uuid) != nullptr || run_history(false).failed() ||
      session.find_node(uuid) == nullptr || session.find_node(uuid)->display_name != "Mesh") {
    return 3;
  }
  if (replace_mesh_assets(
          session, history, uuid,
          {.mesh_uri = mesh, .material_uri = "asset://materials/missing.material.json"})
          .ok() ||
      history.size() != 1U || session.find_node(uuid)->material_uri != material ||
      replace_mesh_assets(
          session, history, *gneiss::engine::guid::parse("ffffffff-ffff-4fff-8fff-ffffffffffff"),
          {.mesh_uri = mesh, .material_uri = material}) != gneiss::engine::error::not_found) {
    return 4;
  }
  if (replace_mesh_assets(session, history, uuid, {.mesh_uri = mesh, .material_uri = material})
          .failed() ||
      history.size() != 1U || run_history(true).failed() || run_history(false).failed()) {
    return 5;
  }
  const auto add_temporary_prefab = [&] {
    const std::string name = "Prefab";
    const std::string uri = prefab;
    return add_prefab_asset(session, history, name, uri, parent);
  };
  if (add_temporary_prefab().failed() || session.selected_prefab_node() == nullptr) {
    return 6;
  }
  const auto prefab_uuid = session.selected_prefab_node()->instance_uuid;
  if (run_history(true).failed() || session.find_prefab_root(prefab_uuid) != nullptr ||
      run_history(false).failed() || session.find_prefab_root(prefab_uuid) == nullptr ||
      session.find_prefab_root(prefab_uuid)->parent != parent) {
    return 7;
  }
  // 容量耗尽时撤回刚创建的节点/实例，且不能把干净文档标脏。
  editor_command_history full{0U};
  session.clear_dirty();
  const auto nodes = session.nodes().size();
  const auto prefabs = session.prefab_nodes().size();
  const auto mesh_failed =
      add_mesh_asset(session, full, "Rollback Mesh", {.mesh_uri = mesh, .material_uri = material});
  if (mesh_failed != engine::error::invalid_argument || session.nodes().size() != nodes ||
      session.is_dirty()) {
    std::fprintf(stderr, "网格回滚：结果=%d 节点=%zu/%zu 脏状态=%d\n", mesh_failed.code(),
                 session.nodes().size(), nodes, session.is_dirty());
    return 8;
  }
  const auto prefab_failed = add_prefab_asset(session, full, "Rollback Prefab", prefab, parent);
  if (prefab_failed != engine::error::invalid_argument ||
      session.prefab_nodes().size() != prefabs || session.is_dirty() || full.can_undo()) {
    std::fprintf(stderr, "Prefab 回滚：结果=%d 节点=%zu/%zu 脏状态=%d\n", prefab_failed.code(),
                 session.prefab_nodes().size(), prefabs, session.is_dirty());
    return 8;
  }
  // 相同资源是无操作，不消耗历史容量，也不把文档标脏。
  if (replace_mesh_assets(session, full, uuid, {.mesh_uri = mesh, .material_uri = material}) !=
          gneiss::engine::error::success ||
      session.is_dirty() || session.find_node(uuid)->material_uri != material) {
    return 9;
  }
  editor_asset_edit removal;
  engine::scene_mesh_binding removed_binding, restored_binding;
  if (app->capture_mesh_binding(session.scene_handle(), session.find_node(uuid)->node,
                                removed_binding)
          .failed() ||
      removal.request_remove(*app, session, history, uuid).failed() ||
      !session.find_node(uuid)->mesh_uri.empty() || run_history(true).failed() ||
      app->capture_mesh_binding(session.scene_handle(), session.find_node(uuid)->node,
                                restored_binding)
          .failed() ||
      (session.find_node(uuid)->component_flags &
       static_cast<std::uint32_t>(scene_node_components::mesh_renderer)) == 0U ||
      removed_binding.mesh != restored_binding.mesh ||
      removed_binding.material != restored_binding.material || run_history(false).failed() ||
      run_history(true).failed())
    return 12;
  history.clear();
  engine::scene_mesh_binding before, after;
  scene_subtree_snapshot snapshot;
  scene_node_id restored;
  if (app->capture_mesh_binding(session.scene_handle(), session.find_node(uuid)->node, before)
          .failed() ||
      session.destroy_subtree(session.find_node(uuid)->node, snapshot).failed() ||
      snapshot.resources.size() != 1U || session.restore_subtree(snapshot, restored).failed() ||
      app->capture_mesh_binding(session.scene_handle(), restored, after).failed() ||
      before.mesh != after.mesh || before.material != after.material)
    return 11;
  session.close();
  return 0;
} catch (...) {
  return 10;
}
