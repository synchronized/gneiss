// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "core/tasks/task_scheduler.h"
#include "core/tasks/task_platform.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace gneiss::tasks {
namespace {
std::atomic_uint64_t next_owner{1U};
thread_local const void* active_scheduler{};
thread_local const char thread_identity{};
bool terminal(task_state state) {
  return state != task_state::waiting && state != task_state::running;
}
}

struct task_scheduler::implementation {
  struct task {
    task_description description;
    task_function function;
    task_completion completion;
    std::atomic_bool cancellation{};
    std::vector<std::shared_ptr<task>> dependencies;
    std::chrono::steady_clock::time_point enqueued{};
    std::chrono::steady_clock::time_point started{};
  };
  struct lane {
    std::uint64_t scope{};
  };
  scheduler_options options;
  const std::uint64_t owner{next_owner.fetch_add(1U)};
  std::uint64_t sequence{1U};
  mutable detail::mutex mutex;
  detail::condition wake;
  std::map<std::uint64_t, bool> scopes;
  std::map<std::uint64_t, lane> lanes;
  std::map<std::uint64_t, std::shared_ptr<task>> tasks;
  scheduler_stats counters;
  bool stopping{};
  std::size_t running_background{};
  std::size_t normal_streak{};
  detail::workers threads;
  const void* host_thread{&thread_identity};
  auto now() const { return options.clock ? options.clock() : std::chrono::steady_clock::now(); }

  void finish(task& value, task_outcome outcome) {
    if (terminal(value.completion.outcome.state)) {
      return;
    }
    const auto now = this->now();
    if (value.completion.outcome.state == task_state::running) {
      value.completion.execution_ms =
          std::chrono::duration<double, std::milli>(now - value.started).count();
      if (value.description.priority == task_priority::background) {
        --running_background;
      }
    } else {
      value.completion.queue_ms =
          std::chrono::duration<double, std::milli>(now - value.enqueued).count();
    }
    value.completion.outcome = std::move(outcome);
    ++counters.completed;
    if (value.completion.outcome.state == task_state::cancelled) {
      ++counters.cancelled;
    }
    if (value.completion.outcome.state == task_state::failed ||
        value.completion.outcome.state == task_state::dependency_failed) {
      ++counters.failed;
    }
    // 终态无需保留整条前置链；其他任务已单独持有它所需的终态。
    value.dependencies.clear();
    wake.notify_all();
  }
  void request_cancel(task& value) {
    value.cancellation = true;
    if (value.completion.outcome.state == task_state::waiting) {
      finish(value, {.state = task_state::cancelled, .error = {}});
    }
  }
  bool scope_idle(std::uint64_t scope) const {
    return std::ranges::none_of(tasks, [scope](const auto& item) {
      return item.second->description.scope.id == scope &&
             !terminal(item.second->completion.outcome.state);
    });
  }
  std::shared_ptr<task> select(std::chrono::steady_clock::time_point& next_due) {
    std::shared_ptr<task> normal;
    std::shared_ptr<task> background;
    const auto now = this->now();
    for (auto& [id, value] : tasks) {
      (void)id;
      if (value->completion.outcome.state != task_state::waiting) {
        continue;
      }
      bool ready = true;
      bool dependency_failed = false;
      for (const auto& before : value->dependencies) {
        const auto state = before->completion.outcome.state;
        ready = ready && terminal(state);
        dependency_failed =
            dependency_failed || (terminal(state) && state != task_state::succeeded);
      }
      if (dependency_failed) {
        finish(*value, {.state = task_state::dependency_failed, .error = "前置任务未成功"});
        continue;
      }
      const bool serial_blocked =
          value->description.serial.id != 0U && std::ranges::any_of(tasks, [&](const auto& item) {
            return item.first < id &&
                   item.second->description.serial.id == value->description.serial.id &&
                   !terminal(item.second->completion.outcome.state);
          });
      if (!ready || serial_blocked) {
        continue;
      }
      if (value->description.not_before > now) {
        next_due = std::min(next_due, value->description.not_before);
        continue;
      }
      if (value->description.priority == task_priority::normal) {
        if (!normal) {
          normal = value;
        }
      } else if (!background && running_background < options.background_limit) {
        background = value;
      }
    }
    auto chosen = normal && (!background || normal_streak < 8U) ? normal : background;
    if (chosen) {
      if (chosen->description.priority == task_priority::background) {
        ++running_background;
        normal_streak = 0U;
      } else {
        ++normal_streak;
      }
      chosen->started = now;
      chosen->completion.queue_ms =
          std::chrono::duration<double, std::milli>(now - chosen->enqueued).count();
      chosen->completion.outcome.state = task_state::running;
    }
    return chosen;
  }
  void execute(const std::shared_ptr<task>& current, task_function function) {
    task_outcome outcome;
    try {
      outcome = function(task_context{current->cancellation});
      if (!terminal(outcome.state) || outcome.state == task_state::dependency_failed) {
        outcome = {.state = task_state::failed, .error = "任务返回非法终态"};
      }
    } catch (const std::exception& error) {
      outcome = {.state = task_state::failed, .error = error.what()};
    } catch (...) {
      outcome = {.state = task_state::failed, .error = "任务发生未知异常"};
    }
    // 运行任务的终态由任务体决定；迟到的取消不能撤销已经提交的业务操作。
    function = {};
    {
      std::scoped_lock lock(mutex);
      finish(*current, std::move(outcome));
    }
  }
  void run() {
    active_scheduler = this;
    for (;;) {
      std::shared_ptr<task> current;
      task_function function;
      {
        std::unique_lock lock(mutex);
        while (!current) {
          if (stopping) {
            active_scheduler = nullptr;
            return;
          }
          auto due = std::chrono::steady_clock::time_point::max();
          current = select(due);
          if (!current) {
            if (due == std::chrono::steady_clock::time_point::max()) {
              wake.wait(lock);
            } else {
              wake.wait_until(lock, due);
            }
          }
        }
        function = std::move(current->function);
      }
      execute(current, std::move(function));
    }
  }
};

task_scheduler::task_scheduler(scheduler_options options)
    : impl_(std::make_unique<implementation>()) {
  if ((options.mode != execution_mode::thread_pool &&
       options.mode != execution_mode::cooperative) ||
      options.workers > 64U || options.capacity == 0U ||
      (options.mode == execution_mode::thread_pool && options.workers == 0U)) {
    throw std::invalid_argument("调度器线程数或容量无效");
  }
#if !GNEISS_TASK_THREADS
  if (options.mode == execution_mode::thread_pool) {
    throw std::invalid_argument("工作线程后端不可用");
  }
#endif
  if (options.mode == execution_mode::cooperative) {
    options.workers = 1U;
    options.background_limit = 1U;
  }
  if (options.background_limit == 0U) {
    options.background_limit = std::max(std::size_t{1U}, options.workers - 1U);
  }
  if (options.background_limit > options.workers) {
    throw std::invalid_argument("后台并发预算超过线程数");
  }
  impl_->options = options;
  try {
    for (std::size_t i = 0; options.mode == execution_mode::thread_pool && i < options.workers;
         ++i) {
      impl_->threads.start([this] { impl_->run(); });
    }
  } catch (...) {
    stop();
    throw;
  }
}
task_scheduler::~task_scheduler() {
  try {
    stop();
  } catch (...) {
    // 从自身工作线程销毁违反所有权契约，无法安全释放仍被执行中的对象。
    std::terminate();
  }
}

task_scope task_scheduler::make_scope() {
  std::scoped_lock lock(impl_->mutex);
  if (impl_->stopping || impl_->scopes.size() >= impl_->options.capacity) {
    return {};
  }
  const auto id = impl_->sequence++;
  impl_->scopes.emplace(id, false);
  return {.owner = impl_->owner, .id = id};
}
serial_queue task_scheduler::make_serial_queue(task_scope scope) {
  std::scoped_lock lock(impl_->mutex);
  const auto found = impl_->scopes.find(scope.id);
  if (impl_->stopping || scope.owner != impl_->owner || found == impl_->scopes.end() ||
      found->second || impl_->lanes.size() >= impl_->options.capacity) {
    return {};
  }
  const auto id = impl_->sequence++;
  impl_->lanes.emplace(id, implementation::lane{.scope = scope.id});
  return {.owner = impl_->owner, .id = id};
}
submit_result task_scheduler::submit(task_description description, task_function function,
                                     task_handle& output) {
  output = {};
  std::scoped_lock lock(impl_->mutex);
  const auto reject = [&](submit_result result) {
    ++impl_->counters.rejected;
    return result;
  };
  const auto scope = impl_->scopes.find(description.scope.id);
  if (description.scope.owner != impl_->owner || scope == impl_->scopes.end() || !function ||
      description.prerequisites.size() > 32U) {
    return reject(submit_result::invalid_argument);
  }
  if (impl_->stopping || scope->second) {
    return reject(submit_result::stopped);
  }
  if (impl_->tasks.size() >= impl_->options.capacity) {
    return reject(submit_result::full);
  }
  const auto serial = impl_->lanes.find(description.serial.id);
  if ((description.serial.id != 0U || description.serial.owner != 0U) &&
      (description.serial.owner != impl_->owner || serial == impl_->lanes.end() ||
       serial->second.scope != description.scope.id)) {
    return reject(submit_result::invalid_argument);
  }
  auto value = std::make_shared<implementation::task>();
  for (const auto prerequisite : description.prerequisites) {
    const auto before = impl_->tasks.find(prerequisite.id);
    if (prerequisite.owner != impl_->owner || before == impl_->tasks.end()) {
      return reject(submit_result::invalid_argument);
    }
    value->dependencies.push_back(before->second);
  }
  value->enqueued = impl_->now();
  value->description = std::move(description);
  value->function = std::move(function);
  const task_handle handle{.owner = impl_->owner, .id = impl_->sequence++};
  value->completion = {.task = handle,
                       .name = value->description.name,
                       .outcome = {.state = task_state::waiting, .error = {}},
                       .queue_ms = 0,
                       .execution_ms = 0};
  impl_->tasks.emplace(handle.id, value);
  ++impl_->counters.submitted;
  output = handle;
  impl_->wake.notify_all();
  return submit_result::success;
}
bool task_scheduler::cancel(task_handle task) {
  std::scoped_lock lock(impl_->mutex);
  const auto found = impl_->tasks.find(task.id);
  if (task.owner != impl_->owner || found == impl_->tasks.end() ||
      terminal(found->second->completion.outcome.state)) {
    return false;
  }
  impl_->request_cancel(*found->second);
  return true;
}
void task_scheduler::cancel_scope(task_scope scope) {
  std::scoped_lock lock(impl_->mutex);
  if (scope.owner != impl_->owner || !impl_->scopes.contains(scope.id)) {
    return;
  }
  impl_->scopes[scope.id] = true;
  for (auto& [id, value] : impl_->tasks) {
    (void)id;
    if (value->description.scope.id == scope.id) {
      impl_->request_cancel(*value);
    }
  }
}
bool task_scheduler::idle(task_scope scope) const {
  std::scoped_lock lock(impl_->mutex);
  return scope.owner == impl_->owner && impl_->scopes.contains(scope.id) &&
         impl_->scope_idle(scope.id);
}
bool task_scheduler::close_scope(task_scope scope) {
  if (active_scheduler == impl_.get()) {
    return false;
  }
  cancel_scope(scope);
  {
    std::unique_lock lock(impl_->mutex);
    if (scope.owner != impl_->owner || !impl_->scopes.contains(scope.id)) {
      return false;
    }
    impl_->wake.wait(lock, [&] { return impl_->scope_idle(scope.id); });
  }
  return try_close_scope(scope);
}
bool task_scheduler::try_close_scope(task_scope scope) {
  if (active_scheduler == impl_.get()) {
    return false;
  }
  cancel_scope(scope);
  std::vector<task_function> cleanup;
  std::vector<std::shared_ptr<implementation::task>> retired;
  {
    std::unique_lock lock(impl_->mutex);
    if (scope.owner != impl_->owner || !impl_->scopes.contains(scope.id)) {
      return false;
    }
    if (!impl_->scope_idle(scope.id)) {
      return false;
    }
    for (auto iterator = impl_->tasks.begin(); iterator != impl_->tasks.end();) {
      if (iterator->second->description.scope.id == scope.id) {
        cleanup.push_back(std::move(iterator->second->function));
        retired.push_back(std::move(iterator->second));
        iterator = impl_->tasks.erase(iterator);
      } else {
        ++iterator;
      }
    }
    std::erase_if(impl_->lanes,
                  [scope](const auto& item) { return item.second.scope == scope.id; });
    impl_->scopes.erase(scope.id);
  }
  return true;
}
std::size_t task_scheduler::poll(task_scope scope, std::vector<task_completion>& output,
                                 std::size_t budget) {
  std::vector<task_function> cleanup;
  std::vector<std::shared_ptr<implementation::task>> retired;
  {
    std::scoped_lock lock(impl_->mutex);
    if (scope.owner != impl_->owner || !impl_->scopes.contains(scope.id)) {
      return 0U;
    }
    for (auto iterator = impl_->tasks.begin();
         iterator != impl_->tasks.end() && retired.size() < budget;) {
      auto& value = iterator->second;
      if (value->description.scope.id == scope.id && terminal(value->completion.outcome.state)) {
        output.push_back(value->completion);
        cleanup.push_back(std::move(value->function));
        retired.push_back(std::move(value));
        iterator = impl_->tasks.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  return retired.size();
}
bool task_scheduler::query(task_handle task, task_completion& output) const {
  std::scoped_lock lock(impl_->mutex);
  const auto found = impl_->tasks.find(task.id);
  if (task.owner != impl_->owner || found == impl_->tasks.end()) {
    return false;
  }
  output = found->second->completion;
  return true;
}
scheduler_stats task_scheduler::stats() const {
  std::scoped_lock lock(impl_->mutex);
  auto stats = impl_->counters;
  stats.retained = impl_->tasks.size();
  for (const auto& [id, value] : impl_->tasks) {
    (void)id;
    if (value->completion.outcome.state == task_state::waiting) {
      ++stats.waiting;
    }
    if (value->completion.outcome.state == task_state::running) {
      ++stats.running;
    }
  }
  return stats;
}
execution_mode task_scheduler::mode() const noexcept { return impl_->options.mode; }
drive_result task_scheduler::run_ready(drive_budget budget) {
  if (mode() != execution_mode::cooperative) {
    return {.status = drive_status::wrong_mode};
  }
  if (impl_->host_thread != &thread_identity) {
    return {.status = drive_status::wrong_thread};
  }
  if (active_scheduler != nullptr) {
    return {.status = drive_status::reentrant};
  }
  drive_result result;
  const auto start = impl_->now();
  struct execution_guard {
    explicit execution_guard(const void* value) { active_scheduler = value; }
    ~execution_guard() { active_scheduler = nullptr; }
  } guard{impl_.get()};
  while (result.executed < budget.max_tasks && impl_->now() - start < budget.max_time) {
    std::shared_ptr<implementation::task> current;
    task_function function;
    {
      std::scoped_lock lock(impl_->mutex);
      if (impl_->stopping) {
        result.status = drive_status::stopped;
        return result;
      }
      auto due = std::chrono::steady_clock::time_point::max();
      current = impl_->select(due);
      if (!current) {
        return result;
      }
      function = std::move(current->function);
    }
    impl_->execute(current, std::move(function));
    ++result.executed;
  }
  result.budget_exhausted = true;
  return result;
}
void task_scheduler::request_stop() {
  std::scoped_lock lock(impl_->mutex);
  impl_->stopping = true;
  for (auto& [id, value] : impl_->tasks) {
    (void)id;
    impl_->request_cancel(*value);
  }
  impl_->wake.notify_all();
}
bool task_scheduler::stopped() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->stopping && std::ranges::all_of(impl_->tasks, [](const auto& item) {
           return terminal(item.second->completion.outcome.state);
         });
}
void task_scheduler::stop() {
  if (mode() == execution_mode::cooperative && impl_->host_thread != &thread_identity) {
    throw std::logic_error("协作调度器必须由所属宿主线程停止");
  }
  if (active_scheduler == impl_.get()) {
    throw std::logic_error("不能从任务停止自身调度器");
  }
  request_stop();
  impl_->threads.join();
}

} // namespace gneiss::tasks
