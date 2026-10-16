// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "editor_asset_edit.hpp"
#include "engine/function/application/application_registry.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gneiss/engine/application.hpp>
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
void run(tasks::execution_mode mode) {
  fixture files;
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  sdk::application app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(sdk::application::create_native(desc, app) == result::success);
  check(application_internal::attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  gneiss_world world{};
  check(gneiss_application_get_world(app.get(), &world) == GNEISS_SUCCESS);
  editor::editor_session session;
  editor::editor_command_history history;
  editor::editor_asset_edit edit;
  check(session.open(*gneiss::application_internal::find_application(app.get()), world,
                     "asset://scene.json") == result::success);
  const auto uuid = session.nodes().front().uuid;
  const auto drive = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (edit.active()) {
      if (mode == tasks::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      edit.advance(*gneiss::application_internal::find_application(app.get()), session, history);
      check(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
  };
  const auto request = [&](std::string_view material) {
    return edit.request(*gneiss::application_internal::find_application(app.get()), session,
                        history, uuid, {.mesh_uri = "asset://mesh.json", .material_uri = material});
  };
  check(request("asset://b.json") == result::success && edit.active() && edit.visible());
  check(history.size() == 0U && session.nodes().front().material_uri == "asset://a.json");
  check(request("asset://b.json") == result::not_ready);
  drive();
  check(edit.operation() == result::success && !edit.visible() && history.size() == 1U);
  check(session.nodes().front().material_uri == "asset://b.json");
  std::ofstream(files.root / "a.json") << "broken";
  std::ofstream(files.root / "b.json") << "broken";
  check(history.undo() == result::success &&
        session.nodes().front().material_uri == "asset://a.json");
  check(history.redo() == result::success &&
        session.nodes().front().material_uri == "asset://b.json");
  check(history.undo() == result::success);
  check(request("asset://b.json") == result::success && !edit.active() && !edit.visible());
  const auto count = history.size();
  check(request("asset://missing.json") == result::success);
  drive();
  check(edit.operation().failed() && history.size() == count &&
        session.nodes().front().material_uri == "asset://b.json");
  edit.dismiss();
  check(request("asset://missing.json") == result::success);
  edit.cancel(*gneiss::application_internal::find_application(app.get()));
  drive();
  check(!edit.visible() && history.size() == count);
  // 创建同样先准备，成功才产生节点；命令持有版本，坏文件不影响重做。
  const editor::mesh_asset_reference new_assets{.mesh_uri = "asset://mesh.json",
                                                .material_uri = "asset://b.json"};
  check(edit.request_add(*gneiss::application_internal::find_application(app.get()), session,
                         history, "new node", new_assets) == result::success);
  check(edit.active() && session.nodes().size() == 1U && history.size() == count);
  drive();
  check(edit.operation() == result::success && session.nodes().size() == 2U);
  const auto created_uuid = session.nodes().back().uuid;
  std::ofstream(files.root / "mesh.json") << "broken";
  check(history.undo() == result::success && session.find_node(created_uuid) == nullptr);
  check(history.redo() == result::success && session.find_node(created_uuid) != nullptr);
  editor::editor_command_history disabled{0U};
  check(edit.request_add(*gneiss::application_internal::find_application(app.get()), session,
                         disabled, "rejected", new_assets) == result::invalid_argument);
  check(!edit.active() && session.nodes().size() == 2U);
  check(edit.request_add(*gneiss::application_internal::find_application(app.get()), session,
                         history, "ready node", new_assets) == result::success);
  check(!edit.active() && !edit.visible() && session.nodes().size() == 3U);
  const auto creation_count = history.size();
  check(edit.request_add(*gneiss::application_internal::find_application(app.get()), session,
                         history, "missing",
                         {.mesh_uri = "asset://mesh.json",
                          .material_uri = "asset://missing.json"}) == result::success);
  drive();
  check(edit.operation().failed() && session.nodes().size() == 3U &&
        history.size() == creation_count);
  edit.dismiss();
  // 文档已经关闭的迟到成功回执不能提交历史或采用新版本。
  std::ofstream(files.root / "c.json")
      << R"({"format":"gneiss.material","version":1,"color":[0,0,1,1]})";
  check(request("asset://c.json") == result::success);
  session.close();
  drive();
  check(edit.operation() == result::invalid_state && history.size() == creation_count);
  render_internal::render_asset_lease unused;
  check(application_internal::query_ready_asset(
            app.get(),
            {.uri = "asset://c.json", .type = render_internal::render_asset_type::material},
            unused) == GNEISS_ERROR_NOT_READY &&
        !unused);
  history.clear();
  session.close();
}
}
int main() try {
  run(gneiss::tasks::execution_mode::cooperative);
  run(gneiss::tasks::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
