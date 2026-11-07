// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_edit.hpp"
#include "documents/scene_document.hpp"
#include "runtime/application.hpp"
namespace gneiss::editor {
engine::result<scene_document_snapshot>
capture_scene_document(runtime::application& app, const scene_document& document) noexcept try {
  if (!document.is_open() || !app.is_owner_thread() || !app.accepts_requests())
    return engine::error::invalid_state;
  return document.capture_content();
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error restore_scene_document(runtime::application& app, scene_document& document,
                                     const scene_document_snapshot& snapshot) noexcept try {
  if (!app.is_owner_thread() || !app.accepts_requests() || !document.is_open() ||
      document.uri() != snapshot.uri)
    return engine::error::invalid_state;
  return document.accept_content(snapshot);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error scene_document_edit::finish(engine::error status) noexcept {
  before_.reset();
  after_.reset();
  selection_ = {};
  return status_ = status;
}
void scene_document_edit::cancel() noexcept {
  if (status_ == engine::error::not_ready)
    (void)finish(engine::error::cancelled);
}
engine::error scene_document_edit::begin(runtime::application& app, scene_document& document,
                                         std::span<const author_document_change> changes,
                                         std::string label,
                                         std::function<void(bool)> selection) noexcept try {
  using engine::error;
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
} catch (const std::bad_alloc&) {
  return finish(engine::error::out_of_memory);
} catch (...) {
  return finish(engine::error::internal);
}
engine::error scene_document_edit::advance(runtime::application& app, scene_document& document,
                                           editor_command_history& history) noexcept try {
  using engine::error;
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
  const auto apply = [&app, &document, selection = selection_](
                         const std::shared_ptr<scene_document_snapshot>& snapshot, bool undo) {
    auto result = restore_scene_document(app, document, *snapshot);
    if (result.ok()) {
      document.mark_dirty();
      if (selection)
        selection(undo);
    }
    return result;
  };
  const bool was_dirty = document.is_dirty();
  status = history.execute({.label = label_,
                            .undo = [apply, before = before_] { return apply(before, true); },
                            .redo = [apply, after = after_] { return apply(after, false); },
                            .merge_key = {}});
  if (status.failed() && !was_dirty)
    document.clear_dirty();
  return finish(status);
} catch (const std::bad_alloc&) {
  return finish(engine::error::out_of_memory);
} catch (...) {
  return finish(engine::error::internal);
}
}
