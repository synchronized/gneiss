// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_EXECUTOR_H_
#define GNEISS_RENDER_RENDER_EXECUTOR_H_

#include "render/render_frame_packet.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace gneiss::render_internal {

struct render_execution_result final {
  bool needs_recreate{};
  float queue_wait_ms{};
};

struct render_frame_completion final {
  std::uint64_t sequence{};
  gneiss_result status{GNEISS_ERROR_UNKNOWN};
  render_execution_result execution;
  bool dropped{};
};

enum class render_command_stage : std::uint8_t { queued, preparing, uploading, completed };

struct render_command_progress final {
  render_command_stage stage{render_command_stage::queued};
  std::uint64_t completed_work{};
  std::uint64_t total_work{};
};

struct render_command_completion final {
  std::uint64_t sequence{};
  gneiss_result status{GNEISS_ERROR_UNKNOWN};
  render_command_progress progress;
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

struct render_queue_stats final {
  std::size_t pending_high_watermark{};
  std::uint64_t replaced_frames{};
};

using render_frame_callback =
    std::function<gneiss_result(render_frame_packet&, render_execution_result&)>;
using render_command_callback = std::function<gneiss_result(const render_command_reporter&)>;

class inline_render_executor final {
public:
  explicit inline_render_executor(render_frame_callback callback);
  [[nodiscard]] gneiss_result submit(render_frame_packet packet, render_execution_result& output);
  [[nodiscard]] gneiss_result flush() const noexcept { return GNEISS_SUCCESS; }

private:
  render_frame_callback callback_;
};

class threaded_render_executor final {
public:
  threaded_render_executor();
  ~threaded_render_executor();
  threaded_render_executor(const threaded_render_executor&) = delete;
  threaded_render_executor& operator=(const threaded_render_executor&) = delete;

  [[nodiscard]] gneiss_result initialize(render_frame_callback callback,
                                         std::size_t maximum_pending_frames = 3U) noexcept;
  [[nodiscard]] gneiss_result submit_frame(render_frame_packet packet,
                                           std::uint64_t& out_sequence) noexcept;
  [[nodiscard]] gneiss_result submit_command(render_command_callback command,
                                             std::uint64_t& out_sequence) noexcept;
  [[nodiscard]] bool try_take_frame_completion(render_frame_completion& output) noexcept;
  [[nodiscard]] bool try_take_command_completion(render_command_completion& output) noexcept;
  [[nodiscard]] bool query_command_status(render_command_status& output) const noexcept;
  [[nodiscard]] render_queue_stats query_stats() const noexcept;
  [[nodiscard]] gneiss_result flush() noexcept;
  void stop() noexcept;
  [[nodiscard]] bool is_running() const noexcept;

private:
  struct state;
  std::unique_ptr<state> state_;
};

} // namespace gneiss::render_internal

#endif
