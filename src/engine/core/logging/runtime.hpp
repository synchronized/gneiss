// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/logger.hpp"
#include "engine/core/logging/sink.hpp"
#include <vector>

namespace gneiss::engine::logging {
/** 显式日志上下文，复用 logging::detail::async_queue 的唯一有界队列。
 * 宿主负责停止生产者后关闭；flush/close 不得从 Sink 回调调用。
 * 生命周期操作由宿主串行执行；Logger 可跨线程提交。 */
class GNEISS_API runtime final {
public:
  explicit runtime(std::vector<std::unique_ptr<sink>> outputs, std::size_t capacity = 1024);
  ~runtime();
  runtime(const runtime&) = delete;
  runtime& operator=(const runtime&) = delete;
  [[nodiscard]] logger get_logger(std::string_view channel) const;
  void set_level(level threshold);
  void flush();
  void close();
  /** 输出端 write/flush 返回失败的累计次数；不通过普通日志递归报告。 */
  [[nodiscard]] std::uint64_t output_failures() const noexcept;

private:
  std::shared_ptr<detail::runtime_state> state_;
};
}
