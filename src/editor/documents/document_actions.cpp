// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/document_actions.hpp"
#include "engine/core/error.hpp"
#include <utility>
namespace gneiss::editor {
document_action document_actions::request(document_action action, bool dirty) noexcept {
  if (action == document_action::none)
    return action;
  if (dirty) {
    defer(action);
    return document_action::none;
  }
  return action;
}
void document_actions::defer(document_action action) noexcept { pending_ = action; }
document_action document_actions::saved(gneiss::engine::error operation) noexcept {
  return operation == gneiss::engine::error::success ? discard() : document_action::none;
}
document_action document_actions::discard() noexcept {
  return std::exchange(pending_, document_action::none);
}
void document_actions::cancel() noexcept { pending_ = document_action::none; }
}
