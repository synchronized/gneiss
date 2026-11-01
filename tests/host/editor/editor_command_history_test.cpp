// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/editor_command_history.hpp"
#include "engine/core/error.hpp"

#include <string>
#include <utility>

namespace {

class delayed_operation final : public gneiss::editor::editor_history_operation {
public:
  delayed_operation(int& value, int next) : value_(value), next_(next) {}
  gneiss::engine::error advance(bool& complete) noexcept override {
    complete = ++steps_ == 2;
    if (!complete) {
      return gneiss::engine::error::success;
    }
    if (cancelled_) {
      return gneiss::engine::error::invalid_state;
    }
    value_ = next_;
    return gneiss::engine::error::success;
  }
  void cancel() noexcept override { cancelled_ = true; }

private:
  int& value_;
  int next_;
  int steps_{};
  bool cancelled_{};
};

class delayed_command final : public gneiss::editor::editor_command {
public:
  explicit delayed_command(int& value) : value_(value) {}
  std::string_view label() const noexcept override { return "分帧命令"; }
  gneiss::engine::error undo() noexcept override { return gneiss::engine::error::invalid_state; }
  gneiss::engine::error redo() noexcept override { return gneiss::engine::error::invalid_state; }
  gneiss::engine::error begin_history_operation(
      bool undo,
      std::unique_ptr<gneiss::editor::editor_history_operation>& output) noexcept override {
    try {
      output = std::make_unique<delayed_operation>(value_, undo ? 0 : 1);
    } catch (...) {
      return gneiss::engine::error::out_of_memory;
    }
    return gneiss::engine::error::success;
  }

private:
  int& value_;
};

gneiss::editor::editor_command_history::command
set_value_command(int& value, int previous, int next, std::string merge_key = {}) {
  return {.label = "设置值",
          .undo =
              [&value, previous] {
                value = previous;
                return gneiss::engine::error::success;
              },
          .redo =
              [&value, next] {
                value = next;
                return gneiss::engine::error::success;
              },
          .merge_key = std::move(merge_key)};
}

} // namespace

int main() {
  {
    using gneiss::engine::error;
    int value = 1;
    gneiss::editor::editor_command_history pending;
    if (pending.record(std::make_unique<delayed_command>(value)) !=
            gneiss::engine::error::success ||
        pending.mark_saved() != gneiss::engine::error::success ||
        pending.undo() != gneiss::engine::error::not_ready || !pending.is_pending() ||
        pending.can_undo() || pending.can_redo() ||
        pending.clear() != gneiss::engine::error::not_ready ||
        pending.mark_saved() != gneiss::engine::error::not_ready ||
        pending.execute(set_value_command(value, 1, 9)) != gneiss::engine::error::not_ready ||
        value != 1 ||
        pending.record(set_value_command(value, 1, 9)) != gneiss::engine::error::not_ready ||
        pending.advance_pending() != gneiss::engine::error::not_ready || pending.is_dirty() ||
        pending.advance_pending() != gneiss::engine::error::success || value != 0 ||
        !pending.can_redo() || pending.redo() != gneiss::engine::error::not_ready) {
      return 21;
    }
    pending.cancel_pending();
    if (pending.advance_pending() != gneiss::engine::error::not_ready ||
        pending.advance_pending() != gneiss::engine::error::invalid_state || pending.is_pending() ||
        value != 0 || !pending.can_redo() || pending.redo() != gneiss::engine::error::not_ready ||
        pending.advance_pending() != gneiss::engine::error::not_ready ||
        pending.advance_pending() != gneiss::engine::error::success || value != 1 ||
        pending.is_dirty() || pending.advance_pending() != gneiss::engine::error::invalid_state) {
      return 22;
    }
  }
  int untouched = 0;
  gneiss::editor::editor_command_history disabled{0U};
  if (disabled.execute(set_value_command(untouched, 0, 1)) !=
          gneiss::engine::error::invalid_argument ||
      untouched != 0 || disabled.size() != 0U) {
    return 20;
  }
  gneiss::editor::editor_command_history history;
  int value = 1;
  if (history.record(set_value_command(value, 0, 1)) != gneiss::engine::error::success ||
      !history.can_undo() || history.can_redo() || !history.is_dirty() ||
      history.undo_label() != "设置值" || history.undo() != gneiss::engine::error::success ||
      value != 0 || history.redo() != gneiss::engine::error::success || value != 1) {
    return 1;
  }

  history.mark_saved();
  value = 2;
  if (history.record(set_value_command(value, 1, 2)) != gneiss::engine::error::success ||
      !history.is_dirty() || history.undo() != gneiss::engine::error::success || value != 1 ||
      history.is_dirty() || history.redo() != gneiss::engine::error::success ||
      !history.is_dirty()) {
    return 2;
  }

  if (history.undo() != gneiss::engine::error::success ||
      history.record({.label = "失败命令",
                      .undo = [] { return gneiss::engine::error::io; },
                      .redo = [] { return gneiss::engine::error::success; },
                      .merge_key = {}}) != gneiss::engine::error::success ||
      history.can_redo() || history.undo() != gneiss::engine::error::io || !history.can_undo()) {
    return 3;
  }

  gneiss::editor::editor_command_history merged;
  value = 1;
  if (merged.record(set_value_command(value, 0, 1, "transform.translation")) !=
      gneiss::engine::error::success) {
    return 4;
  }
  value = 2;
  if (merged.record(set_value_command(value, 1, 2, "transform.translation")) !=
          gneiss::engine::error::success ||
      merged.size() != 1U || merged.undo() != gneiss::engine::error::success || value != 0 ||
      merged.redo() != gneiss::engine::error::success || value != 2) {
    return 5;
  }
  merged.mark_saved();
  value = 3;
  if (merged.record(set_value_command(value, 2, 3, "transform.translation")) !=
          gneiss::engine::error::success ||
      merged.size() != 2U || merged.undo() != gneiss::engine::error::success || value != 2 ||
      merged.is_dirty()) {
    return 10;
  }

  gneiss::editor::editor_command_history bounded(2U);
  for (int next = 1; next <= 3; ++next) {
    value = next;
    if (bounded.record(set_value_command(value, next - 1, next)) !=
        gneiss::engine::error::success) {
      return 6;
    }
  }
  if (bounded.size() != 2U || bounded.undo() != gneiss::engine::error::success || value != 2 ||
      bounded.undo() != gneiss::engine::error::success || value != 1 ||
      bounded.undo() != gneiss::engine::error::not_ready) {
    return 7;
  }

  gneiss::editor::editor_command_history executed;
  value = 0;
  if (executed.execute(set_value_command(value, 0, 1)) != gneiss::engine::error::success ||
      value != 1 || executed.size() != 1U ||
      executed.execute({.label = "执行失败",
                        .undo = [] { return gneiss::engine::error::success; },
                        .redo = [] { return gneiss::engine::error::io; },
                        .merge_key = {}}) != gneiss::engine::error::io ||
      executed.size() != 1U) {
    return 8;
  }

  gneiss::editor::editor_command_history rejected(0U);
  value = 0;
  if (rejected.execute(set_value_command(value, 0, 1)) != gneiss::engine::error::invalid_argument ||
      value != 0 || rejected.size() != 0U) {
    return 11;
  }

  gneiss::editor::editor_command_history failed_redo;
  if (failed_redo.record({.label = "重做失败",
                          .undo = [] { return gneiss::engine::error::success; },
                          .redo = [] { return gneiss::engine::error::io; },
                          .merge_key = {}}) != gneiss::engine::error::success ||
      failed_redo.undo() != gneiss::engine::error::success ||
      failed_redo.redo() != gneiss::engine::error::io || !failed_redo.can_redo() ||
      failed_redo.can_undo()) {
    return 12;
  }

  if (history.clear(false) != gneiss::engine::error::success || !history.is_dirty() ||
      history.can_undo() || history.can_redo())
    return 13;
  history.clear();
  return history.can_undo() || history.can_redo() || history.is_dirty() ? 9 : 0;
}
