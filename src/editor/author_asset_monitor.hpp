// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_AUTHOR_ASSET_MONITOR_H_
#define GNEISS_APPS_EDITOR_AUTHOR_ASSET_MONITOR_H_

#include "engine/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
  gneiss::engine::result operation{gneiss::engine::result::success};
  std::string message;
  std::uint64_t fingerprint{};
};

/** 跟踪 Scene/Prefab 指纹；内容指纹由调用方提供，不读取文件内容。 */
class author_asset_monitor final {
public:
  /** 使用本次异步读取的结果比较基线；不执行 I/O。 */
  [[nodiscard]] author_asset_change observe(const std::filesystem::path& relative_path,
                                            bool document_dirty, gneiss::engine::result operation,
                                            std::uint64_t fingerprint) noexcept;
  [[nodiscard]] gneiss::engine::result acknowledge_content(std::string_view uri,
                                                           std::string_view content) noexcept;
  void establish_baseline(std::string_view uri, std::uint64_t value);
  void accept_fingerprint(std::string_view uri, std::uint64_t value);
  [[nodiscard]] std::vector<std::string> known_uris() const;
  void mark_applied(std::string_view uri, std::uint64_t value) noexcept;
  void mark_failed(std::string_view uri, gneiss::engine::result operation) noexcept;

  [[nodiscard]] const author_asset_change& status() const noexcept { return status_; }

private:
  std::unordered_map<std::string, std::uint64_t> fingerprints_;
  author_asset_change status_;
};

} // namespace gneiss::editor

#endif
