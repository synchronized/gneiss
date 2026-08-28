// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_index.h"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <set>
#include <span>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace gneiss::tooling::asset_import {
namespace {

struct document_deleter final {
  void operator()(yyjson_doc* document) const noexcept { yyjson_doc_free(document); }
};

using document_ptr = std::unique_ptr<yyjson_doc, document_deleter>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

[[nodiscard]] asset_index_report failure(asset_index_result result, std::string diagnostic) {
  return {.result = result, .diagnostic = std::move(diagnostic)};
}

[[nodiscard]] std::string_view json_string(yyjson_val* value) {
  return {yyjson_get_str(value), yyjson_get_len(value)};
}

[[nodiscard]] bool has_exact_fields(yyjson_val* object, std::span<const char* const> names) {
  if (!yyjson_is_obj(object) || yyjson_obj_size(object) != names.size()) {
    return false;
  }
  return std::ranges::all_of(names,
                             [object](const char* name) { return yyjson_obj_get(object, name); });
}

[[nodiscard]] bool is_safe_relative_path(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  const std::filesystem::path path(
      std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
  return !path.is_absolute() && path == path.lexically_normal() && *path.begin() != "..";
}

[[nodiscard]] std::string_view state_name(asset_import_state state) {
  switch (state) {
  case asset_import_state::ready:
    return "ready";
  case asset_import_state::stale:
    return "stale";
  case asset_import_state::missing:
    return "missing";
  }
  return {};
}

[[nodiscard]] bool parse_state(yyjson_val* value, asset_import_state& output) {
  if (!yyjson_is_str(value)) {
    return false;
  }
  const auto text = json_string(value);
  if (text == "ready") {
    output = asset_import_state::ready;
    return true;
  }
  if (text == "stale") {
    output = asset_import_state::stale;
    return true;
  }
  if (text == "missing") {
    output = asset_import_state::missing;
    return true;
  }
  return false;
}

[[nodiscard]] bool validate_entry(const asset_index_entry& entry, std::string& diagnostic) {
  if (!is_safe_relative_path(entry.source_path) || entry.source_key.empty() ||
      entry.importer_id.empty() || entry.importer_version == 0U || entry.content_hash.empty()) {
    diagnostic = "资产索引记录包含空字段或不安全的源路径";
    return false;
  }
  const auto prefix = std::string{"asset://imported/"} + entry.source_key + '/';
  std::set<std::string> unique_outputs;
  for (const auto& uri : entry.output_uris) {
    if (!uri.starts_with(prefix) || uri.find("..") != std::string::npos ||
        !unique_outputs.insert(uri).second) {
      diagnostic = "资产索引记录包含越界或重复的输出 URI";
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool parse_entry(yyjson_val* value, asset_index_entry& output) {
  constexpr std::array fields = {"source", "source_key", "importer", "importer_version",
                                 "hash",   "state",      "outputs"};
  if (!has_exact_fields(value, fields)) {
    return false;
  }
  auto* source = yyjson_obj_get(value, "source");
  auto* source_key = yyjson_obj_get(value, "source_key");
  auto* importer = yyjson_obj_get(value, "importer");
  auto* importer_version = yyjson_obj_get(value, "importer_version");
  auto* hash = yyjson_obj_get(value, "hash");
  auto* state = yyjson_obj_get(value, "state");
  auto* outputs = yyjson_obj_get(value, "outputs");
  if (!yyjson_is_str(source) || !yyjson_is_str(source_key) || !yyjson_is_str(importer) ||
      !yyjson_is_uint(importer_version) || yyjson_get_uint(importer_version) == 0U ||
      yyjson_get_uint(importer_version) > UINT32_MAX || !yyjson_is_str(hash) ||
      !yyjson_is_arr(outputs) || !parse_state(state, output.state)) {
    return false;
  }
  output.source_path = json_string(source);
  output.source_key = json_string(source_key);
  output.importer_id = json_string(importer);
  output.importer_version = static_cast<std::uint32_t>(yyjson_get_uint(importer_version));
  output.content_hash = json_string(hash);
  yyjson_val* item = nullptr;
  std::size_t index = 0;
  std::size_t maximum = 0;
  yyjson_arr_foreach(outputs, index, maximum, item) {
    if (!yyjson_is_str(item)) {
      return false;
    }
    output.output_uris.emplace_back(json_string(item));
  }
  std::string diagnostic;
  return validate_entry(output, diagnostic);
}

[[nodiscard]] bool append_string(yyjson_mut_doc* document, yyjson_mut_val* object, const char* name,
                                 std::string_view value) {
  return yyjson_mut_obj_add_strncpy(document, object, name, value.data(), value.size());
}

[[nodiscard]] bool append_entry(yyjson_mut_doc* document, yyjson_mut_val* entries,
                                const asset_index_entry& entry) {
  auto* object = yyjson_mut_obj(document);
  auto* outputs = yyjson_mut_arr(document);
  if (object == nullptr || outputs == nullptr ||
      !append_string(document, object, "source", entry.source_path) ||
      !append_string(document, object, "source_key", entry.source_key) ||
      !append_string(document, object, "importer", entry.importer_id) ||
      !yyjson_mut_obj_add_uint(document, object, "importer_version", entry.importer_version) ||
      !append_string(document, object, "hash", entry.content_hash) ||
      !append_string(document, object, "state", state_name(entry.state))) {
    return false;
  }
  for (const auto& uri : entry.output_uris) {
    if (!yyjson_mut_arr_add_strncpy(document, outputs, uri.data(), uri.size())) {
      return false;
    }
  }
  return yyjson_mut_obj_add_val(document, object, "outputs", outputs) &&
         yyjson_mut_arr_add_val(entries, object);
}

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return stream.good();
}

} // namespace

asset_index_report load_asset_index(const std::filesystem::path& path, asset_index& output) {
  if (path.empty()) {
    return failure(asset_index_result::invalid_format, "资产索引路径不能为空");
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return failure(std::filesystem::exists(path) ? asset_index_result::io_error
                                                 : asset_index_result::not_found,
                   "无法打开资产索引");
  }
  const std::string json{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  document_ptr document{yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG)};
  auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
  constexpr std::array root_fields = {"format", "version", "entries"};
  if (!has_exact_fields(root, root_fields)) {
    return failure(asset_index_result::invalid_format, "资产索引根结构无效");
  }
  auto* format = yyjson_obj_get(root, "format");
  auto* version = yyjson_obj_get(root, "version");
  auto* entries = yyjson_obj_get(root, "entries");
  if (!yyjson_is_str(format) || json_string(format) != "gneiss.asset-index" ||
      !yyjson_is_uint(version) || !yyjson_is_arr(entries)) {
    return failure(asset_index_result::invalid_format, "资产索引头无效");
  }
  if (yyjson_get_uint(version) != asset_index_version) {
    return failure(asset_index_result::unsupported_version, "不支持该资产索引版本");
  }
  asset_index parsed;
  std::set<std::string> source_paths;
  std::set<std::string> source_keys;
  yyjson_val* item = nullptr;
  std::size_t index = 0;
  std::size_t maximum = 0;
  yyjson_arr_foreach(entries, index, maximum, item) {
    asset_index_entry entry;
    if (!parse_entry(item, entry) || !source_paths.insert(entry.source_path).second ||
        !source_keys.insert(entry.source_key).second) {
      return failure(asset_index_result::invalid_format, "资产索引记录无效或重复");
    }
    parsed.entries.push_back(std::move(entry));
  }
  output = std::move(parsed);
  return {.result = asset_index_result::success, .diagnostic = {}};
}

asset_index_report save_asset_index(const std::filesystem::path& path, const asset_index& index) {
  if (path.empty() || path.filename().empty()) {
    return failure(asset_index_result::invalid_format, "资产索引路径不能为空");
  }
  asset_index normalized = index;
  std::ranges::sort(normalized.entries, {}, &asset_index_entry::source_path);
  std::set<std::string> source_paths;
  std::set<std::string> source_keys;
  for (const auto& entry : normalized.entries) {
    std::string diagnostic;
    if (!validate_entry(entry, diagnostic) || !source_paths.insert(entry.source_path).second ||
        !source_keys.insert(entry.source_key).second) {
      return failure(asset_index_result::invalid_format,
                     diagnostic.empty() ? "资产索引记录重复" : std::move(diagnostic));
    }
  }

  yyjson_mut_doc* raw_document = yyjson_mut_doc_new(nullptr);
  if (raw_document == nullptr) {
    return failure(asset_index_result::io_error, "无法分配资产索引文档");
  }
  mutable_document_ptr document(raw_document, &yyjson_mut_doc_free);
  auto* root = yyjson_mut_obj(document.get());
  auto* entries = yyjson_mut_arr(document.get());
  if (root == nullptr || entries == nullptr ||
      !yyjson_mut_obj_add_str(document.get(), root, "format", "gneiss.asset-index") ||
      !yyjson_mut_obj_add_uint(document.get(), root, "version", asset_index_version)) {
    return failure(asset_index_result::io_error, "无法构造资产索引文档");
  }
  for (const auto& entry : normalized.entries) {
    if (!append_entry(document.get(), entries, entry)) {
      return failure(asset_index_result::io_error, "无法构造资产索引记录");
    }
  }
  if (!yyjson_mut_obj_add_val(document.get(), root, "entries", entries)) {
    return failure(asset_index_result::io_error, "无法完成资产索引文档");
  }
  yyjson_mut_doc_set_root(document.get(), root);
  std::size_t length = 0;
  std::unique_ptr<char, decltype(&std::free)> json(
      yyjson_mut_write(document.get(), YYJSON_WRITE_PRETTY, &length), &std::free);
  if (!json) {
    return failure(asset_index_result::io_error, "无法序列化资产索引");
  }

  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return failure(asset_index_result::io_error, "无法创建资产索引目录");
  }
  auto temporary = path;
  temporary += ".gneiss-staging";
  auto backup = path;
  backup += ".gneiss-backup";
  std::filesystem::remove(temporary, error);
  error.clear();
  if (!write_text(temporary, std::string_view(json.get(), length))) {
    std::filesystem::remove(temporary, error);
    return failure(asset_index_result::io_error, "无法写入资产索引暂存文件");
  }
  if (std::filesystem::exists(path)) {
    std::filesystem::remove(backup, error);
    error.clear();
    std::filesystem::rename(path, backup, error);
    if (error) {
      std::filesystem::remove(temporary, error);
      return failure(asset_index_result::io_error, "无法暂存旧资产索引");
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::error_code restore_error;
    if (std::filesystem::exists(backup)) {
      std::filesystem::rename(backup, path, restore_error);
    }
    std::filesystem::remove(temporary, restore_error);
    return failure(asset_index_result::io_error, "无法提交新资产索引");
  }
  std::filesystem::remove(backup, error);
  return {.result = asset_index_result::success, .diagnostic = {}};
}

asset_index_report hash_source_file(const std::filesystem::path& path, std::string& output_hash) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return failure(asset_index_result::not_found, "无法读取源资产以计算哈希");
  }
  constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  auto hash = offset_basis;
  std::array<char, 64U * 1024U> buffer{};
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    for (std::streamsize index = 0; index < stream.gcount(); ++index) {
      hash ^= static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]);
      hash *= prime;
    }
  }
  if (!stream.eof()) {
    return failure(asset_index_result::io_error, "读取源资产时发生错误");
  }
  std::ostringstream text;
  text << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  output_hash = text.str();
  return {.result = asset_index_result::success, .diagnostic = {}};
}

asset_index_report upsert_asset_index_entry(asset_index& index, asset_index_entry entry) {
  std::string diagnostic;
  if (!validate_entry(entry, diagnostic)) {
    return failure(asset_index_result::invalid_format, std::move(diagnostic));
  }
  const auto conflict = std::ranges::find_if(index.entries, [&entry](const auto& existing) {
    return existing.source_key == entry.source_key && existing.source_path != entry.source_path;
  });
  if (conflict != index.entries.end()) {
    return failure(asset_index_result::invalid_format, "稳定源键与另一源文件冲突");
  }
  const auto existing =
      std::ranges::find(index.entries, entry.source_path, &asset_index_entry::source_path);
  if (existing == index.entries.end()) {
    index.entries.push_back(std::move(entry));
  } else {
    *existing = std::move(entry);
  }
  return {.result = asset_index_result::success, .diagnostic = {}};
}

} // namespace gneiss::tooling::asset_import
