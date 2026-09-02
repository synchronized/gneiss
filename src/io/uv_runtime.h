// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_UV_RUNTIME_H_
#define GNEISS_SRC_IO_UV_RUNTIME_H_

#include <gneiss/core/result.hpp>

#include <cstddef>
#include <functional>
#include <memory>

namespace gneiss {

/**
 * 在专用线程运行单个 libuv loop 的内部执行器。
 *
 * 除 post() 外调用方负责外部同步；任务始终在 I/O 线程执行。对象必须由非 I/O 线程销毁。
 */
class uv_runtime final {
public:
  using task = std::function<void()>;

  explicit uv_runtime(std::size_t queue_capacity = 256U);
  ~uv_runtime();

  uv_runtime(const uv_runtime&) = delete;
  uv_runtime& operator=(const uv_runtime&) = delete;

  [[nodiscard]] result start() noexcept;
  [[nodiscard]] result post(task operation) noexcept;
  [[nodiscard]] result stop() noexcept;

  [[nodiscard]] bool is_running() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss

#endif
