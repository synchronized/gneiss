// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "author_asset_monitor.h"
#include "core/tasks/task_scheduler.h"

#include <memory>

namespace gneiss::editor {

/// 主线程调用；后台只持有文件监视模型，不访问 Session、World 或 UI。
/// 调度器必须活得更久；关闭仅取消自身作用域。
class author_asset_service final {
public:
  explicit author_asset_service(tasks::task_scheduler& scheduler);
  ~author_asset_service();
  [[nodiscard]] result initialize(const std::filesystem::path& root);
  [[nodiscard]] author_asset_change observe(const std::filesystem::path& path, bool dirty);
  /// 内容必须是已成功保存的原始字节；后台计算基线，不重新读取磁盘冒充自身保存。
  [[nodiscard]] result acknowledge(std::string_view uri, std::string content);
  void request_rescan();
  [[nodiscard]] result poll_rescan(std::vector<std::filesystem::path>& output,
                                   std::size_t budget = 8U);
  [[nodiscard]] bool is_rescanning() const;
  [[nodiscard]] result rescan_result() const;
  void mark_applied(std::string_view uri);
  void mark_failed(std::string_view uri, result operation);
  [[nodiscard]] const author_asset_change& status() const;
  void request_stop();
  [[nodiscard]] bool stopped() const;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::editor
