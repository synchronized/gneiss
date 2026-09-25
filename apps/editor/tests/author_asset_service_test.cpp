// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_asset_service.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace gneiss;
using namespace gneiss::editor;
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
void write(const std::filesystem::path& path, std::string_view content) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << content;
  require(static_cast<bool>(stream), "写入测试资产失败");
}
template <class Predicate> void until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < deadline, "作者资产任务超时");
    std::this_thread::yield();
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-author-service-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() { std::filesystem::create_directories(root); }
  ~fixture() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};
void run(std::size_t workers, tasks::execution_mode mode = tasks::execution_mode::thread_pool) {
  fixture files;
  const auto path = files.root / "main.scene.json";
  constexpr auto uri = "asset://main.scene.json";
  write(path, "initial");
  tasks::task_scheduler scheduler({.workers = workers, .mode = mode});
  author_asset_service service(scheduler);
  require(service.initialize(files.root) == result::success, "启动失败");
  auto drain = [&] {
    until([&] {
      if (mode == tasks::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      std::vector<std::filesystem::path> paths;
      require(service.poll_rescan(paths, 1U) == result::success, "补扫失败");
      for (const auto& item : paths) {
        const auto change = service.observe(item, false);
        if (change.operation == result::success) {
          service.mark_applied(change.uri);
        }
      }
      return !service.is_rescanning() && scheduler.stats().running == 0U &&
             scheduler.stats().waiting == 0U && scheduler.stats().retained == 0U;
    });
  };
  drain();
  write(path, "external");
  (void)service.observe("main.scene.json", false);
  author_asset_change found;
  until([&] {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    std::vector<std::filesystem::path> paths;
    (void)service.poll_rescan(paths);
    for (const auto& item : paths) {
      found = service.observe(item, true);
    }
    return found.state == author_asset_change_state::conflict;
  });
  require(found.uri == uri, "消费时脏状态未保护场景");
  // 先产生旧回执，再保存新内容；旧回执必须失效。
  service.request_rescan();
  std::vector<std::filesystem::path> old_paths;
  until([&] {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    (void)service.poll_rescan(old_paths);
    return !old_paths.empty();
  });
  write(path, "own-save");
  require(service.acknowledge(uri, "own-save") == result::success, "保存确认失败");
  // 外部写入发生在保存之后、后台确认之前，不能被确认为自身保存。
  write(path, "external-after-save");
  found = {};
  until([&] {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    std::vector<std::filesystem::path> paths;
    (void)service.poll_rescan(paths);
    for (const auto& item : paths) {
      found = service.observe(item, false);
    }
    return found.state == author_asset_change_state::changed;
  });
  require(found.uri == uri && found.operation == result::success, "自身保存吞掉外部修改");
  service.mark_applied(uri);
  drain();
  std::filesystem::remove(path);
  service.request_rescan();
  found = {};
  until([&] {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    std::vector<std::filesystem::path> paths;
    (void)service.poll_rescan(paths);
    for (const auto& item : paths) {
      found = service.observe(item, false);
    }
    return found.state == author_asset_change_state::changed;
  });
  require(found.operation == result::not_found, "删除未被发现");
  write(path, "rebuilt");
  write(files.root / "child.prefab.json", "prefab");
  service.request_rescan();
  drain();
  require(service.observe("../outside.scene.json", false).state == author_asset_change_state::idle,
          "接受越界路径");
  service.request_stop();
  until([&] {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    return service.stopped();
  });
  const auto other = scheduler.make_scope();
  tasks::task_handle handle;
  require(scheduler.submit(
              {.scope = other}, [](const auto&) { return tasks::task_outcome{}; }, handle) ==
              tasks::submit_result::success,
          "停止作者服务关闭了共享调度器");
  require(scheduler.close_scope(other), "独立作用域无法关闭");
}
} // namespace
int main() try {
  run(1U);
  run(3U);
  run(0U, tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
