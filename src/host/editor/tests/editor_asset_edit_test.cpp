// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "commands/editor_asset_edit.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
void check(bool value, std::source_location location = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("资产编辑验证失败：" + std::to_string(location.line()));
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-edit-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() {
    std::filesystem::create_directories(root);
    std::ofstream(root / "mesh.json")
        << R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
    std::ofstream(root / "a.json")
        << R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})";
    std::ofstream(root / "b.json")
        << R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})";
    std::ofstream(root / "scene.json")
        << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://a.json"}}}],"prefab_instances":[]})";
  }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
void run(gneiss::engine::execution_mode mode) {
  fixture files;
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration desc;
  const auto root = files.root.string();
  desc.asset_root = root;
  check(gneiss::engine::result{gneiss::runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  gneiss::engine::rid world{};
  check((world = app->world()) != gneiss::engine::null_rid);
  editor::scene_document session;
  editor::editor_command_history history;
  editor::editor_asset_edit edit;
  check(session.open(*app, world, "asset://scene.json") == gneiss::engine::result::success);
  const auto uuid = session.nodes().front().uuid;
  const auto drive = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (edit.active()) {
      if (mode == gneiss::engine::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      edit.advance(*app, session, history);
      check(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
  };
  const auto request = [&](std::string_view material) {
    return edit.request(*app, session, history, uuid,
                        {.mesh_uri = "asset://mesh.json", .material_uri = material});
  };
  const auto binding = [&] {
    gneiss::engine::scene_instances::mesh_binding value;
    check(app->capture_mesh_binding(session.scene_handle(), session.find_node(uuid)->node, value) ==
          gneiss::engine::result::success);
    return value;
  };
  const auto initial_binding = binding();
  check(request("asset://b.json") == gneiss::engine::result::success && edit.active() &&
        edit.visible());
  check(history.size() == 0U && session.nodes().front().material_uri == "asset://a.json");
  check(request("asset://b.json") == gneiss::engine::result::not_ready);
  drive();
  check(edit.operation() == gneiss::engine::result::success && !edit.visible() &&
        history.size() == 1U);
  check(session.nodes().front().material_uri == "asset://b.json");
  const auto changed_binding = binding();
  check(initial_binding.material != changed_binding.material);
  std::ofstream(files.root / "a.json") << "broken";
  std::ofstream(files.root / "b.json") << "broken";
  check(history.undo() == gneiss::engine::result::success &&
        session.nodes().front().material_uri == "asset://a.json");
  check(binding().material == initial_binding.material);
  check(history.redo() == gneiss::engine::result::success &&
        session.nodes().front().material_uri == "asset://b.json");
  check(binding().material == changed_binding.material);
  // 新材质准备期间撤销前一次编辑；迟到结果不得覆盖撤销后的确切版本。
  std::ofstream(files.root / "c.json")
      << R"({"format":"gneiss.material","version":1,"color":[0,0,1,1]})";
  check(request("asset://c.json") == gneiss::engine::result::success && edit.active());
  check(history.undo() == gneiss::engine::result::success);
  drive();
  check(edit.operation() == gneiss::engine::result::invalid_state && history.size() == 1U &&
        binding().material == initial_binding.material);
  check(history.redo() == gneiss::engine::result::success &&
        binding().material == changed_binding.material);
  edit.dismiss();
  check(history.undo() == gneiss::engine::result::success);
  check(request("asset://b.json") == gneiss::engine::result::success && !edit.active() &&
        !edit.visible());
  const auto count = history.size();
  check(request("asset://missing.json") == gneiss::engine::result::success);
  drive();
  check(edit.operation().failed() && history.size() == count &&
        session.nodes().front().material_uri == "asset://b.json");
  edit.dismiss();
  check(request("asset://missing.json") == gneiss::engine::result::success);
  edit.cancel(*app);
  drive();
  check(!edit.visible() && history.size() == count);
  // 创建同样先准备，成功才产生节点；命令持有资源引用，坏文件不影响当前成功内容的复用。
  const editor::mesh_asset_reference new_assets{.mesh_uri = "asset://mesh.json",
                                                .material_uri = "asset://b.json"};
  check(edit.request_add(*app, session, history, "new node", new_assets) ==
        gneiss::engine::result::success);
  check(!edit.active() && session.nodes().size() == 2U && history.size() == count + 1U);
  drive();
  check(edit.operation() == gneiss::engine::result::success && session.nodes().size() == 2U);
  const auto created_uuid = session.nodes().back().uuid;
  std::ofstream(files.root / "mesh.json") << "broken";
  check(history.undo() == gneiss::engine::result::success &&
        session.find_node(created_uuid) == nullptr);
  check(history.redo() == gneiss::engine::result::success &&
        session.find_node(created_uuid) != nullptr);
  editor::editor_command_history disabled{0U};
  check(edit.request_add(*app, session, disabled, "rejected", new_assets) ==
        gneiss::engine::result::invalid_argument);
  check(!edit.active() && session.nodes().size() == 2U);
  check(edit.request_add(*app, session, history, "ready node", new_assets) ==
        gneiss::engine::result::success);
  check(!edit.active() && !edit.visible() && session.nodes().size() == 3U);
  const auto creation_count = history.size();
  check(
      edit.request_add(*app, session, history, "missing",
                       {.mesh_uri = "asset://mesh.json", .material_uri = "asset://missing.json"}) ==
      gneiss::engine::result::success);
  drive();
  check(edit.operation().failed() && session.nodes().size() == 3U &&
        history.size() == creation_count);
  edit.dismiss();
  // 文档已经关闭的迟到成功回执不能提交历史或采用新版本。
  std::ofstream(files.root / "c.json")
      << R"({"format":"gneiss.material","version":1,"color":[0,0,1,1]})";
  check(request("asset://c.json") == gneiss::engine::result::success);
  session.close();
  drive();
  check(edit.operation() == gneiss::engine::result::invalid_state &&
        history.size() == creation_count);
  gneiss::engine::render_asset_ref unused;
  check(app->query_ready_asset(
            {.uri = "asset://c.json", .type = gneiss::engine::render_asset_type::material},
            unused) == gneiss::engine::result::not_ready &&
        !unused);
  history.clear();
  // 旧请求还在准备时切换到另一个有效场景；不能污染新场景或采用旧候选。
  check(session.create_empty(*app, world) == gneiss::engine::result::success);
  std::ofstream(files.root / "d.json")
      << R"({"format":"gneiss.material","version":1,"color":[1,1,0,1]})";
  check(edit.request_add(*app, session, history, "stale",
                         {.mesh_uri = "asset://mesh.json", .material_uri = "asset://d.json"}) ==
        gneiss::engine::result::success);
  check(edit.active());
  const auto previous_scene = session.scene_handle();
  check(session.create_empty(*app, world) == gneiss::engine::result::success);
  const auto new_scene = session.scene_handle();
  check(new_scene != previous_scene);
  drive();
  check(edit.operation() == gneiss::engine::result::invalid_state &&
        session.scene_handle() == new_scene && session.nodes().empty() && history.size() == 0U);
  check(app->query_ready_asset(
            {.uri = "asset://d.json", .type = gneiss::engine::render_asset_type::material},
            unused) == gneiss::engine::result::not_ready);
  session.close();
}
}
int main() try {
  run(gneiss::engine::execution_mode::cooperative);
  run(gneiss::engine::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
