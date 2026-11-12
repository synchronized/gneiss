// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_EXECUTOR_HPP_
#define GNEISS_RENDER_RENDER_EXECUTOR_HPP_

#include "engine/core/result.hpp"
#include "engine/core/wakeup_signal.hpp"
#include "engine/function/render/detail/frame_packet.hpp"
#include "engine/function/render/frame_result.hpp"
#include "engine/function/render/queue_options.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace gneiss::render::detail {

enum class render_command_stage : std::uint8_t { queued, preparing, uploading, completed };

struct render_command_progress final {
  render_command_stage stage{render_command_stage::queued};
  std::uint64_t completed_work{};
  std::uint64_t total_work{};
};

struct render_command_completion final {
  std::uint64_t sequence{};
  error status{error::unknown};
  render_command_progress progress;
  double execution_ms{};
};

struct render_command_status final {
  std::uint64_t sequence{};
  render_command_progress progress;
  bool active{};
};

class render_command_reporter final {
public:
  explicit render_command_reporter(
      std::function<void(render_command_stage, std::uint64_t, std::uint64_t)> publish)
      : publish_(std::move(publish)) {}
  void report(render_command_stage stage, std::uint64_t completed_work,
              std::uint64_t total_work) const;

private:
  std::function<void(render_command_stage, std::uint64_t, std::uint64_t)> publish_;
};

using render_frame_prepare = std::function<result<frame_packet>()>;

using render_frame_callback = std::function<gneiss::error(frame_packet&, execution_result&)>;
using render_command_callback = std::function<gneiss::error(const render_command_reporter&)>;

enum class render_command_policy : std::uint8_t { once, resumable };

class inline_render_executor final {
public:
  explicit inline_render_executor(render_frame_callback callback);
  [[nodiscard]] error submit(frame_packet packet, execution_result& output);
  [[nodiscard]] error flush() const noexcept { return error::success; }

private:
  render_frame_callback callback_;
  std::uint64_t next_sequence_{1U};
};

class threaded_render_executor final {
public:
  threaded_render_executor();
  ~threaded_render_executor();
  threaded_render_executor(const threaded_render_executor&) = delete;
  threaded_render_executor& operator=(const threaded_render_executor&) = delete;

  [[nodiscard]] error initialize(render_frame_callback callback, queue_options options = {});
  [[nodiscard]] result<std::uint64_t> submit_frame(frame_packet packet,
                                                   frame_policy policy = frame_policy::replaceable,
                                                   render_frame_prepare prepare = {});
  /** resumable 命令返回 NOT_READY 时保留同一序号排到队尾，后续帧和命令可先执行。
   * 回调须自行保存进度并最终返回终态；flush/stop 会等它结束，不强制销毁在途资源。
   * once 的 NOT_READY 仍是一次完成结果。 */
  [[nodiscard]] result<std::uint64_t>
  submit_command(render_command_callback command,
                 render_command_policy policy = render_command_policy::once);
  /** 线程安全领取；空队列返回空值，执行错误保存在完成记录的 status 中。 */
  [[nodiscard]] std::optional<frame_completion> try_take_frame_completion() noexcept;
  [[nodiscard]] std::optional<render_command_completion> try_take_command_completion() noexcept;
  [[nodiscard]] std::optional<render_command_status> query_command_status() const noexcept;
  [[nodiscard]] queue_stats query_stats() const noexcept;
  void record_skipped_frame_build() noexcept;
  void set_completion_wakeup(std::shared_ptr<wakeup_signal> notification) noexcept;
  /** 等待已提交工作执行及其捕获数据释放；不等同于 GPU 空闲。 */
  [[nodiscard]] error flush() noexcept;
  void stop() noexcept;
  [[nodiscard]] bool is_running() const noexcept;

private:
  struct state;
  std::unique_ptr<state> state_;
};

} // namespace gneiss

#endif
