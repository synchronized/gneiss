// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gneiss::editor {

enum class asset_browser_result { success, invalid_argument, invalid_index, io_error };
enum class asset_browser_kind { source, authored_asset, imported_output };
enum class asset_browser_status { untracked, ready, stale, missing };

struct asset_browser_entry {
  std::string id;
  std::string display_name;
  std::string relative_path;
  std::string asset_uri;
  asset_browser_kind kind{asset_browser_kind::source};
  asset_browser_status status{asset_browser_status::untracked};
};

class asset_browser_model final {
public:
  [[nodiscard]] asset_browser_result refresh(const std::filesystem::path& project_root,
                                             const std::filesystem::path& asset_root);
  [[nodiscard]] const std::vector<asset_browser_entry>& entries() const noexcept {
    return entries_;
  }
  [[nodiscard]] const std::string& diagnostic() const noexcept { return diagnostic_; }
  [[nodiscard]] const std::string& selection() const noexcept { return selection_; }
  [[nodiscard]] bool select(std::string_view id) noexcept;

private:
  std::vector<asset_browser_entry> entries_;
  std::string diagnostic_;
  std::string selection_;
};

} // namespace gneiss::editor
