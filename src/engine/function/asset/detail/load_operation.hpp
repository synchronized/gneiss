// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_request.hpp"
#include "engine/function/asset/detail/asset_entry.hpp"
#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <vector>
namespace gneiss::engine::detail {
/** 记录持有当前操作；请求只共享观察状态，执行队列不承担身份查询。 */
struct load_operation {
  virtual ~load_operation() = default;
  std::atomic_bool queued{};
  std::function<void()> notify;
  std::weak_ptr<asset_entry> entry;
  /** 所属线程检查请求或资源拥有者；操作自身的引用不算观察者。 */
  [[nodiscard]] bool has_observers() const noexcept {
    return observer.use_count() > 1 || observer->target.use_count() > 1 ||
           observer->identity.use_count() > identity_owners;
  }
  /** 只有当前记录仍指向本操作时，才允许采用资产内容。 */
  [[nodiscard]] bool is_current() const noexcept {
    auto record = entry.lock();
    return record && record->loading.get() == this;
  }
  /** 清理旧操作不能清除后来提交的新操作。调用方须保活本操作。 */
  void detach() noexcept {
    if (auto record = entry.lock(); record && record->loading.get() == this) {
      record->pending_dependencies.clear();
      record->loading.reset();
    }
  }
  /** 所属线程提交一次终态；停止或已完成后，迟到结果不能覆盖观察状态。 */
  bool complete(error status, serialization::diagnostic diagnostic = {},
                std::shared_ptr<const void> value = {}, std::size_t source_bytes = 0,
                asset_load_metrics metrics = {}) {
    if (status == error::not_ready)
      return false;
    std::vector<std::function<void()>> listeners;
    {
      const std::scoped_lock lock(observer->mutex);
      if (observer->status != error::not_ready)
        return false;
      diagnostic.error = status;
      observer->diagnostic = std::move(diagnostic);
      observer->metrics = std::move(metrics);
      observer->source_bytes = source_bytes;
      if (status.ok())
        observer->value = std::move(value);
      observer->status = status;
      listeners = std::move(observer->completed);
    }
    if (status.ok())
      if (auto record = entry.lock(); record && record->loading.get() == this)
        record->owned_dependencies = std::move(record->pending_dependencies);
    detach();
    for (auto& listener : listeners)
      listener();
    return true;
  }
  /** 不替代具体 I/O、任务或依赖取消；已完成结果不再改写。 */
  void stop() { (void)complete(error::invalid_state); }
  // 操作 previous 与观察状态可能共同持有预留身份。
  long identity_owners{2};
  std::shared_ptr<asset_state> observer{std::make_shared<asset_state>()};
};
/** 通知仅保留弱引用；活跃表拥有操作，服务关闭后迟到通知无害。 */
template <class Operation> struct operation_queue {
  std::mutex mutex;
  std::deque<std::weak_ptr<Operation>> pending;
  void push(const std::shared_ptr<Operation>& work) {
    std::scoped_lock lock(mutex);
    if (!work->queued.exchange(true))
      pending.push_back(work);
  }
  std::vector<std::shared_ptr<Operation>> take() {
    std::vector<std::shared_ptr<Operation>> result;
    std::scoped_lock lock(mutex);
    while (!pending.empty()) {
      if (auto work = pending.front().lock()) {
        work->queued.store(false);
        result.push_back(std::move(work));
      }
      pending.pop_front();
    }
    return result;
  }
};
template <class Operation>
void connect_operation(const std::shared_ptr<Operation>& work,
                       const std::shared_ptr<operation_queue<Operation>>& queue) {
  work->notify = [weak = std::weak_ptr(work), ready = std::weak_ptr(queue)] {
    if (auto operation = weak.lock())
      if (auto target = ready.lock())
        target->push(operation);
  };
  work->observer->changed = work->notify;
}
/** 只恢复就绪项；没有事件的等待项不参与本轮遍历。 */
template <class Operation, class Advance, class Cancel>
void advance_load_operations(std::map<Operation*, std::shared_ptr<Operation>>& active,
                             operation_queue<Operation>& ready, Advance advance, Cancel cancel) {
  for (auto& work : ready.take()) {
    if (!active.contains(work.get()))
      continue;
    if (work->observer->status == error::not_ready) {
      if (!work->has_observers()) {
        cancel(work);
        work->complete(error::invalid_state, {.message = "加载已无观察者"});
      } else {
        advance(work);
      }
    }
    if (work->observer->status != error::not_ready) {
      work->detach();
      active.erase(work.get());
    }
  }
}
}
