// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_COMMAND_HISTORY_H_
#define GNEISS_APPS_EDITOR_EDITOR_COMMAND_HISTORY_H_

#include <gneiss/core/result.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::editor {

/** 保存可逆编辑操作；命令失败时保持当前历史位置。 */
class editor_command_history final {
public:
  using operation = std::function<result()>;

  struct command final {
    std::string label;
    operation undo;
    operation redo;
  };

  /** 记录已经成功执行的命令；新命令会清空 redo。 */
  [[nodiscard]] result record(command value) noexcept;
  [[nodiscard]] result undo() noexcept;
  [[nodiscard]] result redo() noexcept;
  void clear() noexcept;

  [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
  [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
  [[nodiscard]] std::string_view undo_label() const noexcept;
  [[nodiscard]] std::string_view redo_label() const noexcept;

private:
  std::vector<command> undo_;
  std::vector<command> redo_;
};

} // namespace gneiss::editor

#endif
