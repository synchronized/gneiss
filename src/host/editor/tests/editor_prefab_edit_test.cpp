// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "commands/editor_prefab_edit.hpp"
#include "engine/core/result.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
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
        << R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
    std::ofstream(root / "material.json")
        << R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})";
    std::ofstream(root / "scene.json")
        << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[],"prefab_instances":[]})";
    std::ofstream prefab(root / "prefab.json");
    prefab
        << R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"30000000-0000-4000-8000-000000000001","objects":[)";
    for (unsigned i = 0; i < 150U; ++i) {
      char uuid[37]{};
      std::snprintf(uuid, sizeof(uuid), "30000000-0000-4000-8000-%012u", i + 2U);
      if (i != 0U) {
        prefab << ',';
      }
      prefab
          << "{\"uuid\":\"" << uuid << "\",\"name\":\"Source\",\"parent\":"
          << (i == 149U ? "null" : "\"30000000-0000-4000-8000-000000000151\"")
          << R"(,"transform":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://material.json"}}})";
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
  check(gneiss::engine::result{gneiss::runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  gneiss_world world{};
  check((world = app->world()) != GNEISS_NULL_WORLD);
  editor::scene_document session;
  editor::editor_command_history history;
  editor::editor_prefab_edit edit;
  check(session.open(*app, world, "asset://scene.json") == gneiss::engine::result::success);
  scene_node_id first, second;
  check(session.create_prefab_instance("First", "asset://prefab.json", {}, first) ==
        gneiss::engine::result::success);
  check(session.create_prefab_instance("Second", "asset://prefab.json", {}, second) ==
        gneiss::engine::result::success);
  const auto first_uuid = session.prefab_nodes()[0].instance_uuid;
  const auto second_uuid = session.prefab_nodes()[151].instance_uuid;
  const auto source_uuid = session.prefab_nodes()[1].source_node_uuid;
  const auto old_selected = session.find_prefab_source(first_uuid, source_uuid)->node;
  auto transform = session.find_prefab_source(first_uuid, source_uuid)->local_transform;
  transform.translation[0] = 17.0F;
  check(session.set_local_transform(old_selected, transform) == gneiss::engine::result::success);
  check(session.select(old_selected) == gneiss::engine::result::success);
  const auto step = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
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
  const auto drive_history = [&](gneiss::engine::result expected =
                                     gneiss::engine::result::success) {
    gneiss::engine::result outcome = gneiss::engine::result::not_ready;
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
        gneiss::engine::result::success);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (edit.active() && edit.phase() != editor::prefab_edit_phase::constructing) {
    step();
    check(std::chrono::steady_clock::now() < deadline);
  }
  // 取消候选准备不得改变旧实例、选择或历史。
  for (unsigned i = 0; i < 12U; ++i) {
    step();
  }
  check(edit.active() && edit.can_cancel());
  edit.cancel();
  drive();
  check(history.size() == 0U && session.selection() == old_selected);
  check(session.find_prefab_root(first_uuid)->node == first &&
        session.find_prefab_root(second_uuid)->node == second);
  check(edit.request_refresh(*app, scheduler, session, history, first) ==
        gneiss::engine::result::success);
  drive();
  check(edit.operation() == gneiss::engine::result::success && history.size() == 1U &&
        session.prefab_nodes().size() == 302U);
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
  check(history.undo() == gneiss::engine::result::not_ready);
  drive_history();
  check(session.find_prefab_source(first_uuid, source_uuid)->display_name == "Source");
  check(session.find_prefab_source(second_uuid, source_uuid)->display_name == "Source");
  check(session.selection() == session.find_prefab_source(first_uuid, source_uuid)->node);
  check(history.redo() == gneiss::engine::result::not_ready);
  drive_history();
  check(session.find_prefab_source(first_uuid, source_uuid)->display_name == "Updated");
  check(session.find_prefab_source(first_uuid, source_uuid)->local_transform.translation[0] ==
        17.0F);
  const auto preserved = session.selection();
  check(history.undo() == gneiss::engine::result::not_ready);
  (void)history.advance_pending();
  history.cancel_pending();
  drive_history(gneiss::engine::result::invalid_state);
  check(session.selection() == preserved && !history.can_redo());
  check(edit.request_refresh(*app, scheduler, session, history,
                             session.find_prefab_root(first_uuid)->node) ==
        gneiss::engine::result::success);
  drive();
  check(edit.operation().failed() && history.size() == 1U && session.selection() == preserved);
  history.clear();
  std::ofstream(files.root / "mesh.json")
      << R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})";
  // 第二个实例的覆盖引用将被删除的节点，第一候选已准备也不得部分提交。
  const auto removed_uuid = session.prefab_nodes()[2U].source_node_uuid;
  const auto second_node = session.find_prefab_source(second_uuid, removed_uuid)->node;
  const auto original = session.find_prefab_source(second_uuid, removed_uuid)->local_transform;
  auto changed = original;
  changed.translation[1] = 29.0F;
  check(session.set_local_transform(second_node, changed) == gneiss::engine::result::success);
  const auto first_root = session.find_prefab_root(first_uuid)->node;
  const auto second_root = session.find_prefab_root(second_uuid)->node;
  const auto removed_start = updated.find(R"(,{"uuid":")");
  const auto removed_end = updated.find(R"(,{"uuid":")", removed_start + 1U);
  check(removed_start != std::string::npos && removed_end != std::string::npos);
  updated.erase(removed_start, removed_end - removed_start);
  std::ofstream(files.root / "prefab.json") << updated;
  check(edit.request_refresh(*app, scheduler, session, history, first_root) ==
        gneiss::engine::result::success);
  drive();
  check(edit.operation() == gneiss::engine::result::not_found && history.size() == 0U);
  check(session.find_prefab_root(first_uuid)->node == first_root);
  check(session.find_prefab_root(second_uuid)->node == second_root);
  std::uint64_t count{};
  check(gneiss::engine::result{app->native_world()->entity_count(count)} ==
            gneiss::engine::result::success &&
        count == 302U);
  check(session.set_local_transform(second_node, original) == gneiss::engine::result::success);
  check(session.select(session.find_prefab_source(first_uuid, removed_uuid)->node) ==
        gneiss::engine::result::success);
  check(edit.request_refresh(*app, scheduler, session, history, first_root) ==
        gneiss::engine::result::success);
  drive();
  check(edit.operation() == gneiss::engine::result::success &&
        session.prefab_nodes().size() == 300U);
  check(session.selection() == session.find_prefab_root(first_uuid)->node);
  check(history.undo() == gneiss::engine::result::not_ready);
  drive_history();
  check(session.prefab_nodes().size() == 302U);
  history.clear();
  check(edit.request_refresh(*app, scheduler, session, history,
                             session.find_prefab_root(first_uuid)->node) ==
        gneiss::engine::result::success);
  session.close();
  drive();
  check(edit.operation() == gneiss::engine::result::invalid_state && history.size() == 0U);
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
  check(gneiss::engine::result{gneiss::runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  gneiss_world world{};
  check((world = app->world()) != GNEISS_NULL_WORLD);
  editor::scene_document session;
  editor::editor_command_history history;
  editor::editor_prefab_edit edit;
  check(session.open(*app, world, "asset://scene.json") == gneiss::engine::result::success);
  const auto request = [&](std::string_view uri = "asset://prefab.json") {
    return edit.request(*app, scheduler, session, history, "Placed", uri, {});
  };
  const auto step = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
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
  const auto drive_history = [&](gneiss::engine::result expected =
                                     gneiss::engine::result::success) {
    gneiss::engine::result outcome = gneiss::engine::result::not_ready;
    unsigned steps{};
    while (history.is_pending()) {
      outcome = history.advance_pending();
      check(++steps < 1000U);
    }
    check(outcome == expected);
  };
  // 旧场景加载/同步实例没有发布组，也必须支持分步退出与历史恢复。
  scene_node_id legacy;
  check(session.create_prefab_instance("Legacy", "asset://prefab.json", {}, legacy) ==
        gneiss::engine::result::success);
  check(edit.request_remove(*app, session, history, legacy) == gneiss::engine::result::success);
  drive();
  check(edit.operation() == gneiss::engine::result::success && session.prefab_nodes().empty());
  check(history.undo() == gneiss::engine::result::not_ready);
  drive_history();
  check(session.prefab_nodes().size() == 151U);
  check(history.redo() == gneiss::engine::result::not_ready);
  drive_history();
  check(session.prefab_nodes().empty());
  history.clear();
  check(request() == gneiss::engine::result::success);
  const auto preparation_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (edit.active() && edit.phase() != editor::prefab_edit_phase::constructing) {
    step();
    check(std::chrono::steady_clock::now() < preparation_deadline);
  }
  check(edit.active());
  step();
  std::uint64_t entity_count{};
  check(gneiss::engine::result{app->native_world()->entity_count(entity_count)} ==
            gneiss::engine::result::success &&
        entity_count == 0U);
  edit.cancel();
  drive();
  check(!edit.visible() && session.prefab_nodes().empty() && history.size() == 0U);
  check(gneiss::engine::result{app->native_world()->entity_count(entity_count)} ==
            gneiss::engine::result::success &&
        entity_count == 0U);
  edit.dismiss();
  check(request() == gneiss::engine::result::success && edit.active());
  check(request() == gneiss::engine::result::not_ready);
  check(session.prefab_nodes().empty() && history.size() == 0U);
  drive();
  if (edit.operation().failed()) {
    std::fprintf(stderr, "placement result=%d\n", (edit.operation()).code());
  }
  check(edit.operation() == gneiss::engine::result::success && !edit.visible());
  check(session.prefab_nodes().size() == 151U && history.size() == 1U);
  const auto uuid = session.prefab_nodes().front().instance_uuid;
  // 删除全部来源文件后，历史仍持有来源描述及实际 Mesh/Material 版本。
  std::ofstream(files.root / "prefab.json") << "broken";
  std::ofstream(files.root / "mesh.json") << "broken";
  std::ofstream(files.root / "material.json") << "broken";
  check(history.undo() == gneiss::engine::result::not_ready && history.is_pending());
  check(session.prefab_nodes().size() == 151U);
  history.cancel_pending();
  drive_history(gneiss::engine::result::invalid_state);
  check(session.prefab_nodes().size() == 151U && history.can_undo());
  check(history.undo() == gneiss::engine::result::not_ready);
  drive_history();
  check(session.prefab_nodes().empty() && history.can_redo());
  check(history.redo() == gneiss::engine::result::not_ready);
  (void)history.advance_pending();
  check(session.prefab_nodes().empty());
  history.cancel_pending();
  drive_history(gneiss::engine::result::invalid_state);
  check(session.prefab_nodes().empty() && history.can_redo());
  check(history.redo() == gneiss::engine::result::not_ready);
  drive_history();
  check(session.prefab_nodes().size() == 151U &&
        session.prefab_nodes().front().instance_uuid == uuid);
  // 删除保存实际版本和覆盖；恢复不依赖来源文件或旧节点 ID。
  editor::editor_command_history deletion_history;
  auto root_node = session.find_prefab_root(uuid)->node;
  const auto source_uuid = session.prefab_nodes()[1U].source_node_uuid;
  auto local = session.find_prefab_source(uuid, source_uuid)->local_transform;
  local.translation[0] = 17.0F;
  check(session.set_local_transform(session.find_prefab_source(uuid, source_uuid)->node, local) ==
        gneiss::engine::result::success);
  auto root_local = session.find_prefab_root(uuid)->local_transform;
  root_local.translation[1] = 7.0F;
  check(session.set_local_transform(root_node, root_local) == gneiss::engine::result::success);
  check(session.rename_prefab_instance(root_node, "Renamed") == gneiss::engine::result::success);
  check(edit.request_remove(*app, session, deletion_history, root_node) ==
        gneiss::engine::result::success);
  step();
  edit.cancel();
  drive();
  check(deletion_history.size() == 0U && session.prefab_nodes().size() == 151U);
  check(edit.request_remove(*app, session, deletion_history, root_node) ==
        gneiss::engine::result::success);
  drive();
  check(edit.operation() == gneiss::engine::result::success && session.prefab_nodes().empty() &&
        deletion_history.size() == 1U);
  const auto restore_deleted = [&] {
    check(deletion_history.undo() == gneiss::engine::result::not_ready);
    gneiss::engine::result restored = gneiss::engine::result::not_ready;
    unsigned turns{};
    while (deletion_history.is_pending()) {
      restored = deletion_history.advance_pending();
      check(++turns < 1000U);
    }
    check(restored == gneiss::engine::result::success);
    check(session.find_prefab_root(uuid)->display_name == "Renamed");
    check(session.find_prefab_root(uuid)->local_transform.translation[1] == 7.0F);
    check(session.find_prefab_source(uuid, source_uuid)->local_transform.translation[0] == 17.0F);
    check(session.find_prefab_source(uuid, source_uuid)->override_flags != 0U);
  };
  restore_deleted();
  check(session.find_prefab_root(uuid)->node != root_node);
  check(deletion_history.redo() == gneiss::engine::result::not_ready);
  while (deletion_history.is_pending()) {
    (void)deletion_history.advance_pending();
  }
  check(session.prefab_nodes().empty());
  restore_deleted();
  deletion_history.clear();
  check(request() == gneiss::engine::result::success);
  drive();
  check(edit.operation().failed() && history.size() == 1U && session.prefab_nodes().size() == 151U);
  edit.dismiss();
  check(request("asset://missing.json") == gneiss::engine::result::success);
  edit.cancel();
  drive();
  check(!edit.visible() && session.prefab_nodes().size() == 151U);
  edit.dismiss();
  auto missing_source = source;
  for (std::size_t position{};
       (position = missing_source.find("asset://material.json", position)) != std::string::npos;) {
    missing_source.replace(position, std::string_view("asset://material.json").size(),
                           "asset://missing-material.json");
    ++position;
  }
  std::ofstream(files.root / "prefab.json") << missing_source;
  check(request() == gneiss::engine::result::success);
  drive();
  check(edit.operation().failed() && session.prefab_nodes().size() == 151U && history.size() == 1U);
  edit.dismiss();
  check(history.undo() == gneiss::engine::result::not_ready);
  while (history.can_cancel_pending()) {
    (void)history.advance_pending();
  }
  // 发布后不可取消，关闭路径继续回收直到终态。
  history.cancel_pending();
  drive_history();
  check(session.prefab_nodes().empty());
  history.clear();
  std::ofstream(files.root / "prefab.json") << source;
  check(request() == gneiss::engine::result::success);
  session.close();
  drive();
  check(edit.operation() == gneiss::engine::result::invalid_state && history.size() == 0U);
  edit.dismiss();
}
}
int main() try {
  run_refresh(gneiss::engine::execution_mode::cooperative);
  run_refresh(gneiss::engine::execution_mode::thread_pool);
  run(gneiss::engine::execution_mode::cooperative);
  run(gneiss::engine::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
