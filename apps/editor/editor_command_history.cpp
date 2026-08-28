// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_command_history.h"

#include <limits>
#include <new>
#include <utility>

namespace gneiss::editor {

functional_editor_command::functional_editor_command(desc value) noexcept
    : label_(std::move(value.label)), undo_(std::move(value.undo)), redo_(std::move(value.redo)),
      merge_key_(std::move(value.merge_key)) {}

result functional_editor_command::undo() noexcept {
  try {
    return undo_ ? undo_() : result::invalid_state;
  } catch (...) {
    return result::internal;
  }
}

result functional_editor_command::redo() noexcept {
  try {
    return redo_ ? redo_() : result::invalid_state;
  } catch (...) {
    return result::internal;
  }
}

bool functional_editor_command::merge_with(const editor_command& next) noexcept {
  const auto* functional = dynamic_cast<const functional_editor_command*>(&next);
  if (functional == nullptr || merge_key_.empty() || merge_key_ != functional->merge_key_) {
    return false;
  }
  try {
    label_ = functional->label_;
    redo_ = functional->redo_;
    return true;
  } catch (...) {
    return false;
  }
}

editor_command_history::editor_command_history(std::size_t capacity) noexcept
    : capacity_(capacity) {}

std::uint64_t editor_command_history::allocate_state() noexcept {
  const auto value = next_state_;
  if (next_state_ != std::numeric_limits<std::uint64_t>::max()) {
    ++next_state_;
  }
  return value;
}

result editor_command_history::record_impl(std::unique_ptr<editor_command>& value) noexcept {
  if (value == nullptr || capacity_ == 0U) {
    return result::invalid_argument;
  }
  try {
    entries_.reserve(entries_.size() + 1U);
    if (cursor_ < entries_.size()) {
      entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());
    }
    if (cursor_ != 0U && current_state_ != saved_state_ &&
        entries_[cursor_ - 1U].value->merge_with(*value)) {
      entries_[cursor_ - 1U].after_state = allocate_state();
      current_state_ = entries_[cursor_ - 1U].after_state;
      return result::success;
    }
    const auto after_state = allocate_state();
    entries_.push_back(
        {.value = std::move(value), .before_state = current_state_, .after_state = after_state});
    ++cursor_;
    current_state_ = after_state;
    if (entries_.size() > capacity_) {
      entries_.erase(entries_.begin());
      --cursor_;
    }
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_command_history::record(std::unique_ptr<editor_command> value) noexcept {
  return record_impl(value);
}

result editor_command_history::record(command value) noexcept {
  if (!value.undo || !value.redo) {
    return result::invalid_argument;
  }
  try {
    std::unique_ptr<editor_command> command_value =
        std::make_unique<functional_editor_command>(std::move(value));
    return record_impl(command_value);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_command_history::execute(std::unique_ptr<editor_command> value) noexcept {
  if (value == nullptr) {
    return result::invalid_argument;
  }
  const auto operation_result = value->redo();
  if (operation_result != result::success) {
    return operation_result;
  }
  const auto record_result = record_impl(value);
  if (record_result != result::success && value != nullptr) {
    (void)value->undo();
  }
  return record_result;
}

result editor_command_history::execute(command value) noexcept {
  if (!value.undo || !value.redo) {
    return result::invalid_argument;
  }
  try {
    return execute(std::make_unique<functional_editor_command>(std::move(value)));
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_command_history::undo() noexcept {
  if (!can_undo()) {
    return result::not_ready;
  }
  auto& target = entries_[cursor_ - 1U];
  const auto operation_result = target.value->undo();
  if (operation_result == result::success) {
    --cursor_;
    current_state_ = target.before_state;
  }
  return operation_result;
}

result editor_command_history::redo() noexcept {
  if (!can_redo()) {
    return result::not_ready;
  }
  auto& target = entries_[cursor_];
  const auto operation_result = target.value->redo();
  if (operation_result == result::success) {
    ++cursor_;
    current_state_ = target.after_state;
  }
  return operation_result;
}

void editor_command_history::clear() noexcept {
  entries_.clear();
  cursor_ = 0U;
  current_state_ = allocate_state();
  saved_state_ = current_state_;
}

std::string_view editor_command_history::undo_label() const noexcept {
  return can_undo() ? entries_[cursor_ - 1U].value->label() : std::string_view{};
}

std::string_view editor_command_history::redo_label() const noexcept {
  return can_redo() ? entries_[cursor_].value->label() : std::string_view{};
}

} // namespace gneiss::editor
