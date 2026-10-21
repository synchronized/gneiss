// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/document_editing.hpp"
namespace gneiss::editor {
bool document_editing::active() const noexcept {
  return assets_.active() || prefabs_.active() || history_.is_pending();
}
void document_editing::cancel(runtime::application& application) {
  prefabs_.cancel();
  history_.cancel_pending();
  assets_.cancel(application);
}
void document_editing::advance_cleanup(runtime::application& application) {
  prefabs_.advance();
  if (history_.is_pending()) {
    (void)history_.advance_pending();
  }
  assets_.advance(application, document_, history_);
}
result document_editing::close() noexcept {
  if (active())
    return result::not_ready;
  const auto cleared = history_.clear();
  if (cleared.failed())
    return cleared;
  prefabs_.dismiss();
  assets_.dismiss();
  document_.close();
  return result::success;
}
} // namespace gneiss::editor
