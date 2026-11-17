// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/error.hpp>

#include <cstddef>
#include <functional>
#include <memory>

namespace gneiss::io {

namespace detail {
class uv_loop_access;
}

/**
 * 在专用线程运行单个 libuv loop 的内部执行器。
 *
 * 除 post() 外调用方负责外部同步；任务始终在 I/O 线程执行。对象必须由非 I/O 线程销毁。
 */
class executor final {
public:
  using task = std::function<void()>;

  explicit executor(std::size_t queue_capacity = 256U);
  ~executor();

  executor(const executor&) = delete;
  executor& operator=(const executor&) = delete;

  [[nodiscard]] error start();
  [[nodiscard]] error post(task operation);
  [[nodiscard]] error stop() noexcept;

  [[nodiscard]] bool is_running() const noexcept;

private:
  friend class detail::uv_loop_access;
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::io
