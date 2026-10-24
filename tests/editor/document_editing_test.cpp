// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/document_editing.hpp"
#include "documents/editor_scene_loader.hpp"
#include "engine/core/result.hpp"
#include "runtime/application.hpp"

namespace {

class pending_operation final : public gneiss::editor::editor_history_operation {
public:
  explicit pending_operation(bool& cancelled) : cancelled_(cancelled) {}
  gneiss::engine::core::result advance(bool& complete) noexcept override {
    complete = ++steps_ == 2;
    return complete && cancelled_ ? gneiss::engine::core::result::invalid_state
                                  : gneiss::engine::core::result::success;
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
  gneiss::engine::core::result undo() noexcept override {
    return gneiss::engine::core::result::invalid_state;
  }
  gneiss::engine::core::result redo() noexcept override {
    return gneiss::engine::core::result::invalid_state;
  }
  gneiss::engine::core::result begin_history_operation(
      bool, std::unique_ptr<gneiss::editor::editor_history_operation>& output) noexcept override {
    try {
      output = std::make_unique<pending_operation>(cancelled_);
    } catch (...) {
      return gneiss::engine::core::result::out_of_memory;
    }
    return gneiss::engine::core::result::success;
  }

private:
  gneiss::editor::scene_document& document_;
  bool& cancelled_;
  bool& released_;
};
}
int main() try {
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::runtime::application::create({}, application) !=
      gneiss::engine::core::result::success)
    return 1;
  bool cancelled = false;
  bool released_while_open = false;
  gneiss::editor::document_editing editing;
  if (editing.create_empty(*application, application->world()).failed())
    return 2;
  const auto scene = editing.document().scene_handle();
  if (editing.history()
          .record(
              std::make_unique<pending_command>(editing.document(), cancelled, released_while_open))
          .failed() ||
      editing.history().undo() != gneiss::engine::core::result::not_ready || !editing.active())
    return 3;
  // 拒绝关闭不能提前释放历史或场景；取消后仍需排空异步操作。
  if (editing.close() != gneiss::engine::core::result::not_ready ||
      editing.document().scene_handle() != scene || editing.history().size() != 1 ||
      released_while_open)
    return 4;
  gneiss::editor::editor_scene_loader loader;
  gneiss::editor::editor_camera camera;
  auto world = application->world();
  bool changed = true;
  if (editing.create_empty(*application, world) != gneiss::engine::core::result::not_ready ||
      loader.request(*application, editing, "asset://scene.json") !=
          gneiss::engine::core::result::not_ready ||
      loader.active() || loader.advance(*application, editing, world, camera, changed).failed() ||
      changed || editing.document().scene_handle() != scene)
    return 8;
  editing.cancel(*application);
  editing.advance_cleanup(*application);
  if (!cancelled || !editing.active() || editing.close() != gneiss::engine::core::result::not_ready)
    return 5;
  editing.advance_cleanup(*application);
  if (editing.active() || editing.close() != gneiss::engine::core::result::success ||
      !released_while_open || editing.document().is_open() || editing.history().size() != 0 ||
      editing.close() != gneiss::engine::core::result::success)
    return 6;
  // 新建失败不能丢失可撤销历史；成功后先释放旧命令，再替换文档。
  if (editing.create_empty(*application, world).failed())
    return 9;
  const auto original = editing.document().scene_handle();
  released_while_open = false;
  if (editing.history()
          .record(
              std::make_unique<pending_command>(editing.document(), cancelled, released_while_open))
          .failed() ||
      editing.create_empty(*application, GNEISS_NULL_WORLD) !=
          gneiss::engine::core::result::invalid_argument ||
      editing.document().scene_handle() != original || !editing.history().can_undo() ||
      released_while_open)
    return 10;
  if (editing.create_empty(*application, world).failed() || !released_while_open ||
      editing.document().scene_handle() == original || editing.history().size() != 0 ||
      !editing.document().is_dirty() || editing.close().failed())
    return 11;
  return 0;
} catch (...) {
  return 7;
}
