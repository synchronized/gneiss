// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_edit.hpp"
#include "documents/scene_edit_session.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
namespace gneiss::editor {
namespace {
struct document_selection {
  gneiss::guid object, source;
  static document_selection capture(scene_edit_session& document) {
    if (auto node = document.selected_node())
      return {node->uuid, {}};
    if (auto node = document.selected_prefab_node())
      return {node->instance_uuid, node->source_node_uuid};
    return {};
  }
  void restore(scene_edit_session& document) const {
    if (source) {
      auto node = document.find_prefab_source(object, source);
      (void)document.select(node ? node->node : entity_id{});
    } else {
      auto node = document.find_node(object);
      (void)document.select(node ? node->node : entity_id{});
    }
  }
};
class document_patch_command final : public editor_command {
public:
  document_patch_command(scene_edit_session& document, scene_edit_session::version before,
                         scene_edit_session::version after, std::string label, std::string key)
      : document_(document), before_(before.stamp()), after_(after.stamp()),
        label_(std::move(label)), key_(std::move(key)),
        selection_before_(document_selection::capture(document)),
        selection_after_(selection_before_) {}
  std::string_view label() const noexcept override { return label_; }
  gneiss::error prepare(const scene_document_snapshot& before,
                        const scene_document_snapshot& after) {
    auto patch = make_document_patch(before, after);
    if (!patch)
      return patch.error();
    {
      patches_.push_back(std::move(*patch));
      return gneiss::error::success;
    }
  }
  gneiss::error complete() noexcept {
    auto after = document_.current_version();
    if (patches_.empty())
      return gneiss::error::invalid_state;
    after_ = after.stamp();
    selection_after_ = document_selection::capture(document_);
    return gneiss::error::success;
  }
  gneiss::error undo() override {
    auto status = document_.restore_changes(after_, before_, patches_, true);
    if (status.ok())
      selection_before_.restore(document_);
    return status;
  }
  gneiss::error redo() override {
    auto status = document_.restore_changes(before_, after_, patches_, false);
    if (status.ok())
      selection_after_.restore(document_);
    return status;
  }
  bool merge_with(const editor_command& next) override {
    auto* other = dynamic_cast<const document_patch_command*>(&next);
    if (!other || &other->document_ != &document_ || key_.empty() || key_ != other->key_ ||
        after_ != other->before_ || patches_.size() != 1 || other->patches_.size() != 1)
      return false;
    auto merged = merge_document_patches(patches_.front(), other->patches_.front());
    if (!merged)
      return false;
    patches_.front() = std::move(*merged);
    after_ = other->after_;
    selection_after_ = other->selection_after_;
    return true;
  }

private:
  scene_edit_session& document_;
  scene_document::revision before_, after_;
  std::vector<document_patch> patches_;
  std::string label_, key_;
  document_selection selection_before_, selection_after_;
};
}
gneiss::error execute_scene_edit(editor_command_history& history, scene_edit_session& document,
                                 std::string label, const std::function<gneiss::error()>& edit,
                                 std::string key) {
  auto before = document.current_version();
  if (!before || !edit)
    return gneiss::error::invalid_state;
  auto status = history.prepare_record();
  if (status.failed())
    return status;
  auto command = std::make_unique<document_patch_command>(document, before, before,
                                                          std::move(label), std::move(key));
  if (document.edit_base_)
    return gneiss::error::invalid_state;
  auto candidate = document.document_->make_candidate();
  if (!candidate)
    return candidate.error();
  const auto selection = document_selection::capture(document);
  auto authority = std::move(document.document_);
  document.edit_base_ = authority.get();
  document.document_ = std::move(*candidate);
  // 候选运行投影属于编辑预览；失败不发布文档，恢复预览基线。
  const auto discard = [&](scene_edit_session* session) {
    if (session->document_ && session->document_->current() != before)
      (void)session->restore_version(session->document_->current(), before);
    session->document_ = std::move(authority);
    session->edit_base_ = nullptr;
    selection.restore(*session);
  };
  std::unique_ptr<scene_edit_session, decltype(discard)> scope(&document, discard);
  {
    status = edit();
  }
  if (status.failed())
    return status;
  if (!document.document_)
    return gneiss::error::invalid_state;
  const auto after = document.document_->current();
  if (before == after) {
    document.document_ = std::move(authority);
    document.edit_base_ = nullptr;
    (void)scope.release();
    return gneiss::error::success;
  }
  status = command->prepare(before.content(), after.content());
  if (status.failed())
    return status;
  // 最终候选已校验，历史差异与容量已准备；发布只交换不可变版本，不再分配。
  status = authority->checkout(before, after);
  if (status.failed())
    return status;
  document.document_ = std::move(authority);
  document.edit_base_ = nullptr;
  (void)scope.release();
  (void)document.validate_selection();
  status = command->complete();
  if (status.ok())
    status = history.record(std::move(command));
  if (status.failed())
    (void)document.restore_version(document.current_version(), before);
  return status;
}

gneiss::result<scene_document_snapshot> capture_scene_document(runtime::application& app,
                                                               const scene_edit_session& document) {
  if (!document.is_open() || !app.is_owner_thread() || !app.accepts_requests())
    return gneiss::error::invalid_state;
  return document.capture_content();
}

gneiss::error restore_scene_document(runtime::application& app, scene_edit_session& document,
                                     const scene_document_snapshot& snapshot) {
  if (!app.is_owner_thread() || !app.accepts_requests() || !document.is_open() ||
      document.uri() != snapshot.uri)
    return gneiss::error::invalid_state;
  return document.accept_content(snapshot);
}
gneiss::error scene_document_edit::finish(gneiss::error status) {
  before_.reset();
  after_.reset();
  selection_ = {};
  return status_ = status;
}
void scene_document_edit::cancel() noexcept {
  if (status_ == gneiss::error::not_ready)
    (void)finish(gneiss::error::cancelled);
}
gneiss::error scene_document_edit::begin(runtime::application& app, scene_edit_session& document,
                                         std::span<const author_document_change> changes,
                                         std::string label,
                                         std::function<void(bool)> selection) try {
  using gneiss::error;
  if (status_ == error::not_ready)
    return error::not_ready;
  (void)finish(error::invalid_state);
  auto captured = capture_scene_document(app, document);
  if (!captured)
    return finish(captured.error());
  before_ = std::make_shared<scene_document_snapshot>(std::move(*captured));
  auto prepared = prepare_scene_document(*before_, changes);
  if (!prepared)
    return finish(prepared.error());
  after_ = std::make_shared<scene_document_snapshot>(std::move(prepared->snapshot));
  scene_ = document.scene_handle();

  application_ = &app;
  for (const auto& source : prepared->missing) {
    auto loading = app.assets().load(source);
    if (!loading)
      return finish(loading.error());
    after_->assets.insert_or_assign(source.uri, loading->resource());
  }
  document_ = &document;
  label_ = std::move(label);
  selection_ = std::move(selection);
  status_ = error::not_ready;
  return error::success;
} catch (...) {
  // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
  (void)finish(gneiss::error::internal);
  throw;
}
gneiss::error scene_document_edit::advance(runtime::application& app, scene_edit_session& document,
                                           editor_command_history& history) try {
  using gneiss::error;
  if (status_ != error::not_ready)
    return status_;
  if (&app != application_ || &document != document_ || !app.is_owner_thread() ||
      !app.accepts_requests() || !document.is_open() || document.scene_handle() != scene_)
    return finish(error::invalid_state);
  if (history.is_pending())
    return finish(error::invalid_state);
  // 比较持久化值树，不通过 JSON 编码判断等待期间的修改。
  auto current = capture_scene_document(app, document);
  if (!current)
    return finish(current.error());
  if (current->content != before_->content || current->prefabs != before_->prefabs)
    return finish(error::invalid_state);
  auto status = error{error::success};
  const bool was_dirty = document.is_dirty();
  status = execute_scene_edit(history, document, label_, [&] {
    auto result = restore_scene_document(app, document, *after_);
    if (result.ok() && selection_)
      selection_(false);
    return result;
  });
  if (status.failed() && !was_dirty)
    document.clear_dirty();
  return finish(status);
} catch (...) {
  // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
  (void)finish(gneiss::error::internal);
  throw;
}
}
