// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/document_editing.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/function/scene/world.hpp>
namespace gneiss::editor {
bool document_editing::active() const noexcept {
  return assets_.active() || prefabs_.active() || history_.is_pending();
}
void document_editing::cancel(app::application& application) {
  prefabs_.cancel();
  history_.cancel_pending();
  assets_.cancel(application);
}
void document_editing::advance_cleanup(app::application& application) {
  prefabs_.advance();
  if (history_.is_pending()) {
    (void)history_.advance_pending();
  }
  assets_.advance(application, document_, history_);
}
gneiss::error document_editing::create_empty(app::application& application, gneiss::world* world) {
  if (active())
    return gneiss::error::not_ready;
  scene_edit_session candidate;
  const auto prepared = candidate.create_empty(application, world);
  if (prepared.failed())
    return prepared;
  const auto closed = close();
  if (closed.failed())
    return closed;
  document_ = std::move(candidate);
  return gneiss::error::success;
}
gneiss::error document_editing::close() noexcept {
  if (active())
    return gneiss::error::not_ready;
  const auto cleared = history_.clear();
  if (cleared.failed())
    return cleared;
  prefabs_.dismiss();
  assets_.dismiss();
  document_.close();
  return gneiss::error::success;
}
} // namespace gneiss::editor
