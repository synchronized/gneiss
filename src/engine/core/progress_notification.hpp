// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

namespace gneiss::core {

/** 内部进展通知；只唤醒宿主，不执行用户逻辑，不回调生产者，不保存业务结果。
 * 可跨线程调用；生产者持有共享引用，宿主失效后通知对象仍可独立存活。 */
class progress_notification {
public:
  virtual ~progress_notification() = default;
  virtual void notify() noexcept = 0;
};

} // namespace gneiss::core
