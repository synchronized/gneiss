// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "console_model.hpp"

#include <array>

namespace gneiss::editor {

/** 窗口拥有的筛选与滚动状态，不保存日志副本；与模型在 Editor 主线程使用。 */
struct console_panel_state final {
  console_filter filter;
  std::array<char, 128> search{};
  std::array<char, 96> source{};
  std::array<char, 96> category{};
  bool paused = false;
  bool auto_scroll = true;
  std::uint64_t pause_entry_id = 0U;

  /** 暂停只固定可见记录截止位置，模型仍可继续接收和淘汰日志。 */
  void set_paused(const console_model& model, bool value) noexcept;
  [[nodiscard]] result visible_indices(const console_model& model,
                                       std::vector<std::size_t>& output);
};

/** 单次绘制借用的进程展示值；log_path 只需在绘制期间有效。 */
struct console_runtime_view final {
  bool building = false;
  bool running = false;
  bool started = false;
  int exit_code = 0;
  bool attempted = false;
  result operation = result::success;
  std::string_view log_path;
};

/** 单次绘制同步调用的可选清空操作；面板不保存 context 或取得进程所有权。 */
struct console_panel_actions final {
  void* context = nullptr;
  void (*clear)(void*) = nullptr;
};

void draw_console_panel(const console_model& model, console_panel_state& state, bool& is_open,
                        const console_runtime_view& runtime, const console_panel_actions& actions);

} // namespace gneiss::editor
