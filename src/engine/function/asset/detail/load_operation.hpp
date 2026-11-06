// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_request.hpp"
#include "engine/function/asset/detail/asset_entry.hpp"
namespace gneiss::engine::detail {
/** 记录持有当前操作；请求只共享观察状态，执行队列不承担身份查询。 */
struct load_operation {
  virtual ~load_operation() = default;
  std::weak_ptr<asset_entry> entry;
  /** 所属线程检查外部订阅；操作本身持有的 observer 不算观察者。 */
  [[nodiscard]] bool has_observers() const noexcept { return observer.use_count() > 1; }
  /** 只有当前记录仍指向本操作时，才允许采用资产内容。 */
  [[nodiscard]] bool is_current() const noexcept {
    auto record = entry.lock();
    return record && record->loading.get() == this;
  }
  /** 清理旧操作不能清除后来提交的新操作。调用方须保活本操作。 */
  void detach() noexcept {
    if (auto record = entry.lock(); record && record->loading.get() == this)
      record->loading.reset();
  }
  /** 停止观察并释放当前记录；不替代具体 I/O、任务或依赖的取消。 */
  void stop() noexcept {
    observer->status = error::invalid_state;
    detach();
  }
  std::shared_ptr<asset_state> observer{std::make_shared<asset_state>()};
};
}
