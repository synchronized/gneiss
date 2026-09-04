// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "asset_import_controller.h"

#include <gneiss/core/result.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace gneiss::editor {

enum class asset_reimport_state {
  waiting,
  importing,
  unchanged,
  succeeded,
  failed,
  untracked,
  removed,
  restart_required,
};

struct asset_reimport_event final {
  asset_reimport_state state{asset_reimport_state::waiting};
  std::filesystem::path relative_path;
  editor_import_report import;
};

struct asset_reimport_queue_options final {
  std::chrono::milliseconds debounce{250};
  std::chrono::milliseconds stable_read_delay{100};
  std::size_t capacity{256U};
};

/**
 * 将文件监听候选变化整理为稳定、去重且按源文件串行的重新导入事务。
 *
 * 本类型由 Editor 主线程驱动。tick() 最多同步执行 max_imports 次导入；调用方应控制每帧预算。
 */
class asset_reimport_queue final {
public:
  using clock = std::chrono::steady_clock;
  using import_function = std::function<editor_import_report(
      const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&)>;

  explicit asset_reimport_queue(asset_reimport_queue_options options = {},
                                import_function importer = reimport_source_asset);
  ~asset_reimport_queue();

  asset_reimport_queue(const asset_reimport_queue&) = delete;
  asset_reimport_queue& operator=(const asset_reimport_queue&) = delete;

  /** 加入相对于工程 sources 的候选路径；重复通知会重置防抖期限。 */
  [[nodiscard]] result notify(const std::filesystem::path& relative_path,
                              clock::time_point now = clock::now()) noexcept;

  /** 处理到期候选；返回本次实际执行的导入次数。 */
  [[nodiscard]] std::size_t tick(const std::filesystem::path& project_root,
                                 const std::filesystem::path& asset_root,
                                 clock::time_point now = clock::now(),
                                 std::size_t max_imports = 1U) noexcept;

  [[nodiscard]] std::size_t poll_events(std::vector<asset_reimport_event>& output,
                                        std::size_t max_count = 64U) noexcept;
  [[nodiscard]] std::size_t pending_count() const noexcept;
  [[nodiscard]] std::size_t dropped_candidate_count() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::editor
