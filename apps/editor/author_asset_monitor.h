// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_AUTHOR_ASSET_MONITOR_H_
#define GNEISS_APPS_EDITOR_AUTHOR_ASSET_MONITOR_H_

#include <gneiss/core/result.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gneiss::editor {

enum class author_asset_change_state : std::uint8_t {
  idle,
  changed,
  applied,
  failed,
  conflict,
};

struct author_asset_change final {
  author_asset_change_state state{author_asset_change_state::idle};
  std::string uri;
  result operation{result::success};
  std::string message;
};

/** 跟踪 Scene/Prefab 作者文件内容；用于区分自身保存、外部变化与未保存冲突。 */
class author_asset_monitor final {
public:
  [[nodiscard]] result initialize(const std::filesystem::path& asset_root) noexcept;
  [[nodiscard]] author_asset_change observe(const std::filesystem::path& relative_path,
                                            bool document_dirty) noexcept;
  [[nodiscard]] result acknowledge(std::string_view uri) noexcept;
  void mark_applied(std::string_view uri) noexcept;
  void mark_failed(std::string_view uri, result operation) noexcept;

  [[nodiscard]] const author_asset_change& status() const noexcept { return status_; }

private:
  [[nodiscard]] result fingerprint(std::string_view uri, std::uint64_t& output) const noexcept;

  std::filesystem::path asset_root_;
  std::unordered_map<std::string, std::uint64_t> fingerprints_;
  author_asset_change status_;
};

} // namespace gneiss::editor

#endif
