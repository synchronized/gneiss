// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "native_author_transaction.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace gneiss::editor {
namespace {

constexpr std::string_view manifest_header = "gneiss.author-transaction\n1\n";

struct journal_entry final {
  std::filesystem::path destination;
  std::filesystem::path before;
  std::filesystem::path after;
  bool baseline_exists = false;
  bool replacement_exists = false;
};

[[nodiscard]] bool is_within(const std::filesystem::path& root, const std::filesystem::path& path) {
  auto root_part = root.begin();
  auto path_part = path.begin();
  for (; root_part != root.end(); ++root_part, ++path_part) {
    if (path_part == path.end() || *root_part != *path_part) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::filesystem::path utf8_path(std::string_view text) {
  return {std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())};
}

[[nodiscard]] result replace_file(const std::filesystem::path& temporary,
                                  const std::filesystem::path& destination) noexcept {
#ifdef _WIN32
  return MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE
             ? result::success
             : result::io;
#else
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  return error ? result::io : result::success;
#endif
}

[[nodiscard]] bool write_file(const std::filesystem::path& path, std::string_view contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  stream.flush();
  return static_cast<bool>(stream);
}

[[nodiscard]] bool read_file(const std::filesystem::path& path, std::string& contents) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }
  contents.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  return static_cast<bool>(stream) || stream.eof();
}

[[nodiscard]] result read_optional(const std::filesystem::path& path,
                                   std::optional<std::string>& contents) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    contents.reset();
    return error ? result::io : result::success;
  }
  if (!std::filesystem::is_regular_file(path, error) || error) {
    return result::io;
  }
  std::string text;
  if (!read_file(path, text)) {
    return result::io;
  }
  contents = std::move(text);
  return result::success;
}

[[nodiscard]] result resolve_destination(const std::filesystem::path& root,
                                         std::string_view relative_text,
                                         std::filesystem::path& destination) {
  if (relative_text.empty()) {
    return result::invalid_argument;
  }
  const auto relative = utf8_path(relative_text).lexically_normal();
  if (relative.empty() || relative.is_absolute() || relative.has_root_path() ||
      std::ranges::find(relative, std::filesystem::path{".."}) != relative.end()) {
    return result::invalid_argument;
  }
  std::error_code error;
  destination = std::filesystem::weakly_canonical(root / relative, error);
  if (error || !is_within(root, destination) ||
      !std::filesystem::is_directory(destination.parent_path(), error) || error) {
    return result::invalid_argument;
  }
  return result::success;
}

[[nodiscard]] std::filesystem::path
make_transaction_directory(const std::filesystem::path& transactions) {
  const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
  for (std::size_t attempt = 0U; attempt < 1024U; ++attempt) {
    auto candidate = transactions / (std::to_string(seed) + "-" + std::to_string(attempt));
    std::error_code error;
    if (std::filesystem::create_directory(candidate, error)) {
      return candidate;
    }
    if (error && error != std::errc::file_exists) {
      return {};
    }
  }
  return {};
}

[[nodiscard]] std::filesystem::path payload_path(const std::filesystem::path& journal,
                                                 std::size_t index, std::string_view suffix) {
  return journal / (std::to_string(index) + std::string(suffix));
}

[[nodiscard]] result publish_payload(const journal_entry& entry, bool replacement,
                                     const std::filesystem::path& temporary) {
  const auto& payload = replacement ? entry.after : entry.before;
  std::error_code error;
  std::filesystem::copy_file(payload, temporary, std::filesystem::copy_options::overwrite_existing,
                             error);
  if (error) {
    return result::io;
  }
  const auto operation = replace_file(temporary, entry.destination);
  if (operation != result::success) {
    std::filesystem::remove(temporary, error);
  }
  return operation;
}

[[nodiscard]] result apply_entry(const journal_entry& entry, bool replacement,
                                 const std::filesystem::path& temporary) {
  const auto exists = replacement ? entry.replacement_exists : entry.baseline_exists;
  if (exists) {
    return publish_payload(entry, replacement, temporary);
  }
  std::error_code error;
  std::filesystem::remove(entry.destination, error);
  return error ? result::io : result::success;
}

[[nodiscard]] result restore_entries(const std::vector<journal_entry>& entries,
                                     const std::filesystem::path& journal) {
  for (std::size_t index = entries.size(); index != 0U; --index) {
    const auto temporary = entries[index - 1U].destination.string() + ".gneiss-restore.tmp";
    if (apply_entry(entries[index - 1U], false, temporary) != result::success) {
      return result::io;
    }
  }
  std::error_code error;
  std::filesystem::remove_all(journal, error);
  return error ? result::io : result::success;
}

[[nodiscard]] result load_journal(const std::filesystem::path& root,
                                  std::vector<journal_entry>& entries,
                                  const std::filesystem::path& journal) {
  std::string manifest;
  if (!read_file(journal / "manifest", manifest) || !manifest.starts_with(manifest_header)) {
    return result::invalid_argument;
  }
  const auto count_text = std::string_view(manifest).substr(manifest_header.size());
  std::size_t offset = 0U;
  const auto count = std::stoull(std::string(count_text), &offset);
  if (offset != count_text.size() || count > 4096U) {
    return result::invalid_argument;
  }
  entries.clear();
  entries.reserve(count);
  for (std::size_t index = 0U; index < count; ++index) {
    std::string relative;
    if (!read_file(payload_path(journal, index, ".path"), relative)) {
      return result::invalid_argument;
    }
    journal_entry entry;
    const auto resolved = resolve_destination(root, relative, entry.destination);
    if (resolved != result::success) {
      return resolved;
    }
    entry.before = payload_path(journal, index, ".before");
    entry.after = payload_path(journal, index, ".after");
    std::error_code error;
    entry.baseline_exists = std::filesystem::exists(entry.before, error);
    if (error) {
      return result::io;
    }
    entry.replacement_exists = std::filesystem::exists(entry.after, error);
    if (error) {
      return result::io;
    }
    entries.push_back(std::move(entry));
  }
  return result::success;
}

[[nodiscard]] result recover_journal(const std::filesystem::path& root,
                                     const std::filesystem::path& journal) {
  std::error_code error;
  if (!std::filesystem::exists(journal / "manifest", error)) {
    std::filesystem::remove_all(journal, error);
    return error ? result::io : result::success;
  }
  std::vector<journal_entry> entries;
  const auto loaded = load_journal(root, entries, journal);
  if (loaded != result::success) {
    return loaded;
  }
  if (!std::filesystem::exists(journal / "committed", error)) {
    return error ? result::io : restore_entries(entries, journal);
  }
  std::filesystem::remove_all(journal, error);
  return error ? result::io : result::success;
}

[[nodiscard]] result validate_changes(const std::filesystem::path& root,
                                      std::span<const author_document_change> changes,
                                      std::vector<journal_entry>& entries) {
  entries.clear();
  entries.reserve(changes.size());
  for (std::size_t index = 0U; index < changes.size(); ++index) {
    if (changes[index].baseline == changes[index].replacement) {
      return result::invalid_argument;
    }
    for (std::size_t previous = 0U; previous < index; ++previous) {
      if (changes[previous].path == changes[index].path) {
        return result::invalid_argument;
      }
    }
    journal_entry entry;
    const auto resolved = resolve_destination(root, changes[index].path, entry.destination);
    if (resolved != result::success) {
      return resolved;
    }
    std::optional<std::string> current;
    const auto read = read_optional(entry.destination, current);
    if (read != result::success) {
      return read;
    }
    if (current != changes[index].baseline) {
      return result::invalid_state;
    }
    entry.baseline_exists = changes[index].baseline.has_value();
    entry.replacement_exists = changes[index].replacement.has_value();
    entries.push_back(std::move(entry));
  }
  return result::success;
}

[[nodiscard]] result write_journal(const std::filesystem::path& journal,
                                   std::span<const author_document_change> changes,
                                   std::vector<journal_entry>& entries) {
  std::error_code error;
  for (std::size_t index = 0U; index < changes.size(); ++index) {
    entries[index].before = payload_path(journal, index, ".before");
    entries[index].after = payload_path(journal, index, ".after");
    if (!write_file(payload_path(journal, index, ".path"), changes[index].path) ||
        (changes[index].baseline && !write_file(entries[index].before, *changes[index].baseline)) ||
        (changes[index].replacement &&
         !write_file(entries[index].after, *changes[index].replacement))) {
      std::filesystem::remove_all(journal, error);
      return result::io;
    }
  }
  const auto manifest = std::string(manifest_header) + std::to_string(changes.size());
  if (!write_file(journal / "manifest.tmp", manifest) ||
      replace_file(journal / "manifest.tmp", journal / "manifest") != result::success) {
    std::filesystem::remove_all(journal, error);
    return result::io;
  }
  return result::success;
}

[[nodiscard]] result apply_journal(const std::filesystem::path& journal,
                                   const std::vector<journal_entry>& entries,
                                   const native_author_transaction_options& options) {
  for (std::size_t index = 0U; index < entries.size(); ++index) {
    if (index == options.interrupt_after_replacements) {
      return result::not_ready;
    }
    const auto temporary = entries[index].destination.string() + ".gneiss-commit.tmp";
    if (apply_entry(entries[index], true, temporary) != result::success) {
      return restore_entries(entries, journal) == result::success ? result::io : result::internal;
    }
  }
  if (!write_file(journal / "committed.tmp", "committed") ||
      replace_file(journal / "committed.tmp", journal / "committed") != result::success) {
    return restore_entries(entries, journal) == result::success ? result::io : result::internal;
  }
  return options.interrupt_after_commit_marker ? result::not_ready : result::success;
}

} // namespace

result recover_native_author_transactions(const std::filesystem::path& asset_root) noexcept {
  try {
    std::error_code error;
    const auto root = std::filesystem::canonical(asset_root, error);
    if (error || !std::filesystem::is_directory(root, error) || error) {
      return result::invalid_argument;
    }
    const auto transactions = root / ".gneiss" / "transactions";
    if (!std::filesystem::exists(transactions, error)) {
      return error ? result::io : result::success;
    }
    std::vector<std::filesystem::path> journals;
    for (const auto& item : std::filesystem::directory_iterator(transactions, error)) {
      if (error || !item.is_directory()) {
        return result::io;
      }
      journals.push_back(item.path());
    }
    if (error) {
      return result::io;
    }
    for (const auto& journal : journals) {
      const auto recovered = recover_journal(root, journal);
      if (recovered != result::success) {
        return recovered;
      }
    }
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result commit_native_author_transaction(const std::filesystem::path& asset_root,
                                        std::span<const author_document_change> changes,
                                        const native_author_transaction_options& options) noexcept {
  if (changes.empty()) {
    return result::invalid_argument;
  }
  try {
    std::error_code error;
    const auto root = std::filesystem::canonical(asset_root, error);
    if (error || !std::filesystem::is_directory(root, error) || error) {
      return result::invalid_argument;
    }
    const auto recovery = recover_native_author_transactions(root);
    if (recovery != result::success) {
      return recovery;
    }
    std::vector<journal_entry> entries;
    const auto validated = validate_changes(root, changes, entries);
    if (validated != result::success) {
      return validated;
    }

    const auto transactions = root / ".gneiss" / "transactions";
    std::filesystem::create_directories(transactions, error);
    if (error) {
      return result::io;
    }
    const auto journal = make_transaction_directory(transactions);
    if (journal.empty()) {
      return result::io;
    }
    const auto written = write_journal(journal, changes, entries);
    if (written != result::success) {
      return written;
    }
    const auto applied = apply_journal(journal, entries, options);
    if (applied != result::success) {
      return applied;
    }
    std::filesystem::remove_all(journal, error);
    return error ? result::io : result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

} // namespace gneiss::editor
