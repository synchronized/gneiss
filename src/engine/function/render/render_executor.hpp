// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_EXECUTOR_HPP_
#define GNEISS_RENDER_RENDER_EXECUTOR_HPP_

#include "engine/core/progress_notification.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/render_frame_packet.hpp"
#include "engine/function/render/render_statistics.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace gneiss::engine {

enum class render_frame_policy : std::uint8_t { replaceable, required };

struct render_execution_result final {
  bool needs_recreate{};
  bool presented{};
  float frame_capture_ms{};
  std::size_t copied_payload_bytes{};
  float queue_wait_ms{};
  float resource_prepare_ms{};
  float acquire_wait_ms{};
  float record_submit_ms{};
  float present_wait_ms{};
  float render_thread_ms{};
  bool gpu_timing_supported{};
  bool gpu_timing_valid{};
  std::uint64_t gpu_timing_sample_sequence{};
  float gpu_frame_ms{};
  float gpu_shadow_ms{};
  float gpu_opaque_ms{};
  float gpu_tone_mapping_ms{};
  bool environment_asset_requested{};
  bool environment_fallback{};
  float environment_intensity{1.0F};
  float environment_rotation_radians{};
};

struct render_frame_completion final {
  std::uint64_t sequence{};
  error status{error::unknown};
  render_execution_result execution;
  bool dropped{};
  render_frame_policy policy{render_frame_policy::replaceable};
  render_frame_packet reusable_packet;
};

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

using render_frame_callback =
    std::function<engine::error(render_frame_packet&, render_execution_result&)>;
using render_command_callback = std::function<engine::error(const render_command_reporter&)>;

enum class render_command_policy : std::uint8_t { once, resumable };

class inline_render_executor final {
public:
  explicit inline_render_executor(render_frame_callback callback);
  [[nodiscard]] error submit(render_frame_packet packet, render_execution_result& output);
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

  [[nodiscard]] error initialize(render_frame_callback callback,
                                 std::size_t maximum_pending_frames = 3U) noexcept;
  [[nodiscard]] result<std::uint64_t>
  submit_frame(render_frame_packet packet,
               render_frame_policy policy = render_frame_policy::replaceable) noexcept;
  /** resumable 命令返回 NOT_READY 时保留同一序号排到队尾，后续帧和命令可先执行。
   * 回调须自行保存进度并最终返回终态；flush/stop 会等它结束，不强制销毁在途资源。
   * once 的 NOT_READY 仍是一次完成结果。 */
  [[nodiscard]] result<std::uint64_t>
  submit_command(render_command_callback command,
                 render_command_policy policy = render_command_policy::once) noexcept;
  [[nodiscard]] bool try_take_frame_completion(render_frame_completion& output) noexcept;
  [[nodiscard]] bool try_take_command_completion(render_command_completion& output) noexcept;
  [[nodiscard]] bool query_command_status(render_command_status& output) const noexcept;
  [[nodiscard]] render_queue_stats query_stats() const noexcept;
  void record_skipped_frame_build() noexcept;
  void set_completion_notification(std::shared_ptr<progress_notification> notification) noexcept;
  [[nodiscard]] error flush() noexcept;
  void stop() noexcept;
  [[nodiscard]] bool is_running() const noexcept;

private:
  struct state;
  std::unique_ptr<state> state_;
};

} // namespace gneiss::engine

#endif
