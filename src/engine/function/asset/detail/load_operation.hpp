// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_request.hpp"
#include "engine/function/asset/detail/asset_entry.hpp"
#include <algorithm>
#include <vector>
namespace gneiss::engine::detail {
/** 记录持有当前操作；请求只共享观察状态，执行队列不承担身份查询。 */
struct load_operation {
  virtual ~load_operation() = default;
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
                asset_load_metrics metrics = {}) noexcept {
    if (status == error::not_ready)
      return false;
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
    }
    if (status.ok())
      if (auto record = entry.lock(); record && record->loading.get() == this)
        record->owned_dependencies = std::move(record->pending_dependencies);
    detach();
    return true;
  }
  /** 不替代具体 I/O、任务或依赖取消；已完成结果不再改写。 */
  void stop() noexcept { (void)complete(error::invalid_state); }
  // 操作 previous 与观察状态可能共同持有预留身份。
  long identity_owners{2};
  std::shared_ptr<asset_state> observer{std::make_shared<asset_state>()};
};
/** 所属线程推进开始时已有的操作；回调可追加队列，不能移除队列元素。
 * 已运行任务由自身保活；取消回调只发出停止请求，不等待工作线程。 */
template <class Operation, class Advance, class Cancel>
void advance_load_operations(std::vector<std::shared_ptr<Operation>>& queue, Advance advance,
                             Cancel cancel) {
  const auto count = queue.size();
  for (std::size_t i = 0; i < count; ++i) {
    auto work = queue[i];
    if (work->observer->status != error::not_ready)
      continue;
    if (!work->has_observers()) {
      cancel(work);
      work->complete(error::invalid_state, {.message = "加载已无观察者"});
      continue;
    }
    advance(work);
  }
  std::erase_if(queue, [](const auto& work) {
    if (work->observer->status == error::not_ready)
      return false;
    work->detach();
    return true;
  });
}
}
