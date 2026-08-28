// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_browser_model.h"

#include "tooling/asset_import/asset_index.h"

#include <algorithm>
#include <map>
#include <string_view>

namespace gneiss::editor {
namespace {

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::filesystem::path utf8_path(std::string_view text) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

[[nodiscard]] bool is_within(const std::filesystem::path& root,
                             const std::filesystem::path& candidate) {
  const auto relative = candidate.lexically_relative(root);
  return !relative.empty() && relative != "." && *relative.begin() != "..";
}

[[nodiscard]] asset_browser_status
source_status(const std::filesystem::path& source, const std::filesystem::path& asset_root,
              const gneiss::tooling::asset_import::asset_index_entry& indexed) {
  std::string hash;
  if (gneiss::tooling::asset_import::hash_source_file(source, hash).result !=
      gneiss::tooling::asset_import::asset_index_result::success) {
    return asset_browser_status::missing;
  }
  if (hash != indexed.content_hash) {
    return asset_browser_status::stale;
  }
  constexpr std::string_view scheme = "asset://";
  for (const auto& uri : indexed.output_uris) {
    const auto output = std::filesystem::weakly_canonical(
        asset_root / utf8_path(std::string_view(uri).substr(scheme.size())));
    if (!is_within(asset_root, output) || !std::filesystem::is_regular_file(output)) {
      return asset_browser_status::stale;
    }
  }
  return asset_browser_status::ready;
}

} // namespace

asset_browser_result asset_browser_model::refresh(const std::filesystem::path& project_root,
                                                  const std::filesystem::path& asset_root) {
  if (project_root.empty() || asset_root.empty()) {
    return asset_browser_result::invalid_argument;
  }
  try {
    const auto canonical_project = std::filesystem::weakly_canonical(project_root);
    const auto canonical_assets = std::filesystem::weakly_canonical(asset_root);
    if (!std::filesystem::is_directory(canonical_project) ||
        !std::filesystem::is_directory(canonical_assets) ||
        !is_within(canonical_project, canonical_assets)) {
      return asset_browser_result::invalid_argument;
    }

    gneiss::tooling::asset_import::asset_index index;
    const auto loaded = gneiss::tooling::asset_import::load_asset_index(
        canonical_project / ".gneiss" / "asset-index.json", index);
    if (loaded.result != gneiss::tooling::asset_import::asset_index_result::success &&
        loaded.result != gneiss::tooling::asset_import::asset_index_result::not_found) {
      diagnostic_ = loaded.diagnostic;
      return asset_browser_result::invalid_index;
    }
    std::map<std::string, const gneiss::tooling::asset_import::asset_index_entry*> indexed_sources;
    for (const auto& entry : index.entries) {
      indexed_sources.emplace(entry.source_path, &entry);
    }

    std::vector<asset_browser_entry> pending;
    const auto source_root = canonical_project / "sources";
    if (std::filesystem::is_directory(source_root)) {
      for (const auto& item : std::filesystem::recursive_directory_iterator(source_root)) {
        if (!item.is_regular_file()) {
          continue;
        }
        const auto canonical = std::filesystem::weakly_canonical(item.path());
        if (!is_within(source_root, canonical)) {
          continue;
        }
        const auto relative = path_utf8(canonical.lexically_relative(source_root));
        const auto indexed = indexed_sources.find(relative);
        pending.push_back(
            {.id = "source:" + relative,
             .display_name = path_utf8(canonical.filename()),
             .relative_path = relative,
             .asset_uri = {},
             .kind = asset_browser_kind::source,
             .status = indexed == indexed_sources.end()
                           ? asset_browser_status::untracked
                           : source_status(canonical, canonical_assets, *indexed->second)});
        indexed_sources.erase(relative);
      }
    }
    for (const auto& [relative, indexed] : indexed_sources) {
      pending.push_back({.id = "source:" + relative,
                         .display_name = path_utf8(utf8_path(relative).filename()),
                         .relative_path = relative,
                         .asset_uri = {},
                         .kind = asset_browser_kind::source,
                         .status = asset_browser_status::missing});
    }

    for (const auto& item : std::filesystem::recursive_directory_iterator(canonical_assets)) {
      if (!item.is_regular_file()) {
        continue;
      }
      const auto canonical = std::filesystem::weakly_canonical(item.path());
      if (!is_within(canonical_assets, canonical)) {
        continue;
      }
      const auto relative = path_utf8(canonical.lexically_relative(canonical_assets));
      const auto imported = std::string_view(relative).starts_with("imported/");
      pending.push_back({.id = "asset:" + relative,
                         .display_name = path_utf8(canonical.filename()),
                         .relative_path = relative,
                         .asset_uri = "asset://" + relative,
                         .kind = imported ? asset_browser_kind::imported_output
                                          : asset_browser_kind::authored_asset,
                         .status = asset_browser_status::ready});
    }
    std::ranges::sort(pending, {}, &asset_browser_entry::id);
    if (!selection_.empty() && std::ranges::none_of(pending, [this](const auto& entry) {
          return entry.id == selection_;
        })) {
      selection_.clear();
    }
    entries_ = std::move(pending);
    diagnostic_.clear();
    return asset_browser_result::success;
  } catch (...) {
    return asset_browser_result::io_error;
  }
}

bool asset_browser_model::select(std::string_view id) noexcept {
  const auto found = std::ranges::find(entries_, id, &asset_browser_entry::id);
  if (found == entries_.end()) {
    return false;
  }
  selection_ = found->id;
  return true;
}

} // namespace gneiss::editor
