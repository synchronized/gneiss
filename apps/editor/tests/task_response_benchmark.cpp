// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_background_worker.hpp"
#include "author_asset_service.hpp"
#include "editor_session.hpp"

#include <gneiss/application.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
using clock_type = std::chrono::steady_clock;
template <class Function> double measure(Function function) {
  const auto start = clock_type::now();
  function();
  return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
void run(const std::filesystem::path& root, const std::filesystem::path& source) {
  using namespace gneiss;
  using namespace gneiss::editor;
  require(!std::filesystem::exists(root), "输出目录必须尚不存在");
  std::filesystem::create_directories(root / "assets");
  editor_import_report imported;
  const auto import_ms =
      measure([&] { imported = import_external_asset(root, root / "assets", source); });
  require(imported.result == editor_import_result::success, "实际模型导入失败");
  std::ifstream prefab_input(GNEISS_TEST_PREFAB, std::ios::binary);
  const std::string prefab{std::istreambuf_iterator<char>(prefab_input),
                           std::istreambuf_iterator<char>()};
  require(!prefab.empty(), "Prefab 测试数据缺失");
  std::ofstream scene(root / "assets/main.scene.json");
  scene
      << R"({"format":"gneiss.scene","version":4,"scene_uuid":"40000000-0000-4000-8000-000000000001","objects":[],"prefab_instances":[)";
  constexpr std::size_t count = 64U;
  for (std::size_t index = 0; index < count; ++index) {
    std::ofstream(root / "assets" / ("item-" + std::to_string(index) + ".prefab.json")) << prefab;
    char id[64]{};
    std::snprintf(id, sizeof(id), "40000000-0000-4000-8000-%012zu", index + 2U);
    if (index != 0U) {
      scene << ',';
    }
    scene << "{\"instance_uuid\":\"" << id
          << "\",\"name\":\"item\",\"parent\":null,"
             "\"prefab\":\"asset://item-"
          << index
          << ".prefab.json\","
             "\"transform\":{\"translation\":[0,0,0],\"rotation\":[0,0,0,1],"
             "\"scale\":[1,1,1]},\"overrides\":[]}";
  }
  scene << "]}";
  scene.close();
  tasks::task_scheduler scheduler;
  author_asset_service authors(scheduler);
  asset_background_worker worker({}, &scheduler);
  const auto assets = root / "assets";
  require(authors.initialize(assets) == result::success, "作者服务启动失败");
  const auto baseline_start = clock_type::now();
  while (authors.is_rescanning()) {
    std::vector<std::filesystem::path> paths;
    require(authors.poll_rescan(paths) == result::success, "作者基线扫描失败");
    require(clock_type::now() - baseline_start < std::chrono::seconds(60), "基线超时");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto baseline_ms =
      std::chrono::duration<double, std::milli>(clock_type::now() - baseline_start).count();
  const auto asset_text = assets.generic_string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_text.c_str();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_text.size());
  application app;
  gneiss_world world{};
  editor_session session;
  require(application::create(desc, app) == result::success &&
              app.get_world(world) == result::success,
          "基准应用创建失败");
  const auto open_ms = measure([&] {
    require(session.open(app.get(), world, "asset://main.scene.json") == result::success,
            "多 Prefab 场景加载失败");
  });
  worker.start(root, assets);
  require(worker.import_asset(imported.source_path), "后台导入提交失败");
  for (std::size_t index = 0; index < count; ++index) {
    std::ofstream(root / "assets" / ("item-" + std::to_string(index) + ".prefab.json"),
                  std::ios::app)
        << ' ';
  }
  authors.request_rescan();
  std::size_t applied{};
  std::size_t peak{};
  bool imported_again{};
  double max_poll{};
  double max_apply{};
  double max_interval{};
  const auto start = clock_type::now();
  auto previous = start;
  while (!imported_again || applied < count || authors.is_rescanning()) {
    const auto now = clock_type::now();
    max_interval =
        std::max(max_interval, std::chrono::duration<double, std::milli>(now - previous).count());
    previous = now;
    require(now - start < std::chrono::seconds(120), "真实资产基准超时");
    std::vector<std::filesystem::path> paths;
    max_poll = std::max(max_poll, measure([&] {
                          require(authors.poll_rescan(paths) == result::success, "作者扫描失败");
                          std::vector<asset_reimport_event> events;
                          (void)worker.poll_events(events);
                          require(worker.status().error.empty(), "后台服务错误");
                          for (const auto& event : events) {
                            imported_again =
                                imported_again || event.state == asset_reimport_state::succeeded;
                            require(event.state != asset_reimport_state::failed, "后台导入失败");
                          }
                          peak = std::max(peak, scheduler.stats().retained);
                        }));
    for (const auto& path : paths) {
      const auto change = authors.observe(path, false);
      if (change.state == author_asset_change_state::changed &&
          change.uri.ends_with(".prefab.json")) {
        require(change.operation == result::success, "Prefab 读取失败");
        max_apply = std::max(max_apply, measure([&] {
                               require(session.open(app.get(), world, "asset://main.scene.json") ==
                                           result::success,
                                       "主线程应用失败");
                             }));
        authors.mark_applied(change.uri);
        ++applied;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  worker.request_stop();
  authors.request_stop();
  std::printf("source_bytes=%ju prefabs=%zu initial_import_ms=%.3f baseline_ms=%.3f "
              "open_ms=%.3f wall_ms=%.3f max_poll_ms=%.3f max_apply_ms=%.3f "
              "max_loop_interval_ms=%.3f peak_tasks=%zu\n",
              std::filesystem::file_size(source), count, import_ms, baseline_ms, open_ms,
              std::chrono::duration<double, std::milli>(clock_type::now() - start).count(),
              max_poll, max_apply, max_interval, peak);
}
} // namespace
int main(int argc, char** argv) try {
  if (argc != 3) {
    return 1;
  }
  run(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]));
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 2;
}
