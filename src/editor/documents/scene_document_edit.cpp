// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_edit.hpp"
#include "documents/scene_document.hpp"
#include "runtime/application.hpp"
namespace gneiss::editor {
namespace {
struct document_selection {
  engine::guid object, source;
  static document_selection capture(scene_document& document) noexcept {
    if (auto node = document.selected_node())
      return {node->uuid, {}};
    if (auto node = document.selected_prefab_node())
      return {node->instance_uuid, node->source_node_uuid};
    return {};
  }
  void restore(scene_document& document) const noexcept {
    if (source) {
      auto node = document.find_prefab_source(object, source);
      (void)document.select(node ? node->node : scene_node_id{});
    } else {
      auto node = document.find_node(object);
      (void)document.select(node ? node->node : scene_node_id{});
    }
  }
};
class document_patch_command final : public editor_command {
public:
  document_patch_command(scene_document& document, scene_document::version before,
                         scene_document::version after, std::string label, std::string key)
      : document_(document), before_(before.stamp()), after_(after.stamp()),
        pending_before_(std::move(before)), label_(std::move(label)), key_(std::move(key)),
        selection_before_(document_selection::capture(document)),
        selection_after_(selection_before_) {}
  std::string_view label() const noexcept override { return label_; }
  engine::error complete() noexcept {
    auto after = document_.current_version();
    auto patch = make_document_patch(pending_before_.content(), after.content());
    if (!patch)
      return patch.error();
    try {
      patches_.push_back(std::move(*patch));
    } catch (...) {
      return engine::error::out_of_memory;
    }
    after_ = after.stamp();
    pending_before_ = {};
    selection_after_ = document_selection::capture(document_);
    return engine::error::success;
  }
  engine::error undo() noexcept override {
    auto status = document_.restore_changes(after_, before_, patches_, true);
    if (status.ok())
      selection_before_.restore(document_);
    return status;
  }
  engine::error redo() noexcept override {
    auto status = document_.restore_changes(before_, after_, patches_, false);
    if (status.ok())
      selection_after_.restore(document_);
    return status;
  }
  bool merge_with(const editor_command& next) noexcept override {
    auto* other = dynamic_cast<const document_patch_command*>(&next);
    if (!other || &other->document_ != &document_ || key_.empty() || key_ != other->key_ ||
        after_ != other->before_)
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
  scene_document& document_;
  scene_document_versions::revision before_, after_;
  scene_document::version pending_before_;
  std::vector<document_patch> patches_;
  std::string label_, key_;
  document_selection selection_before_, selection_after_;
};
}
engine::error record_scene_edit(editor_command_history& history, scene_document& document,
                                const scene_document_versions::version& before, std::string label,
                                std::string key) noexcept try {
  auto after = document.current_version();
  if (!before || !after)
    return engine::error::invalid_state;
  if (before == after)
    return engine::error::success;
  auto command = std::make_unique<document_patch_command>(document, before, after, std::move(label),
                                                          std::move(key));
  auto status = command->complete();
  if (status.failed())
    return status;
  return history.record(std::move(command));
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error execute_scene_edit(editor_command_history& history, scene_document& document,
                                 std::string label, const std::function<engine::error()>& edit,
                                 std::string key) noexcept try {
  auto before = document.current_version();
  if (!before || !edit)
    return engine::error::invalid_state;
  auto status = history.prepare_record();
  if (status.failed())
    return status;
  auto command = std::make_unique<document_patch_command>(document, before, before,
                                                          std::move(label), std::move(key));
  try {
    status = edit();
  } catch (const std::bad_alloc&) {
    status = engine::error::out_of_memory;
  } catch (...) {
    status = engine::error::internal;
  }
  if (status.failed()) {
    if (document.current_version() != before)
      (void)document.restore_version(document.current_version(), before);
    return status;
  }
  if (before == document.current_version())
    return engine::error::success;
  (void)document.validate_selection();
  status = command->complete();
  if (status.ok())
    status = history.record(std::move(command));
  if (status.failed())
    (void)document.restore_version(document.current_version(), before);
  return status;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

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
} catch (const std::bad_alloc&) {
  return finish(engine::error::out_of_memory);
} catch (...) {
  return finish(engine::error::internal);
}
}
