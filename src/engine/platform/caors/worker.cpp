// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "adapter.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include <mutex>

namespace gneiss::caors_internal {
namespace {
// 每个适配器只占用宿主的一个 scope，不创建第二套线程池。
class worker_context final : public worker_adapter {
public:
  explicit worker_context(gneiss::tasks::task_executor& executor)
      : executor_(executor), scope_(executor.make_scope()) {}
  caors::error post(caors::tasks::task task) override {
    const std::scoped_lock lock(mutex_);
    if (closed_)
      return {caors::error_domain::core, caors::errc::frozen};
    auto owned = std::make_shared<caors::tasks::task>(std::move(task));
    gneiss::tasks::task_handle handle;
    const auto status = executor_.submit(
        {.name = "caors", .scope = scope_},
        [owned](const auto&) {
          (void)(*owned)();
          return gneiss::tasks::task_outcome{};
        },
        handle);
    return status == gneiss::tasks::submit_result::success
               ? caors::error{}
               : caors::error{caors::error_domain::core, caors::errc::frozen};
  }
  // 只能由宿主线程调用；任务内关闭由宿主协议禁止。
  void close() noexcept override {
    {
      const std::scoped_lock lock(mutex_);
      closed_ = true;
    }
    (void)executor_.close_scope(scope_);
  }
  void collect() override {
    std::vector<gneiss::tasks::task_completion> values;
    (void)executor_.poll(scope_, values);
  }

private:
  gneiss::tasks::task_executor& executor_;
  gneiss::tasks::task_scope scope_;
  std::mutex mutex_;
  bool closed_{};
};

}
std::shared_ptr<worker_adapter> make_worker(gneiss::tasks::task_executor& executor) {
  return std::make_shared<worker_context>(executor);
}
}
