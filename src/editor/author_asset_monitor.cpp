// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_asset_monitor.hpp"
#include "engine/core/result.hpp"

#include <string_view>

namespace gneiss::editor {
namespace {

[[nodiscard]] bool is_structural_asset(std::string_view path) noexcept {
  return path.ends_with(".scene.json") || path.ends_with(".prefab.json");
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

} // namespace

std::vector<std::string> author_asset_monitor::known_uris() const {
  std::vector<std::string> output;
  output.reserve(fingerprints_.size());
  for (const auto& [uri, value] : fingerprints_)
    output.push_back(uri);
  return output;
}

author_asset_change author_asset_monitor::observe(const std::filesystem::path& relative_path,
                                                  bool document_dirty,
                                                  gneiss::engine::result operation,
                                                  std::uint64_t value) noexcept {
  try {
    const auto relative = path_utf8(relative_path.lexically_normal());
    if (!is_structural_asset(relative)) {
      return {};
    }
    const auto uri = "asset://" + relative;
    const auto known = fingerprints_.find(uri);
    if (operation == gneiss::engine::result::success && known != fingerprints_.end() &&
        known->second == value) {
      return {};
    }
    if (document_dirty) {
      status_ = {.state = author_asset_change_state::conflict,
                 .uri = uri,
                 .operation = gneiss::engine::result::invalid_state,
                 .message = "外部结构资产已变化；当前场景含未保存修改，未自动覆盖"};
      return status_;
    }
    status_ = {.state = author_asset_change_state::changed,
               .uri = uri,
               .operation = operation,
               .message = operation == gneiss::engine::result::success ? "检测到外部结构资产变化"
                                                                       : "结构资产已删除或不可读取",
               .fingerprint = value};
    return status_;
  } catch (...) {
    status_ = {.state = author_asset_change_state::failed,
               .uri = {},
               .operation = gneiss::engine::result::io,
               .message = "结构资产变化检查失败"};
    return status_;
  }
}

gneiss::engine::result
author_asset_monitor::acknowledge_content(std::string_view uri, std::string_view content) noexcept {
  try {
    std::uint64_t value = 14695981039346656037ULL;
    for (const unsigned char byte : content) {
      value ^= byte;
      value *= 1099511628211ULL;
    }
    accept_fingerprint(uri, value);
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::out_of_memory;
  }
}

void author_asset_monitor::establish_baseline(std::string_view uri, std::uint64_t value) {
  if (!fingerprints_.contains(std::string(uri))) {
    accept_fingerprint(uri, value);
  }
}

void author_asset_monitor::accept_fingerprint(std::string_view uri, std::uint64_t value) {
  fingerprints_.insert_or_assign(std::string(uri), value);
}

void author_asset_monitor::mark_applied(std::string_view uri, std::uint64_t value) noexcept {
  accept_fingerprint(uri, value);
  const auto operation = gneiss::engine::result::success;
  status_ = {
      .state = operation == gneiss::engine::result::success ? author_asset_change_state::applied
                                                            : author_asset_change_state::failed,
      .uri = std::string(uri),
      .operation = operation,
      .message = operation == gneiss::engine::result::success ? "外部结构资产已应用"
                                                              : "外部结构资产应用后无法建立基线"};
}

void author_asset_monitor::mark_failed(std::string_view uri,
                                       gneiss::engine::result operation) noexcept {
  status_ = {.state = author_asset_change_state::failed,
             .uri = std::string(uri),
             .operation = operation,
             .message = "外部结构资产应用失败"};
}

} // namespace gneiss::editor
