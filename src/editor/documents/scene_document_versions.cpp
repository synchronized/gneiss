// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_versions.hpp"
#include <new>
namespace gneiss::editor {
namespace {
engine::error validate(const scene_document_snapshot& snapshot) noexcept {
  auto inputs = make_scene_document_inputs(snapshot);
  if (!inputs)
    return inputs.error();
  engine::scene_instantiation_data prepared;
  engine::scene_diagnostic diagnostic;
  return engine::prepare_loaded_scene(*inputs, {}, prepared, diagnostic, {});
}
}
engine::error scene_document_versions::reset(const scene_document_snapshot& snapshot,
                                             bool saved) noexcept try {
  const auto status = validate(snapshot);
  if (status.failed())
    return status;
  version next;
  next.owner_ = std::make_shared<const std::byte>();
  // 复制外壳，避免调用方继续改写 map/URI；领域内容已经是冻结值。
  next.content_ = std::make_shared<const scene_document_snapshot>(snapshot);
  current_ = std::move(next);
  entries_.clear();
  cursor_ = 0;
  saved_.reset();
  if (saved)
    saved_ = current_.content_;
  synchronized_.reset();
  projection_status_ = engine::error::not_ready;
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error scene_document_versions::accept(const version& expected,
                                              const scene_document_snapshot& snapshot,
                                              std::string label, std::string merge_key) noexcept
    try {
  if (!current_ || expected != current_)
    return engine::error::invalid_state;
  if (snapshot.content.get().uuid != current_.content().content.get().uuid)
    return engine::error::invalid_argument;
  const auto status = validate(snapshot);
  if (status.failed())
    return status;
  version next;
  next.owner_ = current_.owner_;
  next.content_ = std::make_shared<const scene_document_snapshot>(snapshot);
  // 先完成所有可能分配的操作；失败不能丢失 redo 或改变当前版本。
  auto entries = entries_;
  const bool merge = !merge_key.empty() && cursor_ == entries.size() && cursor_ != 0 &&
                     entries.back().merge_key == merge_key && saved_.lock() != current_.content_;
  entries.resize(cursor_);
  if (merge) {
    entries.back().after = next;
    entries.back().label = std::move(label);
  } else if (capacity_ != 0) {
    entries.push_back({current_, next, std::move(label), std::move(merge_key)});
  }
  if (entries.size() > capacity_)
    entries.erase(entries.begin(), entries.begin() + (entries.size() - capacity_));
  entries_ = std::move(entries);
  cursor_ = entries_.size();
  current_ = std::move(next);
  changed();
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
void scene_document_versions::changed() noexcept {
  // 接受新版本后旧投影不再代表当前内容，即使后来撤销回旧版本也必须明确同步。
  synchronized_.reset();
  projection_status_ = engine::error::not_ready;
}
engine::error scene_document_versions::undo() noexcept {
  if (!can_undo())
    return engine::error::invalid_state;
  current_ = entries_[--cursor_].before;
  changed();
  return engine::error::success;
}
engine::error scene_document_versions::redo() noexcept {
  if (!can_redo())
    return engine::error::invalid_state;
  current_ = entries_[cursor_++].after;
  changed();
  return engine::error::success;
}
void scene_document_versions::clear_history() noexcept {
  entries_.clear();
  cursor_ = 0;
}
std::string_view scene_document_versions::undo_label() const noexcept {
  return can_undo() ? std::string_view(entries_[cursor_ - 1].label) : std::string_view{};
}
std::string_view scene_document_versions::redo_label() const noexcept {
  return can_redo() ? std::string_view(entries_[cursor_].label) : std::string_view{};
}
bool scene_document_versions::is_dirty() const noexcept {
  return current_ && saved_.lock() != current_.content_;
}
engine::error scene_document_versions::mark_saved(const version& saved) noexcept {
  if (!saved || saved.owner_ != current_.owner_)
    return engine::error::invalid_state;
  saved_ = saved.content_;
  return engine::error::success;
}
engine::error scene_document_versions::report_projection(const version& projected,
                                                         engine::error status) noexcept {
  if (!current_ || projected != current_)
    return engine::error::invalid_state;
  projection_status_ = status;
  synchronized_.reset();
  if (status.ok())
    synchronized_ = current_.content_;
  return engine::error::success;
}
bool scene_document_versions::is_synchronized() const noexcept {
  return current_ && synchronized_.lock() == current_.content_;
}
}
