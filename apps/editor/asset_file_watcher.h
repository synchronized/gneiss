// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace gneiss::editor {

enum class asset_file_event_kind : std::uint8_t {
  changed,
  renamed,
  error,
};

struct asset_file_event final {
  asset_file_event_kind kind = asset_file_event_kind::changed;
  std::filesystem::path relative_path;
  result operation = result::success;
};

/**
 * 监视单个工程 sources 目录并输出候选变化。
 *
 * 文件事件来自后台 I/O 线程，poll_events() 可由 Editor 主线程调用。平台可能为一次写入产生多个
 * 事件，调用方仍须通过防抖与内容校验确认真实变化。除 poll_events() 外生命周期操作需外部同步。
 */
class asset_file_watcher final {
public:
  explicit asset_file_watcher(std::size_t event_capacity = 256U);
  ~asset_file_watcher();

  asset_file_watcher(const asset_file_watcher&) = delete;
  asset_file_watcher& operator=(const asset_file_watcher&) = delete;

  [[nodiscard]] result start(const std::filesystem::path& source_root) noexcept;
  [[nodiscard]] result stop() noexcept;
  [[nodiscard]] std::size_t poll_events(std::vector<asset_file_event>& output,
                                        std::size_t max_count = 64U) noexcept;
  [[nodiscard]] bool is_running() const noexcept;
  [[nodiscard]] std::size_t dropped_event_count() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::editor
