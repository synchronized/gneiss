// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/core/tasks/export.hpp>

#include <gneiss/engine/core/tasks/wakeup_signal.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gneiss::tasks {

struct handle {
  std::uint64_t owner{};
  std::uint64_t id{};
};
struct scope {
  std::uint64_t owner{};
  std::uint64_t id{};
};
struct serial_queue {
  std::uint64_t owner{};
  std::uint64_t id{};
};
enum class state : std::uint8_t {
  waiting,
  running,
  succeeded,
  failed,
  cancelled,
  dependency_failed
};
enum class priority : std::uint8_t { normal, background };
enum class submit_result : std::uint8_t { success, invalid_argument, full, stopped };

struct outcome {
  tasks::state state{state::succeeded};
  std::string error{};
};
class context final {
public:
  explicit context(const std::atomic_bool& cancelled, bool worker_thread = false) noexcept
      : cancelled_(cancelled), worker_thread_(worker_thread) {}
  [[nodiscard]] bool stop_requested() const noexcept { return cancelled_.load(); }
  [[nodiscard]] bool allows_blocking_wait() const noexcept { return worker_thread_; }

private:
  const std::atomic_bool& cancelled_;
  bool worker_thread_;
};
/** 可选的同步提交观测；仅由 submit 调用线程写入，每次覆盖，不可并发复用。 */
struct submission_timings {
  bool measured{};
  double lock_ms{}, work_ms{}, allocate_ms{}, insert_ms{}, notify_ms{};
};
struct description {
  std::string name{};
  tasks::scope scope{};
  serial_queue serial{};
  std::vector<handle> prerequisites{};
  tasks::priority priority{priority::background};
  std::chrono::steady_clock::time_point not_before{};
  // 仅借用到 submit 返回；调度器不得将此指针留给任务或后台线程。
  tasks::submission_timings* submission_timings{};
};
struct completion {
  handle task;
  std::string name{};
  tasks::outcome outcome;
  double queue_ms{};
  double execution_ms{};
};
enum class execution_mode : std::uint8_t { thread_pool, cooperative };
enum class drive_status : std::uint8_t { success, wrong_mode, wrong_thread, reentrant, stopped };
struct drive_budget {
  std::size_t max_tasks{8U};
  std::chrono::nanoseconds max_time{std::chrono::milliseconds(2)};
};
struct drive_result {
  drive_status status{drive_status::success};
  std::size_t executed{};
  bool budget_exhausted{};
};
struct scheduler_options {
  std::size_t workers{3U};
  std::size_t capacity{256U};
  // 零表示自动保留一个普通任务工作槽（单线程时后台仍可运行）。
  std::size_t background_limit{};
  execution_mode mode{execution_mode::thread_pool};
  // 可选单调时钟；须不抛异常，线程池模式下须可并发调用。
  std::function<std::chrono::steady_clock::time_point()> clock{};
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

/** 宿主注入的内部执行入口；无独立状态。跨共享库时通过宿主虚调用保持唯一池和线程上下文。 */
class GNEISS_TASKS_TYPE_API executor {
public:
  using function = std::function<outcome(const context&)>;
  virtual ~executor() = default;
  /** 未声明线程能力的自定义执行器默认按协作模式处理。 */
  [[nodiscard]] virtual execution_mode mode() const noexcept { return execution_mode::cooperative; }
  /** 创建独立任务域；先发布终态再唤醒，信号保活至域关闭，不持有宿主。 */
  [[nodiscard]] virtual scope make_scope(std::shared_ptr<wakeup_signal> wakeup = {}) = 0;
  [[nodiscard]] virtual submit_result submit(description description, function function,
                                             handle& output) = 0;
  [[nodiscard]] virtual bool cancel(handle task) = 0;
  virtual void cancel_scope(scope scope) = 0;
  [[nodiscard]] virtual bool idle(scope scope) const = 0;
  [[nodiscard]] virtual bool close_scope(scope scope) = 0;
  /** 消费所属 scope 的终态队列；内置调度器按完成入队顺序交付，不扫描未完成任务。 */
  virtual std::size_t poll(scope scope, std::vector<completion>& output,
                           std::size_t budget = 64U) = 0;
};

/// 内部调度器；无线程构建仅允许所属宿主线程访问，启用线程时提交、查询和取消线程安全。
/// poll/close_scope 回收结果后句柄失效；已提交依赖仍持有前置终态。
/// stop 和析构由宿主线程串行调用；不得从自身任务销毁调度器。服务须在调度器之前销毁。
class GNEISS_TASKS_TYPE_API scheduler final : public executor {
public:
  using function = std::function<outcome(const context&)>;
  GNEISS_TASKS_API explicit scheduler(scheduler_options options = {});
  GNEISS_TASKS_API ~scheduler() override;
  scheduler(const scheduler&) = delete;
  scheduler& operator=(const scheduler&) = delete;
  [[nodiscard]] GNEISS_TASKS_API scope
  make_scope(std::shared_ptr<wakeup_signal> wakeup = {}) override;
  [[nodiscard]] GNEISS_TASKS_API serial_queue make_serial_queue(scope scope);
  [[nodiscard]] GNEISS_TASKS_API submit_result submit(description description, function function,
                                                      handle& output) override;
  [[nodiscard]] GNEISS_TASKS_API bool cancel(handle task) override;
  GNEISS_TASKS_API void cancel_scope(scope scope) override;
  [[nodiscard]] GNEISS_TASKS_API bool idle(scope scope) const override;
  /// 取消并等待该作用域，丢弃尚未消费的结果。池内调用返回 false，不发生自等待。
  [[nodiscard]] GNEISS_TASKS_API bool close_scope(scope scope) override;
  GNEISS_TASKS_API std::size_t poll(scope scope, std::vector<completion>& output,
                                    std::size_t budget = 64U) override;
  [[nodiscard]] GNEISS_TASKS_API bool query(handle task, completion& output) const;
  [[nodiscard]] GNEISS_TASKS_API scheduler_stats stats() const;
  [[nodiscard]] GNEISS_TASKS_API execution_mode mode() const noexcept override;
  /// 仅协作模式所属宿主线程调用，不允许嵌套驱动任何调度器。
  [[nodiscard]] GNEISS_TASKS_API drive_result run_ready(drive_budget budget = {});
  GNEISS_TASKS_API void request_stop();
  [[nodiscard]] GNEISS_TASKS_API bool stopped() const;
  /// 请求取消并尝试回收；尚有运行任务时返回 false，不阻塞。
  [[nodiscard]] GNEISS_TASKS_API bool try_close_scope(scope scope);
  GNEISS_TASKS_API void stop();

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::tasks
