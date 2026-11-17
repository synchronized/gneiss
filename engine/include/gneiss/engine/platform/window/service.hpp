// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/platform/window/native_window_info.hpp>

#include <gneiss/engine/core/input/types.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/platform/window/configuration.hpp>

#include <memory>

namespace gneiss::window {

/** 窗口与输入事件服务；所属线程创建、调用和销毁。原生窗口信息仅在服务存活期间借用。
 * 构造分配私有实现，可能抛出 bad_alloc；运行操作通过 result 返回错误。 */
class service final {
public:
  service();
  ~service();
  service(const service&) = delete;
  service& operator=(const service&) = delete;
  service(service&&) = delete;
  service& operator=(service&&) = delete;
  [[nodiscard]] error initialize(const configuration& config) noexcept;
  [[nodiscard]] error poll(bool& out_should_close, bool& out_focus_lost) noexcept;
  [[nodiscard]] error poll_input(gneiss::input::event& out_event) noexcept;
  [[nodiscard]] error keyboard(gneiss::input::keyboard_state& out_state) const noexcept;
  [[nodiscard]] error pointer(gneiss::input::pointer_state& out_state) const noexcept;
  [[nodiscard]] const native_window_info& native_window() const noexcept;
  [[nodiscard]] native_window_info& native_window() noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::window
