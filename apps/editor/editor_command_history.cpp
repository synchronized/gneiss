// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_command_history.h"

#include <new>
#include <utility>

namespace gneiss::editor {

result editor_command_history::record(command value) noexcept {
  if (!value.undo || !value.redo) {
    return result::invalid_argument;
  }
  try {
    undo_.push_back(std::move(value));
    redo_.clear();
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_command_history::undo() noexcept {
  if (undo_.empty()) {
    return result::not_ready;
  }
  try {
    redo_.reserve(redo_.size() + 1U);
    auto& value = undo_.back();
    const auto operation_result = value.undo();
    if (operation_result == result::success) {
      redo_.push_back(std::move(value));
      undo_.pop_back();
    }
    return operation_result;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_command_history::redo() noexcept {
  if (redo_.empty()) {
    return result::not_ready;
  }
  try {
    undo_.reserve(undo_.size() + 1U);
    auto& value = redo_.back();
    const auto operation_result = value.redo();
    if (operation_result == result::success) {
      undo_.push_back(std::move(value));
      redo_.pop_back();
    }
    return operation_result;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

void editor_command_history::clear() noexcept {
  undo_.clear();
  redo_.clear();
}

std::string_view editor_command_history::undo_label() const noexcept {
  return undo_.empty() ? std::string_view{} : std::string_view{undo_.back().label};
}

std::string_view editor_command_history::redo_label() const noexcept {
  return redo_.empty() ? std::string_view{} : std::string_view{redo_.back().label};
}

} // namespace gneiss::editor
