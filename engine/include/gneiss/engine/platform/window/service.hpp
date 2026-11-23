// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/platform/window/export.hpp>

#include <gneiss/engine/platform/window/native_handle.hpp>
#include <gneiss/engine/platform/window/surface_state.hpp>

#include <gneiss/engine/platform/window/input.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/platform/window/configuration.hpp>

#include <gneiss/engine/core/base/result.hpp>

#include <functional>
#include <memory>

namespace gneiss::window {

/** 单次采集的窗口通知；失焦在本轮输入分发后处理，不表达跨队列时序。 */
struct event_status {
  bool close_requested{};
  bool focus_lost{};
};

/** 窗口与输入事件服务；所属线程创建、调用和销毁。原生窗口信息仅在服务存活期间借用。
 * 构造可能抛出 bad_alloc；平台失败通过 result/error 返回，采集回调异常向调用方传播。 */
class service final {
public:
  GNEISS_WINDOW_API service();
  GNEISS_WINDOW_API ~service();
  service(const service&) = delete;
  service& operator=(const service&) = delete;
  service(service&&) = delete;
  service& operator=(service&&) = delete;
  [[nodiscard]] GNEISS_WINDOW_API error initialize(const configuration& config) noexcept;
  /** 泵送一次并同步交付输入，回调不保存；不得重入。失败可能已交付部分事件。 */
  [[nodiscard]] GNEISS_WINDOW_API result<event_status>
  poll_events(const std::function<error(const input::event&)>& on_input);
  [[nodiscard]] GNEISS_WINDOW_API window::id id() const noexcept;
  [[nodiscard]] GNEISS_WINDOW_API native_handle native() const noexcept;
  [[nodiscard]] GNEISS_WINDOW_API surface_state surface() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::window
