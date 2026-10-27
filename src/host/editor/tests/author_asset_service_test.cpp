// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_asset_service.hpp"
#include "engine/core/result.hpp"
#include "engine/platform/files/file_backend.hpp"
#include "runtime/application.hpp"

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
class delayed_backend final : public engine::file_backend {
public:
  engine::file_callback<engine::file_bytes> pending;
  engine::result read(std::string_view, engine::read_options,
                      engine::file_callback<engine::file_bytes> callback) override {
    require(!pending, "指纹读取不得并发积压");
    pending = std::move(callback);
    return engine::result::success;
  }
};
void pending_shutdown(bool destroy) {
  fixture disk;
  write(disk.root / "main.scene.json", "initial");
  engine::task_scheduler scheduler({.workers = 0, .mode = engine::execution_mode::cooperative});
  auto files = std::make_shared<engine::file_service>();
  auto backend = std::make_shared<delayed_backend>();
  require(files->mount({"asset://", {backend}, {}}).ok(), "测试挂载失败");
  auto service = std::make_unique<author_asset_service>(scheduler);
  require(service->initialize(disk.root, files).ok(), "延迟服务初始化失败");
  until([&] {
    (void)scheduler.run_ready();
    (void)files->dispatch_completions(64);
    std::vector<std::filesystem::path> paths;
    (void)service->poll_rescan(paths);
    return static_cast<bool>(backend->pending);
  });
  require(scheduler.stats().running == 0 && scheduler.stats().waiting == 0,
          "等待文件期间占用 CPU 任务");
  service->request_stop();
  require(!service->stopped(), "文件未收尾却报告停止");
  if (destroy)
    service.reset();
  auto complete = std::move(backend->pending);
  complete({engine::result::success, {std::byte{7}}});
  require(files->dispatch_completions(64).ok(), "迟到回调分发失败");
  if (service)
    require(service->stopped(), "文件收尾后仍未停止");
  engine::file_request_id request;
  require(files->read(
                   "asset://other", {1}, [](auto) {}, request)
              .ok(),
          "停止作者服务关闭了共享文件服务");
  complete = std::move(backend->pending);
  complete({engine::result::success, {}});
  (void)files->dispatch_completions(64);
}
void run(std::size_t workers,
         gneiss::engine::execution_mode mode = gneiss::engine::execution_mode::thread_pool) {
  fixture files;
  const auto path = files.root / "main.scene.json";
  constexpr auto uri = "asset://main.scene.json";
  write(path, "initial");
  gneiss::engine::task_scheduler scheduler({.workers = workers, .mode = mode});
  gneiss::runtime::application_configuration config;
  const auto root_text = files.root.generic_string();
  config.asset_root = root_text;
  std::shared_ptr<gneiss::runtime::application> app;
  require(gneiss::runtime::application::create(config, app).ok(), "测试文件宿主创建失败");
  auto io = app->async_files();
  author_asset_service service(scheduler);
  require(service.initialize(files.root, io) == gneiss::engine::result::success, "启动失败");
  auto drain = [&] {
    until([&] {
      require(io->dispatch_completions(64).ok(), "文件完成分发失败");
      if (mode == gneiss::engine::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      std::vector<std::filesystem::path> paths;
      require(service.poll_rescan(paths, 1U) == gneiss::engine::result::success, "补扫失败");
      for (const auto& item : paths) {
        const auto change = service.observe(item, false);
        if (change.operation == gneiss::engine::result::success) {
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
    require(io->dispatch_completions(64).ok(), "文件完成分发失败");
    if (mode == gneiss::engine::execution_mode::cooperative) {
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
    require(io->dispatch_completions(64).ok(), "文件完成分发失败");
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    (void)service.poll_rescan(old_paths);
    return !old_paths.empty();
  });
  write(path, "own-save");
  require(service.acknowledge(uri, "own-save") == gneiss::engine::result::success, "保存确认失败");
  // 外部写入发生在保存之后、后台确认之前，不能被确认为自身保存。
  write(path, "external-after-save");
  found = {};
  until([&] {
    require(io->dispatch_completions(64).ok(), "文件完成分发失败");
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    std::vector<std::filesystem::path> paths;
    (void)service.poll_rescan(paths);
    for (const auto& item : paths) {
      found = service.observe(item, false);
    }
    return found.state == author_asset_change_state::changed;
  });
  require(found.uri == uri && found.operation == gneiss::engine::result::success,
          "自身保存吞掉外部修改");
  service.mark_applied(uri);
  drain();
  std::filesystem::remove(path);
  service.request_rescan();
  found = {};
  until([&] {
    require(io->dispatch_completions(64).ok(), "文件完成分发失败");
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    std::vector<std::filesystem::path> paths;
    (void)service.poll_rescan(paths);
    for (const auto& item : paths) {
      found = service.observe(item, false);
    }
    return found.state == author_asset_change_state::changed;
  });
  require(found.operation == gneiss::engine::result::not_found, "删除未被发现");
  write(path, "rebuilt");
  write(files.root / "child.prefab.json", "prefab");
  service.request_rescan();
  drain();
  require(service.observe("../outside.scene.json", false).state == author_asset_change_state::idle,
          "接受越界路径");
  service.request_stop();
  until([&] {
    require(io->dispatch_completions(64).ok(), "文件完成分发失败");
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    return service.stopped();
  });
  const auto other = scheduler.make_scope();
  gneiss::engine::task_handle handle;
  require(scheduler.submit(
              {.scope = other}, [](const auto&) { return gneiss::engine::task_outcome{}; },
              handle) == gneiss::engine::submit_result::success,
          "停止作者服务关闭了共享调度器");
  require(scheduler.close_scope(other), "独立作用域无法关闭");
}
} // namespace
int main() try {
  pending_shutdown(false);
  pending_shutdown(true);
  run(1U);
  run(3U);
  run(0U, gneiss::engine::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
