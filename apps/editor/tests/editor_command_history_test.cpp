// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_command_history.h"

int main() {
  gneiss::editor::editor_command_history history;
  int value = 1;
  if (history.record({.label = "设置值",
                      .undo =
                          [&value] {
                            value = 0;
                            return gneiss::result::success;
                          },
                      .redo =
                          [&value] {
                            value = 1;
                            return gneiss::result::success;
                          }}) != gneiss::result::success ||
      !history.can_undo() || history.can_redo() || history.undo() != gneiss::result::success ||
      value != 0 || history.redo() != gneiss::result::success || value != 1) {
    return 1;
  }
  if (history.undo() != gneiss::result::success ||
      history.record({.label = "新操作",
                      .undo = [] { return gneiss::result::io; },
                      .redo = [] { return gneiss::result::success; }}) != gneiss::result::success ||
      history.can_redo() || history.undo() != gneiss::result::io || !history.can_undo()) {
    return 2;
  }
  history.clear();
  return history.can_undo() || history.can_redo() ? 3 : 0;
}
