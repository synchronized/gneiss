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
#include <unordered_set>

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
class delayed_backend final : public gneiss::engine::files::backend {
public:
  gneiss::engine::files::callback<gneiss::engine::files::bytes> pending;
  gneiss::engine::files::callback<gneiss::engine::files::directory_page> pending_list;
  bool delay_list{};
  engine::result
  list(std::string_view, gneiss::engine::files::list_options,
       gneiss::engine::files::callback<gneiss::engine::files::directory_page> callback) override {
    if (delay_list)
      pending_list = std::move(callback);
    else
      callback({engine::result::success,
                {{{"main.scene.json", {gneiss::engine::files::kind::regular, {}}}}, {}}});
    return engine::result::success;
  }
  engine::result
  read(std::string_view, gneiss::engine::files::read_options,
       gneiss::engine::files::callback<gneiss::engine::files::bytes> callback) override {
    require(!pending, "指纹读取不得并发积压");
    pending = std::move(callback);
    return engine::result::success;
  }
};
void pending_shutdown(bool destroy, bool list = false) {
  fixture disk;
  write(disk.root / "main.scene.json", "initial");
  engine::task_scheduler scheduler({.workers = 0, .mode = engine::execution_mode::cooperative});
  auto files = std::make_shared<gneiss::engine::files::service>();
  auto backend = std::make_shared<delayed_backend>();
  require(files->mount({"asset://", {backend}, {}}).ok(), "测试挂载失败");
  backend->delay_list = list;
  auto service = std::make_unique<author_asset_service>(scheduler);
  require(service->initialize(files).ok(), "延迟服务初始化失败");
  until([&] {
    (void)scheduler.run_ready();
    (void)files->dispatch_completions(64);
    std::vector<std::filesystem::path> paths;
    (void)service->poll_rescan(paths);
    return list ? static_cast<bool>(backend->pending_list) : static_cast<bool>(backend->pending);
  });
  require(scheduler.stats().running == 0 && scheduler.stats().waiting == 0,
          "等待文件期间占用 CPU 任务");
  service->request_stop();
  require(!service->stopped(), "文件未收尾却报告停止");
  if (destroy)
    service.reset();
  if (list) {
    auto complete_list = std::move(backend->pending_list);
    complete_list({engine::result::success, {}});
  } else {
    auto complete = std::move(backend->pending);
    complete({engine::result::success, {std::byte{7}}});
  }
  require(files->dispatch_completions(64).ok(), "迟到回调分发失败");
  if (service)
    require(service->stopped(), "文件收尾后仍未停止");
  gneiss::engine::files::request_id request;
  require(files->read(
                   "asset://other", {1}, [](auto) {}, request)
              .ok(),
          "停止作者服务关闭了共享文件服务");
  auto complete = std::move(backend->pending);
  complete({engine::result::success, {}});
  (void)files->dispatch_completions(64);
}
void scan_failure_retry() {
  engine::task_scheduler scheduler({.workers = 0, .mode = engine::execution_mode::cooperative});
  auto files = std::make_shared<gneiss::engine::files::service>();
  auto backend = std::make_shared<delayed_backend>();
  backend->delay_list = true;
  require(files->mount({"asset://", {backend}, {}}).ok(), "扫描测试挂载失败");
  author_asset_service service(scheduler);
  require(service.initialize(files).ok() && backend->pending_list, "首个目录请求未提交");
  auto complete = std::move(backend->pending_list);
  complete({engine::result::io, {}});
  (void)files->dispatch_completions(64);
  std::vector<std::filesystem::path> paths;
  require(service.poll_rescan(paths) == engine::result::io && !service.is_rescanning(),
          "目录失败没有结束本次扫描");
  service.request_rescan();
  require(service.poll_rescan(paths).ok() && backend->pending_list, "失败后不能重新扫描");
  complete = std::move(backend->pending_list);
  complete({engine::result::success, {}});
  (void)files->dispatch_completions(64);
  (void)service.poll_rescan(paths);
  (void)service.poll_rescan(paths);
  require(!service.is_rescanning() && paths.empty() && service.rescan_result().ok(),
          "空目录恢复失败");
}
void run(std::size_t workers,
         gneiss::engine::execution_mode mode = gneiss::engine::execution_mode::thread_pool) {
  fixture files;
  const auto path = files.root / "main.scene.json";
  constexpr auto uri = "asset://main.scene.json";
  write(path, "initial");
  std::filesystem::create_directories(files.root / "nested");
  for (unsigned i = 0; i < 20; ++i)
    write(files.root / "nested" / (std::to_string(i) + ".prefab.json"), "initial");
  gneiss::engine::task_scheduler scheduler({.workers = workers, .mode = mode});
  gneiss::runtime::application_configuration config;
  const auto root_text = files.root.generic_string();
  config.asset_root = root_text;
  std::shared_ptr<gneiss::runtime::application> app;
  require(gneiss::runtime::application::create(config, app).ok(), "测试文件宿主创建失败");
  auto io = app->async_files();
  author_asset_service service(scheduler);
  require(service.initialize(io) == gneiss::engine::result::success, "启动失败");
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
  for (unsigned i = 0; i < 20; ++i)
    write(files.root / "nested" / (std::to_string(i) + ".prefab.json"), "changed");
  service.request_rescan();
  std::unordered_set<std::string> nested;
  until([&] {
    (void)io->dispatch_completions(64);
    if (mode == engine::execution_mode::cooperative)
      (void)scheduler.run_ready();
    std::vector<std::filesystem::path> paths;
    require(service.poll_rescan(paths).ok(), "分页目录扫描失败");
    for (const auto& item : paths) {
      auto change = service.observe(item, false);
      require(change.operation.ok(), "嵌套文件读取失败");
      nested.insert(change.uri);
      service.mark_applied(change.uri);
    }
    return !service.is_rescanning();
  });
  require(nested.size() == 20, "多页或递归目录漏文件");
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
  scan_failure_retry();
  pending_shutdown(false);
  pending_shutdown(true);
  pending_shutdown(false, true);
  pending_shutdown(true, true);
  run(1U);
  run(3U);
  run(0U, gneiss::engine::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
