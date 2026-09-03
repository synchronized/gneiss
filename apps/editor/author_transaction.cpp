// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_transaction.h"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace gneiss::editor {
namespace {

[[nodiscard]] auto find_document(std::vector<author_document>& documents,
                                 std::string_view path) noexcept {
  return std::ranges::find(documents, path, &author_document::path);
}

[[nodiscard]] auto find_document(std::span<const author_document> documents,
                                 std::string_view path) noexcept {
  return std::ranges::find(documents, path, &author_document::path);
}

[[nodiscard]] const std::optional<std::string>*
find_contents(std::span<const author_document> documents, std::string_view path) noexcept {
  const auto found = find_document(documents, path);
  return found == documents.end() ? nullptr : &found->contents;
}

} // namespace

author_revision make_author_revision(std::string_view contents) noexcept {
  constexpr std::uint64_t offset = UINT64_C(14695981039346656037);
  constexpr std::uint64_t prime = UINT64_C(1099511628211);
  auto digest = offset;
  for (const auto byte : contents) {
    digest ^= static_cast<std::uint8_t>(byte);
    digest *= prime;
  }
  return {.digest = digest, .byte_size = contents.size()};
}

result author_transaction::add(author_document_change change) noexcept {
  if (state_ != state::draft || change.path.empty() || change.baseline == change.replacement ||
      std::ranges::find(changes_, change.path, &author_document_change::path) != changes_.end()) {
    return result::invalid_argument;
  }
  try {
    changes_.push_back(std::move(change));
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result author_transaction::validate(std::span<const author_document> documents,
                                    bool expect_replacement) const noexcept {
  for (const auto& change : changes_) {
    const auto* contents = find_contents(documents, change.path);
    const std::optional<std::string> missing;
    const auto& expected = expect_replacement ? change.replacement : change.baseline;
    if ((contents == nullptr ? missing : *contents) != expected) {
      return result::invalid_state;
    }
  }
  return result::success;
}

result author_transaction::prepare(std::span<const author_document> documents) noexcept {
  if (state_ != state::draft || changes_.empty()) {
    return result::invalid_state;
  }
  const auto checked = validate(documents, false);
  if (checked == result::success) {
    state_ = state::prepared;
  }
  return checked;
}

result author_transaction::apply(std::vector<author_document>& documents, bool use_replacement,
                                 std::size_t failure_before) noexcept {
  try {
    auto staged = documents;
    for (std::size_t index = 0U; index < changes_.size(); ++index) {
      if (index == failure_before) {
        return result::io;
      }
      const auto& change = changes_[index];
      const auto& value = use_replacement ? change.replacement : change.baseline;
      auto found = find_document(staged, change.path);
      if (!value) {
        if (found != staged.end()) {
          staged.erase(found);
        }
      } else if (found == staged.end()) {
        staged.push_back({.path = change.path, .contents = value});
      } else {
        found->contents = value;
      }
    }
    documents.swap(staged);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result author_transaction::commit(std::vector<author_document>& documents,
                                  std::size_t failure_before) noexcept {
  if (state_ != state::prepared || validate(documents, false) != result::success) {
    return result::invalid_state;
  }
  const auto operation = apply(documents, true, failure_before);
  if (operation == result::success) {
    state_ = state::committed;
  }
  return operation;
}

result author_transaction::undo(std::vector<author_document>& documents) noexcept {
  if (state_ != state::committed || validate(documents, true) != result::success) {
    return result::invalid_state;
  }
  const auto operation = apply(documents, false, std::numeric_limits<std::size_t>::max());
  if (operation == result::success) {
    state_ = state::undone;
  }
  return operation;
}

result author_transaction::redo(std::vector<author_document>& documents) noexcept {
  if (state_ != state::undone || validate(documents, false) != result::success) {
    return result::invalid_state;
  }
  const auto operation = apply(documents, true, std::numeric_limits<std::size_t>::max());
  if (operation == result::success) {
    state_ = state::committed;
  }
  return operation;
}

author_revision author_transaction::baseline_revision(std::size_t index) const noexcept {
  if (index >= changes_.size() || !changes_[index].baseline) {
    return {};
  }
  return make_author_revision(*changes_[index].baseline);
}

} // namespace gneiss::editor
