// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_DIAGNOSTICS_LOOP_TIMING_HPP
#define GNEISS_CORE_DIAGNOSTICS_LOOP_TIMING_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <utility>

namespace gneiss::diagnostics {

// 仅用于所属线程的可选观测。嵌套阶段是包含关系，不能重复相加。
enum class loop_stage : std::uint8_t {
  events,
  update,
  render,
  draw_clear,
  scene_advance,
  texture_advance,
  asset_stage,
  upload_poll,
  upload_submit,
  asset_publish,
  candidate_cleanup,
  retirement,
  frame_storage,
  render_snapshot,
  frame_capture,
  frame_submit,
  window_pump,
  window_events,
  input_state,
  input_events,
  task_collect,
  task_submit,
  idle_wait,
  task_submit_lock,
  task_submit_work,
  task_submit_allocate,
  task_submit_insert,
  task_submit_notify,
  idle_lock,
  idle_condition,
  scene_verify,
  scene_builder_create,
  scene_asset_service_reset,
  scene_build,
  scene_description_move,
  scene_instance_allocate,
  scene_instance_initialize,
  scene_node_index_reserve,
  count,
};
inline constexpr std::array loop_stage_names{
    "events",
    "update",
    "render",
    "draw_clear",
    "scene_advance",
    "texture_advance",
    "asset_stage",
    "upload_poll",
    "upload_submit",
    "asset_publish",
    "candidate_cleanup",
    "retirement",
    "frame_storage",
    "render_snapshot",
    "frame_capture",
    "frame_submit",
    "window_pump",
    "window_events",
    "input_state",
    "input_events",
    "task_collect",
    "task_submit",
    "idle_wait",
    "task_submit_lock",
    "task_submit_work",
    "task_submit_allocate",
    "task_submit_insert",
    "task_submit_notify",
    "idle_lock",
    "idle_condition",
    "scene_verify",
    "scene_builder_create",
    "scene_asset_service_reset",
    "scene_build",
    "scene_description_move",
    "scene_instance_allocate",
    "scene_instance_initialize",
    "scene_node_index_reserve",
};
static_assert(loop_stage_names.size() == static_cast<std::size_t>(loop_stage::count));
using timing_clock = std::chrono::steady_clock;
using stage_times = std::array<double, static_cast<std::size_t>(loop_stage::count)>;
inline thread_local stage_times* active_loop_times = nullptr;

class loop_span final {
public:
  explicit loop_span(loop_stage stage) noexcept
      : loop_span(active_loop_times != nullptr
                      ? &(*active_loop_times)[static_cast<std::size_t>(stage)]
                      : nullptr) {}
  explicit loop_span(double* output) noexcept
      : output_(output),
        started_(output_ != nullptr ? timing_clock::now() : timing_clock::time_point{}) {}
  ~loop_span() {
    if (output_ != nullptr) {
      *output_ += std::chrono::duration<double, std::milli>(timing_clock::now() - started_).count();
    }
  }
  loop_span(const loop_span&) = delete;
  loop_span& operator=(const loop_span&) = delete;

private:
  double* output_;
  timing_clock::time_point started_;
};

template <typename Function>
decltype(auto) measure(loop_stage stage,
                       Function&& function) noexcept(noexcept(std::forward<Function>(function)())) {
  const loop_span span(stage);
  return std::forward<Function>(function)();
}

template <typename Function>
decltype(auto) measure(double* output,
                       Function&& function) noexcept(noexcept(std::forward<Function>(function)())) {
  const loop_span span(output);
  return std::forward<Function>(function)();
}

struct loop_record {
  std::uint64_t frame{};
  double start_ms{};
  double total_ms{};
  double gap_ms{};
  stage_times stages{};
};

class loop_timing final {
public:
  static constexpr std::size_t capacity = 128U;
  [[nodiscard]] double gap_before(timing_clock::time_point start) const noexcept {
    return previous_end_ == timing_clock::time_point{}
               ? 0.0
               : std::chrono::duration<double, std::milli>(start - previous_end_).count();
  }
  void mark_end(timing_clock::time_point end) noexcept { previous_end_ = end; }
  void add(const loop_record& record) noexcept {
    ++count_;
    sum_ms_ += record.total_ms;
    maximum_ms_ = std::max(maximum_ms_, record.total_ms);
    for (std::size_t index = 0; index < thresholds_.size(); ++index) {
      exceeds_[index] += record.total_ms > thresholds_[index] ? 1U : 0U;
    }
    for (std::size_t index = 0; index < maxima_.size(); ++index) {
      if (record.stages[index] > maxima_[index]) {
        maxima_[index] = record.stages[index];
        stage_peaks_[index] = record;
      }
    }
    if (retained_ < capacity) {
      longest_[retained_++] = record;
      if (retained_ == capacity) {
        update_smallest();
      }
    } else if (record.total_ms > longest_[smallest_].total_ms) {
      longest_[smallest_] = record;
      update_smallest();
    }
  }
  [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
  [[nodiscard]] std::size_t retained() const noexcept { return retained_; }
  [[nodiscard]] double maximum_ms() const noexcept { return maximum_ms_; }
  void write(std::ostream& output) const {
    output << "kind,frame,start_ms,total_ms,gap_ms";
    for (const auto* name : loop_stage_names) {
      output << ',' << name << "_ms";
    }
    output << '\n';
    for (std::size_t index = 0; index < retained_; ++index) {
      const auto& record = longest_[index];
      output << "sample," << record.frame << ',' << record.start_ms << ',' << record.total_ms << ','
             << record.gap_ms;
      for (auto value : record.stages) {
        output << ',' << value;
      }
      output << '\n';
    }
    // 阶段峰值可能不在最长的 128 轮里，单独保留它的完整上下文。
    for (std::size_t index = 0; index < maxima_.size(); ++index) {
      if (maxima_[index] <= 0.0) {
        continue;
      }
      const auto& record = stage_peaks_[index];
      output << "peak:" << loop_stage_names[index] << ',' << record.frame << ',' << record.start_ms
             << ',' << record.total_ms << ',' << record.gap_ms;
      for (auto value : record.stages) {
        output << ',' << value;
      }
      output << '\n';
    }
    output << "maxima," << count_ << ",0," << maximum_ms_ << ",0";
    for (auto value : maxima_) {
      output << ',' << value;
    }
    output << "\n# total_ms=" << sum_ms_;
    for (std::size_t index = 0; index < thresholds_.size(); ++index) {
      output << ",over_" << thresholds_[index] << "ms=" << exceeds_[index];
    }
    output << '\n';
  }

private:
  void update_smallest() noexcept {
    smallest_ = static_cast<std::size_t>(
        std::ranges::min_element(longest_, {}, &loop_record::total_ms) - longest_.begin());
  }
  timing_clock::time_point previous_end_;
  std::size_t smallest_{};
  static constexpr std::array thresholds_{16.7, 33.3, 50.0, 100.0};
  std::array<std::uint64_t, thresholds_.size()> exceeds_{};
  std::array<loop_record, capacity> longest_{};
  stage_times maxima_{};
  std::array<loop_record, static_cast<std::size_t>(loop_stage::count)> stage_peaks_{};
  std::size_t retained_{};
  std::uint64_t count_{};
  double sum_ms_{};
  double maximum_ms_{};
};

// RAII 确保早退也闭合样本；嵌套 Application 恢复外层线程观测上下文。
class loop_frame final {
public:
  loop_frame(loop_timing* output, std::uint64_t frame, timing_clock::time_point origin) noexcept
      : output_(output), previous_(active_loop_times),
        started_(output != nullptr ? timing_clock::now() : timing_clock::time_point{}) {
    active_loop_times = nullptr;
    if (output != nullptr) {
      record_.emplace();
      active_loop_times = &record_->stages;
      record_->frame = frame;
      record_->gap_ms = output->gap_before(started_);
      record_->start_ms = std::chrono::duration<double, std::milli>(started_ - origin).count();
    }
  }
  ~loop_frame() {
    active_loop_times = previous_;
    if (output_ != nullptr && record_.has_value()) {
      const auto ended = timing_clock::now();
      record_->total_ms =
          record_->gap_ms + std::chrono::duration<double, std::milli>(ended - started_).count();
      output_->mark_end(ended);
      output_->add(*record_);
    }
  }
  loop_frame(const loop_frame&) = delete;
  loop_frame& operator=(const loop_frame&) = delete;

private:
  loop_timing* output_;
  stage_times* previous_;
  timing_clock::time_point started_;
  std::optional<loop_record> record_;
};
} // namespace gneiss::diagnostics

#endif
