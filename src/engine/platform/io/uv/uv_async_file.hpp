// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/result.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace gneiss::engine::platform::io {
class uv_loop_executor;
namespace file_detail {
struct budget;
struct file;
struct opening;
struct reading;
} // namespace file_detail
class uv_async_file;

/** 单消费者回执；销毁仅放弃结果。文件独立于打开回执存活。 */
class uv_file_open_operation final {
public:
  explicit uv_file_open_operation(std::shared_ptr<file_detail::opening> state) noexcept;
  ~uv_file_open_operation();
  uv_file_open_operation(const uv_file_open_operation&) = delete;
  uv_file_open_operation& operator=(const uv_file_open_operation&) = delete;
  [[nodiscard]] result poll(std::shared_ptr<uv_async_file>& output) noexcept;

private:
  std::shared_ptr<file_detail::opening> state_;
  bool delivered_{};
};

/** 请求拥有缓冲与文件引用；成功视图有效至回执销毁。等待仅供可阻塞线程使用。 */
class uv_file_read_operation final {
public:
  explicit uv_file_read_operation(std::shared_ptr<file_detail::reading> state) noexcept;
  ~uv_file_read_operation();
  uv_file_read_operation(const uv_file_read_operation&) = delete;
  uv_file_read_operation& operator=(const uv_file_read_operation&) = delete;
  [[nodiscard]] result poll(std::span<const std::byte>& output) noexcept;
  [[nodiscard]] result wait_for(std::chrono::milliseconds timeout,
                                std::span<const std::byte>& output) noexcept;

private:
  std::shared_ptr<file_detail::reading> state_;
};

/** 只读文件；最后一次使用后在 loop 异步关闭，析构不等待 I/O。
 * 所属执行器须持续运行至全部文件与在途请求释放；禁止在 loop 内同步等待读取。 */
class uv_async_file final : public std::enable_shared_from_this<uv_async_file> {
public:
  explicit uv_async_file(std::shared_ptr<file_detail::file> state) noexcept;
  ~uv_async_file();
  uv_async_file(const uv_async_file&) = delete;
  uv_async_file& operator=(const uv_async_file&) = delete;
  [[nodiscard]] std::uint64_t size() const noexcept;
  /** 禁止新读并异步关闭；重复调用共享终态，在途读仍正常完成。 */
  void begin_close() const noexcept;
  /** 不阻塞；成功表示底层文件与唤醒句柄均已关闭。 */
  [[nodiscard]] result poll_close() const noexcept;
  /** 显式同步精确读取，不分配负载缓冲；只用于允许阻塞的调用点。 */
  [[nodiscard]] result read_at(std::uint64_t offset, std::span<std::byte> output) const noexcept;
  [[nodiscard]] result begin_read(std::uint64_t offset, std::size_t count,
                                  std::unique_ptr<uv_file_read_operation>& output) const noexcept;

private:
  friend struct file_detail::reading;
  std::shared_ptr<file_detail::file> state_;
};

struct uv_file_limits {
  std::size_t requests = 64U;
  std::size_t bytes = 64U * 1024U * 1024U;
};
/** 借用已启动的执行器，不创建线程。容量覆盖打开请求和读取缓冲，满时返回 not_ready。
 * 宿主先释放文件与回执，最后停止执行器。成功回执销毁前也占用容量，避免积压失控。 */
class uv_file_io final {
public:
  explicit uv_file_io(uv_loop_executor& executor, uv_file_limits limits = {});
  [[nodiscard]] result begin_open(std::string_view path,
                                  std::unique_ptr<uv_file_open_operation>& output) const noexcept;

private:
  uv_loop_executor& executor_;
  std::shared_ptr<file_detail::budget> budget_;
};
} // namespace gneiss::engine::platform::io
