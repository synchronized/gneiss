// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gneiss::tasks {

struct task_handle {
  std::uint64_t owner{};
  std::uint64_t id{};
};
struct task_scope {
  std::uint64_t owner{};
  std::uint64_t id{};
};
struct serial_queue {
  std::uint64_t owner{};
  std::uint64_t id{};
};
enum class task_state : std::uint8_t {
  waiting,
  running,
  succeeded,
  failed,
  cancelled,
  dependency_failed
};
enum class task_priority : std::uint8_t { normal, background };
enum class submit_result : std::uint8_t { success, invalid_argument, full, stopped };

struct task_outcome {
  task_state state{task_state::succeeded};
  std::string error{};
};
class task_context final {
public:
  explicit task_context(const std::atomic_bool& cancelled) noexcept : cancelled_(cancelled) {}
  [[nodiscard]] bool stop_requested() const noexcept { return cancelled_.load(); }

private:
  const std::atomic_bool& cancelled_;
};
struct task_description {
  std::string name{};
  task_scope scope{};
  serial_queue serial{};
  std::vector<task_handle> prerequisites{};
  task_priority priority{task_priority::background};
  std::chrono::steady_clock::time_point not_before{};
};
struct task_completion {
  task_handle task;
  std::string name{};
  task_outcome outcome;
  double queue_ms{};
  double execution_ms{};
};
struct scheduler_options {
  std::size_t workers{3U};
  std::size_t capacity{256U};
  // 零表示自动保留一个普通任务工作槽（单线程时后台仍可运行）。
  std::size_t background_limit{};
};
struct scheduler_stats {
  std::size_t waiting{};
  std::size_t running{};
  std::size_t retained{};
  std::uint64_t submitted{};
  std::uint64_t completed{};
  std::uint64_t rejected{};
  std::uint64_t failed{};
  std::uint64_t cancelled{};
};

/// 内部线程安全调度器。poll/close_scope 回收结果后句柄失效；已提交依赖仍持有前置终态。
/// stop 和析构由宿主线程串行调用；不得从自身任务销毁调度器。服务须在调度器之前销毁。
class task_scheduler final {
public:
  using task_function = std::function<task_outcome(const task_context&)>;
  explicit task_scheduler(scheduler_options options = {});
  ~task_scheduler();
  task_scheduler(const task_scheduler&) = delete;
  task_scheduler& operator=(const task_scheduler&) = delete;
  [[nodiscard]] task_scope make_scope();
  [[nodiscard]] serial_queue make_serial_queue(task_scope scope);
  [[nodiscard]] submit_result submit(task_description description, task_function function,
                                     task_handle& output);
  [[nodiscard]] bool cancel(task_handle task);
  void cancel_scope(task_scope scope);
  [[nodiscard]] bool idle(task_scope scope) const;
  /// 取消并等待该作用域，丢弃尚未消费的结果。池内调用返回 false，不发生自等待。
  [[nodiscard]] bool close_scope(task_scope scope);
  std::size_t poll(task_scope scope, std::vector<task_completion>& output,
                   std::size_t budget = 64U);
  [[nodiscard]] bool query(task_handle task, task_completion& output) const;
  [[nodiscard]] scheduler_stats stats() const;
  void stop();

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::tasks
