// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "console_model.h"

#include <limits>
#include <utility>

namespace gneiss::editor {

console_model::console_model(std::size_t capacity) noexcept : capacity_(capacity) {}

std::uint64_t console_model::begin_session() noexcept {
  if (current_session_id_ == std::numeric_limits<std::uint64_t>::max()) {
    clear();
    current_session_id_ = 1U;
  } else {
    ++current_session_id_;
  }
  return current_session_id_;
}

result console_model::append_event(std::uint64_t session_id,
                                   app::runtime_log_record event) noexcept {
  if (capacity_ == 0U || session_id == 0U) {
    return result::invalid_argument;
  }
  try {
    make_room();
    console_entry entry;
    entry.kind = console_entry_kind::structured;
    entry.session_id = session_id;
    entry.event = std::move(event);
    entries_.push_back(std::move(entry));
    return result::success;
  } catch (...) {
    return result::out_of_memory;
  }
}

result console_model::append_raw(std::uint64_t session_id, std::string_view text,
                                 bool was_truncated) noexcept {
  if (capacity_ == 0U || session_id == 0U) {
    return result::invalid_argument;
  }
  try {
    make_room();
    console_entry entry;
    entry.kind = console_entry_kind::raw;
    entry.session_id = session_id;
    entry.raw_text = text;
    entry.was_truncated = was_truncated;
    entries_.push_back(std::move(entry));
    return result::success;
  } catch (...) {
    return result::out_of_memory;
  }
}

void console_model::clear() noexcept {
  entries_.clear();
  dropped_count_ = 0U;
}

std::uint64_t console_model::current_session_id() const noexcept { return current_session_id_; }

std::uint64_t console_model::dropped_count() const noexcept { return dropped_count_; }

const std::deque<console_entry>& console_model::entries() const noexcept { return entries_; }

void console_model::make_room() noexcept {
  if (entries_.size() < capacity_) {
    return;
  }
  entries_.pop_front();
  ++dropped_count_;
}

} // namespace gneiss::editor
