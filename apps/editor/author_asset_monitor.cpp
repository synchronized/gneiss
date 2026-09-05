// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_asset_monitor.h"

#include <array>
#include <fstream>
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

[[nodiscard]] std::filesystem::path utf8_path(std::string_view text) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

} // namespace

result author_asset_monitor::initialize(const std::filesystem::path& asset_root) noexcept {
  if (asset_root.empty()) {
    return result::invalid_argument;
  }
  try {
    asset_root_ = std::filesystem::weakly_canonical(asset_root);
    if (!std::filesystem::is_directory(asset_root_)) {
      return result::not_found;
    }
    fingerprints_.clear();
    for (const auto& item : std::filesystem::recursive_directory_iterator(asset_root_)) {
      if (!item.is_regular_file()) {
        continue;
      }
      const auto relative = path_utf8(item.path().lexically_relative(asset_root_));
      if (!is_structural_asset(relative)) {
        continue;
      }
      const auto uri = "asset://" + relative;
      std::uint64_t value = 0U;
      if (fingerprint(uri, value) == result::success) {
        fingerprints_.insert_or_assign(uri, value);
      }
    }
    status_ = {};
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result author_asset_monitor::fingerprint(std::string_view uri,
                                         std::uint64_t& output) const noexcept {
  constexpr std::string_view scheme = "asset://";
  if (asset_root_.empty() || !uri.starts_with(scheme)) {
    return result::invalid_argument;
  }
  try {
    std::ifstream stream(asset_root_ / utf8_path(uri.substr(scheme.size())), std::ios::binary);
    if (!stream) {
      return result::not_found;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    std::array<char, 4096U> buffer{};
    while (stream) {
      stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      for (std::streamsize index = 0; index < stream.gcount(); ++index) {
        hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
        hash *= 1099511628211ULL;
      }
    }
    if (!stream.eof()) {
      return result::io;
    }
    output = hash;
    return result::success;
  } catch (...) {
    return result::io;
  }
}

author_asset_change author_asset_monitor::observe(const std::filesystem::path& relative_path,
                                                  bool document_dirty) noexcept {
  try {
    const auto relative = path_utf8(relative_path.lexically_normal());
    if (!is_structural_asset(relative)) {
      return {};
    }
    const auto uri = "asset://" + relative;
    std::uint64_t value = 0U;
    const auto operation = fingerprint(uri, value);
    const auto known = fingerprints_.find(uri);
    if (operation == result::success && known != fingerprints_.end() && known->second == value) {
      return {};
    }
    if (document_dirty) {
      status_ = {.state = author_asset_change_state::conflict,
                 .uri = uri,
                 .operation = result::invalid_state,
                 .message = "外部结构资产已变化；当前场景含未保存修改，未自动覆盖"};
      return status_;
    }
    status_ = {.state = author_asset_change_state::changed,
               .uri = uri,
               .operation = operation,
               .message = operation == result::success ? "检测到外部结构资产变化"
                                                       : "结构资产已删除或不可读取"};
    return status_;
  } catch (...) {
    status_ = {.state = author_asset_change_state::failed,
               .uri = {},
               .operation = result::io,
               .message = "结构资产变化检查失败"};
    return status_;
  }
}

result author_asset_monitor::acknowledge(std::string_view uri) noexcept {
  std::uint64_t value = 0U;
  const auto operation = fingerprint(uri, value);
  if (operation == result::success) {
    try {
      fingerprints_.insert_or_assign(std::string(uri), value);
      status_ = {};
    } catch (...) {
      return result::out_of_memory;
    }
  }
  return operation;
}

void author_asset_monitor::mark_applied(std::string_view uri) noexcept {
  const auto operation = acknowledge(uri);
  status_ = {.state = operation == result::success ? author_asset_change_state::applied
                                                   : author_asset_change_state::failed,
             .uri = std::string(uri),
             .operation = operation,
             .message = operation == result::success ? "外部结构资产已应用"
                                                     : "外部结构资产应用后无法建立基线"};
}

void author_asset_monitor::mark_failed(std::string_view uri, result operation) noexcept {
  status_ = {.state = author_asset_change_state::failed,
             .uri = std::string(uri),
             .operation = operation,
             .message = "外部结构资产应用失败"};
}

} // namespace gneiss::editor
