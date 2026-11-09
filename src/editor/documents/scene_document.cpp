// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document.hpp"
#include <new>
namespace gneiss::editor {
namespace {
engine::error validate(const scene_document_snapshot& snapshot) {
  auto inputs = make_scene_document_inputs(snapshot);
  if (!inputs)
    return inputs.error();
  engine::scene_instantiation_data prepared;
  engine::scene_diagnostic diagnostic;
  return engine::prepare_loaded_scene(*inputs, {}, prepared, diagnostic, {});
}
}
engine::result<std::unique_ptr<scene_document>> scene_document::make_candidate() const {
  if (!current_)
    return engine::error::invalid_state;
  auto candidate = std::make_unique<scene_document>();
  candidate->current_ = current_;
  candidate->saved_ = saved_;
  return candidate;
}
engine::error scene_document::reset(const scene_document_snapshot& snapshot, bool saved) {
  const auto status = validate(snapshot);
  if (status.failed())
    return status;
  version next;
  next.owner_ = std::make_shared<const std::byte>();
  next.state_ = std::make_shared<const std::byte>();
  // 复制外壳，避免调用方继续改写 map/URI；领域内容已经是冻结值。
  next.content_ = std::make_shared<const scene_document_snapshot>(snapshot);
  current_ = std::move(next);
  saved_.reset();
  if (saved)
    saved_ = current_.state_;
  return engine::error::success;
}
engine::error scene_document::accept(const version& expected,
                                     const scene_document_snapshot& snapshot) {
  if (!current_ || expected != current_)
    return engine::error::invalid_state;
  if (snapshot.content.get().uuid != current_.content().content.get().uuid)
    return engine::error::invalid_argument;
  const auto status = validate(snapshot);
  if (status.failed())
    return status;
  version next;
  next.owner_ = current_.owner_;
  next.state_ = std::make_shared<const std::byte>();
  next.content_ = std::make_shared<const scene_document_snapshot>(snapshot);
  current_ = std::move(next);
  return engine::error::success;
}
engine::error scene_document::checkout(const version& expected, const version& target) {
  if (!current_ || expected != current_ || !target || target.owner_ != current_.owner_)
    return engine::error::invalid_state;
  current_ = target;
  return engine::error::success;
}
engine::error scene_document::checkout(const revision& expected, const revision& target,
                                       const scene_document_snapshot& snapshot) {
  if (!current_ || current_.stamp() != expected || !target.state || target.owner != current_.owner_)
    return engine::error::invalid_state;
  if (snapshot.uri != current_.content().uri ||
      snapshot.content.get().uuid != current_.content().content.get().uuid)
    return engine::error::invalid_argument;
  auto status = validate(snapshot);
  if (status.failed())
    return status;
  version next;
  next.owner_ = target.owner;
  next.state_ = target.state;
  next.content_ = std::make_shared<const scene_document_snapshot>(snapshot);
  current_ = std::move(next);
  return engine::error::success;
}
bool scene_document::is_dirty() const noexcept {
  return current_ && saved_.lock() != current_.state_;
}
engine::error scene_document::mark_saved(const version& saved) noexcept {
  if (!saved || saved.owner_ != current_.owner_)
    return engine::error::invalid_state;
  saved_ = saved.state_;
  return engine::error::success;
}
}
