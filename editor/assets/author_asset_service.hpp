// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "assets/author_asset_monitor.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/function/files/service.hpp>

#include <memory>

namespace gneiss::editor {

/// 主线程调用；后台只持有文件监视模型，不访问 Session、World 或 UI。
/// 调度器必须活得更久；宿主分发文件完成，关闭仅取消自身请求与作用域。
class author_asset_service final {
public:
  explicit author_asset_service(gneiss::tasks::scheduler& scheduler);
  ~author_asset_service();
  /// 注入包含 asset:// 挂载的服务；内容上限 16 MiB，宿主持续分发到 stopped。
  [[nodiscard]] gneiss::error initialize(std::shared_ptr<gneiss::files::service> files);
  [[nodiscard]] author_asset_change observe(const std::filesystem::path& path, bool dirty);
  /// 内容必须是已成功保存的原始字节；后台计算基线，不重新读取磁盘冒充自身保存。
  [[nodiscard]] gneiss::error acknowledge(std::string_view uri, std::string content);
  void request_rescan();
  [[nodiscard]] gneiss::error poll_rescan(std::vector<std::filesystem::path>& output,
                                          std::size_t budget = 8U);
  [[nodiscard]] bool is_rescanning() const;
  [[nodiscard]] gneiss::error rescan_result() const;
  void mark_applied(std::string_view uri);
  void mark_failed(std::string_view uri, gneiss::error operation);
  [[nodiscard]] const author_asset_change& status() const;
  void request_stop();
  [[nodiscard]] bool stopped() const;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::editor
