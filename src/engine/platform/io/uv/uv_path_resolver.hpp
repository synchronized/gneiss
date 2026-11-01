// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/error.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace gneiss::engine {
class uv_loop_executor;

/** 异步解析结果；单消费者轮询。销毁仅放弃结果，不等待磁盘或执行器。
 * 成功转移 UTF-8 绝对路径一次；失败保持原结果。非成功时清空输出。 */
class uv_path_operation final {
public:
  ~uv_path_operation();
  uv_path_operation(const uv_path_operation&) = delete;
  uv_path_operation& operator=(const uv_path_operation&) = delete;
  [[nodiscard]] error poll(std::string& output) noexcept;

private:
  friend class uv_path_resolver;
  struct state;
  explicit uv_path_operation(std::shared_ptr<state> pending) noexcept;
  std::shared_ptr<state> pending_;
  bool delivered_{};
};

/** 通用路径解析，不承担资产根目录隔离策略，不自行启动线程。
 * 执行器须在提交期间存活；停止时由其排空在途请求。已提交请求独立于解析器存活。
 * 容量限制覆盖排队及 libuv 在途请求；满时返回 not_ready，不同步回退。 */
class uv_path_resolver final {
public:
  explicit uv_path_resolver(uv_loop_executor& executor, std::size_t capacity = 64U);
  [[nodiscard]] error begin(std::string_view path,
                            std::unique_ptr<uv_path_operation>& output) const noexcept;

private:
  uv_loop_executor& executor_;
  std::shared_ptr<std::atomic_size_t> active_;
  std::size_t capacity_;
};
} // namespace gneiss::engine
