// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/document_editing.hpp"
#include "runtime/application.hpp"

namespace {
using gneiss::result;
class pending_operation final : public gneiss::editor::editor_history_operation {
public:
  explicit pending_operation(bool& cancelled) : cancelled_(cancelled) {}
  result advance(bool& complete) noexcept override {
    complete = ++steps_ == 2;
    return complete && cancelled_ ? result::invalid_state : result::success;
  }
  void cancel() noexcept override { cancelled_ = true; }

private:
  bool& cancelled_;
  int steps_{};
};
class pending_command final : public gneiss::editor::editor_command {
public:
  pending_command(gneiss::editor::scene_document& document, bool& cancelled, bool& released)
      : document_(document), cancelled_(cancelled), released_(released) {}
  ~pending_command() override { released_ = document_.is_open(); }
  std::string_view label() const noexcept override { return "关闭顺序验证"; }
  result undo() noexcept override { return result::invalid_state; }
  result redo() noexcept override { return result::invalid_state; }
  result begin_history_operation(
      bool, std::unique_ptr<gneiss::editor::editor_history_operation>& output) noexcept override {
    try {
      output = std::make_unique<pending_operation>(cancelled_);
    } catch (...) {
      return result::out_of_memory;
    }
    return result::success;
  }

private:
  gneiss::editor::scene_document& document_;
  bool& cancelled_;
  bool& released_;
};
}
int main() try {
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::runtime::application::create({}, application) != GNEISS_SUCCESS)
    return 1;
  bool cancelled = false;
  bool released_while_open = false;
  gneiss::editor::document_editing editing;
  if (editing.document().create_empty(*application, application->world()).failed())
    return 2;
  const auto scene = editing.document().scene_handle();
  if (editing.history()
          .record(
              std::make_unique<pending_command>(editing.document(), cancelled, released_while_open))
          .failed() ||
      editing.history().undo() != result::not_ready || !editing.active())
    return 3;
  // 拒绝关闭不能提前释放历史或场景；取消后仍需排空异步操作。
  if (editing.close() != result::not_ready || editing.document().scene_handle() != scene ||
      editing.history().size() != 1 || released_while_open)
    return 4;
  editing.cancel(*application);
  editing.advance_cleanup(*application);
  if (!cancelled || !editing.active() || editing.close() != result::not_ready)
    return 5;
  editing.advance_cleanup(*application);
  if (editing.active() || editing.close() != result::success || !released_while_open ||
      editing.document().is_open() || editing.history().size() != 0 ||
      editing.close() != result::success)
    return 6;
  return 0;
} catch (...) {
  return 7;
}
