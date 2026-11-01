// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/string_name.hpp"

#include <map>
#include <memory>

#include <mutex>
#include <string_view>
#include <utility>

namespace gneiss::engine {

namespace detail {

struct string_name_entry {
  explicit string_name_entry(std::string_view value) : text(value) {}

  std::string text;
};

} // namespace detail

namespace {

class string_name_table {
public:
  [[nodiscard]] const detail::string_name_entry* intern(std::string_view text) {
    if (text.empty()) {
      return nullptr;
    }

    std::lock_guard lock(mutex_);
    const auto existing = entries_by_text_.find(text);
    if (existing != entries_by_text_.end()) {
      return existing->second.get();
    }

    auto entry = std::make_unique<detail::string_name_entry>(text);
    const detail::string_name_entry* stored = entry.get();
    entries_by_text_.emplace(stored->text, std::move(entry));
    return stored;
  }

  [[nodiscard]] const detail::string_name_entry* find_entry(std::string_view text) const {
    std::lock_guard lock(mutex_);
    const auto position = entries_by_text_.find(text);
    if (position == entries_by_text_.end()) {
      return nullptr;
    }

    return position->second.get();
  }

private:
  mutable std::mutex mutex_;
  std::map<std::string, std::unique_ptr<detail::string_name_entry>, std::less<>> entries_by_text_;
};

[[nodiscard]] string_name_table& get_string_name_table() {
  static string_name_table table;
  return table;
}

} // namespace

string_name::string_name(const char* name)
    : string_name(name != nullptr ? std::string_view{name} : std::string_view{}) {}

string_name::string_name(std::string_view name) : entry_(get_string_name_table().intern(name)) {}

string_name::string_name(const std::string& name) : string_name(std::string_view{name}) {}

std::optional<string_name> string_name::find(std::string_view name) {
  const detail::string_name_entry* entry = get_string_name_table().find_entry(name);
  if (entry == nullptr) {
    return std::nullopt;
  }

  return string_name{entry};
}

std::string_view string_name::view() const noexcept {
  return entry_ != nullptr ? std::string_view{entry_->text} : std::string_view{};
}

std::string string_name::to_string() const { return std::string{view()}; }

bool operator==(string_name lhs, std::string_view rhs) { return lhs.view() == rhs; }

bool operator==(std::string_view lhs, string_name rhs) { return lhs == rhs.view(); }

bool operator!=(string_name lhs, std::string_view rhs) { return !(lhs == rhs); }

bool operator!=(std::string_view lhs, string_name rhs) { return !(lhs == rhs); }

} // namespace gneiss::engine
