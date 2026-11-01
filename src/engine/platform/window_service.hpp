// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_PLATFORM_WINDOW_SERVICE_HPP_
#define GNEISS_PLATFORM_WINDOW_SERVICE_HPP_

#include "engine/platform/native_window_info.hpp"

#include "engine/core/error.hpp"
#include "engine/core/input/input_types.hpp"
#include "engine/platform/window_configuration.hpp"

#include <memory>

namespace gneiss::engine {

/** 窗口与输入事件服务；所属线程创建、调用和销毁。原生窗口信息仅在服务存活期间借用。
 * 构造分配私有实现，可能抛出 bad_alloc；运行操作通过 result 返回错误。 */
class window_service final {
public:
  window_service();
  ~window_service();
  window_service(const window_service&) = delete;
  window_service& operator=(const window_service&) = delete;
  window_service(window_service&&) = delete;
  window_service& operator=(window_service&&) = delete;
  [[nodiscard]] error initialize(const window_configuration& config) noexcept;
  [[nodiscard]] error poll(bool& out_should_close, bool& out_focus_lost) noexcept;
  [[nodiscard]] error poll_input(input_event& out_event) noexcept;
  [[nodiscard]] error keyboard(keyboard_state& out_state) const noexcept;
  [[nodiscard]] error pointer(pointer_state& out_state) const noexcept;
  [[nodiscard]] const native_window_info& native_window() const noexcept;
  [[nodiscard]] native_window_info& native_window() noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::engine

#endif
