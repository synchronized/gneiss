// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_reimport_queue.hpp"

#include "tooling/asset_import/asset_index.hpp"

#include <algorithm>
#include <deque>
#include <exception>
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

[[nodiscard]] editor_import_report
invoke_import(const asset_reimport_queue::import_function& importer,
              const std::filesystem::path& project, const std::filesystem::path& assets,
              const std::filesystem::path& source) {
  editor_import_report report;
  try {
    report = importer(project, assets, source);
  } catch (const std::exception& error) {
    report = diagnostic_report(editor_import_result::import_failed,
                               std::string{"重新导入异常："} + error.what());
  } catch (...) {
    report = diagnostic_report(editor_import_result::import_failed, "重新导入发生未知异常");
  }
  if (report.source_path.empty()) {
    report.source_path = source;
  }
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
  bool rescan_requested = false;
  bool rescan_active = false;
  result rescan_operation = result::success;
  std::vector<std::filesystem::path> rescan_sources;
  std::size_t rescan_cursor = 0U;

  void begin_rescan(const asset_import::asset_index& index,
                    const asset_import::asset_index_report& index_report) {
    if (rescan_requested && !rescan_active) {
      rescan_requested = false;
      rescan_sources.clear();
      rescan_cursor = 0U;
      rescan_operation = result::success;
      if (index_report.result != asset_import::asset_index_result::success &&
          index_report.result != asset_import::asset_index_result::not_found) {
        rescan_operation = result::io;
        emit(asset_reimport_state::failed, ".",
             diagnostic_report(editor_import_result::io_error,
                               "源资产补扫无法读取索引：" + index_report.diagnostic));
      } else {
        for (const auto& entry : index.entries) {
          const auto source = std::filesystem::path(
              std::u8string(reinterpret_cast<const char8_t*>(entry.source_path.data()),
                            entry.source_path.size()));
          if (!is_safe_relative_path(source)) {
            rescan_operation = result::invalid_argument;
            rescan_sources.clear();
            break;
          }
          rescan_sources.push_back(source);
        }
        rescan_active = rescan_operation == result::success;
      }
    }
  }

  void admit_rescan(asset_reimport_queue& queue, std::size_t max_candidates,
                    clock::time_point now) {
    // 队列满时保留游标，后续帧继续；不要把补扫自身计作候选丢失。
    for (std::size_t admitted = 0U;
         rescan_active && admitted < max_candidates && rescan_cursor < rescan_sources.size() &&
         candidates.size() < options.capacity;
         ++admitted) {
      const auto& source = rescan_sources[rescan_cursor];
      if (!candidates.contains(portable_path(source))) {
        const auto operation = queue.notify(source, now);
        if (operation != result::success) {
          rescan_operation = operation;
          rescan_active = false;
          break;
        }
      }
      ++rescan_cursor;
    }
  }

  void finish_rescan() {
    if (rescan_active && rescan_cursor == rescan_sources.size() && candidates.empty()) {
      rescan_active = false;
      rescan_sources.clear();
    }
  }

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
  try {
    if (!is_safe_relative_path(relative_path)) {
      return result::invalid_argument;
    }
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

void asset_reimport_queue::request_rescan() noexcept { implementation_->rescan_requested = true; }

bool asset_reimport_queue::is_rescanning() const noexcept {
  return implementation_->rescan_requested || implementation_->rescan_active;
}

result asset_reimport_queue::rescan_result() const noexcept {
  return implementation_->rescan_operation;
}

std::size_t asset_reimport_queue::tick(const std::filesystem::path& project_root,
                                       const std::filesystem::path& asset_root,
                                       clock::time_point now, std::size_t max_imports,
                                       std::size_t max_candidates) noexcept {
  if (project_root.empty() || asset_root.empty() || max_imports == 0U || max_candidates == 0U) {
    return 0U;
  }
  if (implementation_->candidates.empty() && !is_rescanning()) {
    return 0U;
  }
  std::size_t imported{};
  try {
    asset_import::asset_index index;
    const auto index_report =
        asset_import::load_asset_index(project_root / ".gneiss" / "asset-index.json", index);
    implementation_->begin_rescan(index, index_report);
    implementation_->admit_rescan(*this, max_candidates, now);
    std::size_t examined = 0U;
    for (auto iterator = implementation_->candidates.begin();
         iterator != implementation_->candidates.end() && imported < max_imports &&
         examined < max_candidates;) {
      auto& candidate = iterator->second;
      if (candidate.due > now) {
        ++iterator;
        continue;
      }
      ++examined;
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
      if (indexed->content_hash == content_hash && indexed->importer_id == "gneiss.gltf" &&
          indexed->importer_version == asset_import::gltf_importer_version) {
        implementation_->emit(asset_reimport_state::unchanged, candidate.relative_path);
        iterator = implementation_->candidates.erase(iterator);
        continue;
      }
      implementation_->emit(asset_reimport_state::importing, candidate.relative_path);
      // 失败尝试也消耗本帧预算；异常只能终止当前任务，不能在后续帧无限重复执行。
      ++imported;
      auto report = invoke_import(implementation_->importer, project_root, asset_root, source_path);
      implementation_->emit(report.result == editor_import_result::success
                                ? asset_reimport_state::succeeded
                                : asset_reimport_state::failed,
                            candidate.relative_path, std::move(report));
      iterator = implementation_->candidates.erase(iterator);
    }
    implementation_->finish_rescan();
  } catch (...) {
    if (is_rescanning()) {
      implementation_->rescan_operation = result::io;
      implementation_->rescan_active = false;
      implementation_->rescan_requested = false;
      implementation_->rescan_sources.clear();
    }
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
