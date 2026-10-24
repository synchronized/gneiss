// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include <exception>
#include <gneiss/engine/application.h>
#include <memory>
#include <utility>
namespace gneiss::runtime {
class application;
}
namespace gneiss::runtime {
/** 内部兼容桥；不持有 application，不授予关闭权。绑定、移动和撤销限所属线程。
 * 必须在最后一次兼容调用后销毁；对象先关闭时旧句柄立即不能再解析。 */
class application_bridge final {
public:
  application_bridge() noexcept = default;
  ~application_bridge() noexcept {
    if (reset() != gneiss::engine::core::result::success) {
      std::terminate();
    }
  }
  application_bridge(const application_bridge&) = delete;
  application_bridge& operator=(const application_bridge&) = delete;
  application_bridge(application_bridge&& other) noexcept
      : handle_(std::exchange(other.handle_, GNEISS_NULL_APPLICATION)) {}
  application_bridge& operator=(application_bridge&&) = delete;
  [[nodiscard]] GNEISS_API gneiss::engine::core::result
  bind(const std::shared_ptr<runtime::application>& application) noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::core::result reset() noexcept;
  [[nodiscard]] gneiss_application get() const noexcept { return handle_; }

private:
  gneiss_application handle_{};
};
} // namespace gneiss::runtime
