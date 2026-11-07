// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

namespace gneiss::engine {

/** 内部唤醒信号；只唤醒宿主，不执行用户逻辑，不回调生产者，不保存业务结果。
 * 可跨线程调用；生产者持有共享引用，宿主失效后信号仍可独立存活。
 * 可能在执行器锁内调用；实现不得重入执行器或调用业务代码。 */
class wakeup_signal {
public:
  virtual ~wakeup_signal() = default;
  virtual void notify() noexcept = 0;
};

} // namespace gneiss::engine
