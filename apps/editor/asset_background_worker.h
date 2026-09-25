// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "asset_browser_model.h"
#include "asset_reimport_queue.h"

#include <memory>
#include <string>

namespace gneiss::tasks {
class task_scheduler;
}

namespace gneiss::editor {

struct asset_worker_status {
  bool active{};
  bool stopping{};
  bool stopped{};
  bool paused{};
  bool rescanning{};
  std::size_t pending{};
  std::size_t dropped{};
  result rescan_result{result::success};
  std::string stage;
  std::filesystem::path source;
  std::string error;
};

/// 单工程串行资产服务；宿主轮询驱动有限任务，任务不接触 UI、Scene 或 Runtime。
/// 可借用寿命更长的宿主调度器；未提供时持有私有调度器。析构取消并等待自身作用域。
class asset_background_worker final {
public:
  using import_function = std::function<editor_import_report(
      const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&,
      bool, const tooling::asset_import::import_control&)>;
  explicit asset_background_worker(import_function importer = {},
                                   tasks::task_scheduler* scheduler = nullptr);
  ~asset_background_worker();
  asset_background_worker(const asset_background_worker&) = delete;
  asset_background_worker& operator=(const asset_background_worker&) = delete;

  void start(std::filesystem::path project, std::filesystem::path assets);
  [[nodiscard]] result notify(const std::filesystem::path& relative);
  [[nodiscard]] bool import_asset(const std::filesystem::path& source, bool external = false);
  void request_rescan();
  void request_refresh();
  void cancel();
  void request_stop();
  void set_paused(bool paused);
  [[nodiscard]] asset_worker_status status() const;
  [[nodiscard]] bool is_rescanning() const;
  [[nodiscard]] result rescan_result() const;
  [[nodiscard]] std::size_t dropped_candidate_count() const;
  std::size_t poll_events(std::vector<asset_reimport_event>& events);
  bool poll_browser(asset_browser_model& browser, asset_browser_result& result);

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::editor
