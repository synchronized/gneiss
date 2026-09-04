// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_reimport_queue.h"

#include "tooling/asset_import/asset_index.h"

#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <utility>

namespace gneiss::editor {
namespace {

namespace asset_import = gneiss::tooling::asset_import;

[[nodiscard]] bool is_safe_relative_path(const std::filesystem::path& path) {
  return !path.empty() && !path.is_absolute() && path == path.lexically_normal() &&
         *path.begin() != "..";
}

[[nodiscard]] std::string portable_path(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] editor_import_report diagnostic_report(editor_import_result result,
                                                     std::string diagnostic) {
  editor_import_report report;
  report.result = result;
  report.diagnostic = std::move(diagnostic);
  return report;
}

} // namespace

struct asset_reimport_queue::implementation final {
  struct candidate final {
    std::filesystem::path relative_path;
    clock::time_point due{};
    std::string observed_hash;
  };

  asset_reimport_queue_options options;
  import_function importer;
  std::map<std::string, candidate> candidates;
  std::deque<asset_reimport_event> events;
  std::size_t dropped{};

  void emit(asset_reimport_state state, const std::filesystem::path& relative_path,
            editor_import_report report = {}) {
    if (events.size() == options.capacity) {
      events.pop_front();
    }
    events.push_back({.state = state, .relative_path = relative_path, .import = std::move(report)});
  }
};

asset_reimport_queue::asset_reimport_queue(asset_reimport_queue_options options,
                                           import_function importer)
    : implementation_(std::make_unique<implementation>()) {
  implementation_->options = options;
  implementation_->options.capacity = std::max<std::size_t>(1U, options.capacity);
  implementation_->importer = std::move(importer);
}

asset_reimport_queue::~asset_reimport_queue() = default;

result asset_reimport_queue::notify(const std::filesystem::path& relative_path,
                                    clock::time_point now) noexcept {
  if (!is_safe_relative_path(relative_path)) {
    return result::invalid_argument;
  }
  try {
    const auto normalized = relative_path.lexically_normal();
    const auto key = portable_path(normalized);
    auto existing = implementation_->candidates.find(key);
    if (existing == implementation_->candidates.end()) {
      if (implementation_->candidates.size() == implementation_->options.capacity) {
        ++implementation_->dropped;
        return result::not_ready;
      }
      implementation::candidate value;
      value.relative_path = normalized;
      existing = implementation_->candidates.emplace(key, std::move(value)).first;
    }
    existing->second.due = now + implementation_->options.debounce;
    existing->second.observed_hash.clear();
    implementation_->emit(asset_reimport_state::waiting, normalized);
    return result::success;
  } catch (...) {
    return result::out_of_memory;
  }
}

std::size_t asset_reimport_queue::tick(const std::filesystem::path& project_root,
                                       const std::filesystem::path& asset_root,
                                       clock::time_point now, std::size_t max_imports) noexcept {
  if (project_root.empty() || asset_root.empty() || max_imports == 0U) {
    return 0U;
  }
  std::size_t imported{};
  try {
    asset_import::asset_index index;
    const auto index_report =
        asset_import::load_asset_index(project_root / ".gneiss" / "asset-index.json", index);
    for (auto iterator = implementation_->candidates.begin();
         iterator != implementation_->candidates.end() && imported < max_imports;) {
      auto& candidate = iterator->second;
      if (candidate.due > now) {
        ++iterator;
        continue;
      }
      const auto source_path = project_root / "sources" / candidate.relative_path;
      if (!std::filesystem::is_regular_file(source_path)) {
        implementation_->emit(
            asset_reimport_state::removed, candidate.relative_path,
            diagnostic_report(editor_import_result::io_error, "源资产已删除或暂时不可读"));
        iterator = implementation_->candidates.erase(iterator);
        continue;
      }
      if (index_report.result != asset_import::asset_index_result::success) {
        implementation_->emit(asset_reimport_state::failed, candidate.relative_path,
                              diagnostic_report(editor_import_result::io_error,
                                                "无法读取资产索引：" + index_report.diagnostic));
        iterator = implementation_->candidates.erase(iterator);
        continue;
      }
      const auto source = portable_path(candidate.relative_path);
      const auto indexed =
          std::ranges::find(index.entries, source, &asset_import::asset_index_entry::source_path);
      if (indexed == index.entries.end()) {
        implementation_->emit(asset_reimport_state::untracked, candidate.relative_path,
                              diagnostic_report(editor_import_result::invalid_argument,
                                                "源资产尚未导入，忽略自动重新导入"));
        iterator = implementation_->candidates.erase(iterator);
        continue;
      }
      std::string content_hash;
      const auto hash_report = asset_import::hash_source_file(source_path, content_hash);
      if (hash_report.result != asset_import::asset_index_result::success) {
        implementation_->emit(
            asset_reimport_state::failed, candidate.relative_path,
            diagnostic_report(editor_import_result::io_error, hash_report.diagnostic));
        iterator = implementation_->candidates.erase(iterator);
        continue;
      }
      if (candidate.observed_hash.empty() || candidate.observed_hash != content_hash) {
        candidate.observed_hash = std::move(content_hash);
        candidate.due = now + implementation_->options.stable_read_delay;
        ++iterator;
        continue;
      }
      if (indexed->content_hash == content_hash) {
        implementation_->emit(asset_reimport_state::unchanged, candidate.relative_path);
        iterator = implementation_->candidates.erase(iterator);
        continue;
      }
      implementation_->emit(asset_reimport_state::importing, candidate.relative_path);
      auto report = implementation_->importer(project_root, asset_root, source_path);
      ++imported;
      implementation_->emit(report.result == editor_import_result::success
                                ? asset_reimport_state::succeeded
                                : asset_reimport_state::failed,
                            candidate.relative_path, std::move(report));
      iterator = implementation_->candidates.erase(iterator);
    }
  } catch (...) {
    return imported;
  }
  return imported;
}

std::size_t asset_reimport_queue::poll_events(std::vector<asset_reimport_event>& output,
                                              std::size_t max_count) noexcept {
  try {
    const auto count = std::min(max_count, implementation_->events.size());
    output.reserve(output.size() + count);
    for (std::size_t index = 0; index < count; ++index) {
      output.push_back(std::move(implementation_->events.front()));
      implementation_->events.pop_front();
    }
    return count;
  } catch (...) {
    return 0U;
  }
}

std::size_t asset_reimport_queue::pending_count() const noexcept {
  return implementation_->candidates.size();
}

std::size_t asset_reimport_queue::dropped_candidate_count() const noexcept {
  return implementation_->dropped;
}

} // namespace gneiss::editor
