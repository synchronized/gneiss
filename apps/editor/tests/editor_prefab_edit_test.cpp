// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "editor_prefab_edit.hpp"
#include "engine/function/application/application_asset_reload_internal.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gneiss/engine/application.hpp>
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
void run(tasks::execution_mode mode) {
  fixture files;
  std::ifstream input(files.root / "prefab.json");
  const std::string source{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  input.close();
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  application app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create_native(desc, app) == result::success);
  check(application_internal::attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  gneiss_world world{};
  check(gneiss_application_get_world(app.get(), &world) == GNEISS_SUCCESS);
  editor::editor_session session;
  editor::editor_command_history history;
  editor::editor_prefab_edit edit;
  check(session.open(app.get(), world, "asset://scene.json") == result::success);
  const auto request = [&](std::string_view uri = "asset://prefab.json") {
    return edit.request(app.get(), scheduler, session, history, "Placed", uri, {});
  };
  const auto step = [&] {
    if (mode == tasks::execution_mode::cooperative) {
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
  const auto drive_history = [&](result expected = result::success) {
    result outcome = result::not_ready;
    unsigned steps{};
    while (history.is_pending()) {
      outcome = history.advance_pending();
      check(++steps < 1000U);
    }
    check(outcome == expected);
  };
  check(request() == result::success);
  const auto preparation_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (edit.active() && edit.phase() != editor::prefab_edit_phase::constructing) {
    step();
    check(std::chrono::steady_clock::now() < preparation_deadline);
  }
  check(edit.active());
  step();
  std::uint64_t entity_count{};
  check(gneiss_world_entity_count(world, &entity_count) == GNEISS_SUCCESS && entity_count == 0U);
  edit.cancel();
  drive();
  check(!edit.visible() && session.prefab_nodes().empty() && history.size() == 0U);
  check(gneiss_world_entity_count(world, &entity_count) == GNEISS_SUCCESS && entity_count == 0U);
  edit.dismiss();
  check(request() == result::success && edit.active());
  check(request() == result::not_ready);
  check(session.prefab_nodes().empty() && history.size() == 0U);
  drive();
  if (edit.operation().failed()) {
    std::fprintf(stderr, "placement result=%d\n", to_native(edit.operation()));
  }
  check(edit.operation() == result::success && !edit.visible());
  check(session.prefab_nodes().size() == 151U && history.size() == 1U);
  const auto uuid = session.prefab_nodes().front().instance_uuid;
  // 删除全部来源文件后，历史仍持有来源描述及实际 Mesh/Material 版本。
  std::ofstream(files.root / "prefab.json") << "broken";
  std::ofstream(files.root / "mesh.json") << "broken";
  std::ofstream(files.root / "material.json") << "broken";
  check(history.undo() == result::not_ready && history.is_pending());
  check(session.prefab_nodes().size() == 151U);
  history.cancel_pending();
  drive_history(result::invalid_state);
  check(session.prefab_nodes().size() == 151U && history.can_undo());
  check(history.undo() == result::not_ready);
  drive_history();
  check(session.prefab_nodes().empty() && history.can_redo());
  check(history.redo() == result::not_ready);
  (void)history.advance_pending();
  check(session.prefab_nodes().empty());
  history.cancel_pending();
  drive_history(result::invalid_state);
  check(session.prefab_nodes().empty() && history.can_redo());
  check(history.redo() == result::not_ready);
  drive_history();
  check(session.prefab_nodes().size() == 151U &&
        session.prefab_nodes().front().instance_uuid == uuid);
  check(request() == result::success);
  drive();
  check(edit.operation().failed() && history.size() == 1U && session.prefab_nodes().size() == 151U);
  edit.dismiss();
  check(request("asset://missing.json") == result::success);
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
  check(request() == result::success);
  drive();
  check(edit.operation().failed() && session.prefab_nodes().size() == 151U && history.size() == 1U);
  edit.dismiss();
  check(history.undo() == result::not_ready);
  while (history.can_cancel_pending()) {
    (void)history.advance_pending();
  }
  // 发布后不可取消，关闭路径继续回收直到终态。
  history.cancel_pending();
  drive_history();
  check(session.prefab_nodes().empty());
  history.clear();
  std::ofstream(files.root / "prefab.json") << source;
  check(request() == result::success);
  session.close();
  drive();
  check(edit.operation() == result::invalid_state && history.size() == 0U);
  edit.dismiss();
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
