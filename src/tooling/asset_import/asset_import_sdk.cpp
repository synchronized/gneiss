// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_import_sdk.h"

#include "tooling/asset_import/asset_index.h"
#include "tooling/asset_import/asset_writer.h"
#include "tooling/asset_import/gltf_importer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace gneiss::tooling::asset_import {
namespace {

[[nodiscard]] bool is_within(const std::filesystem::path& root,
                             const std::filesystem::path& candidate) {
  const auto relative = candidate.lexically_relative(root);
  return !relative.empty() && relative != "." && *relative.begin() != "..";
}

[[nodiscard]] std::string stable_source_key(const std::filesystem::path& relative_source) {
  constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  auto hash = offset_basis;
  const auto normalized = relative_source.generic_u8string();
  for (const auto character : normalized) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= prime;
  }
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << hash;
  return stream.str();
}

[[nodiscard]] import_asset_result map_result(inspect_result result) {
  switch (result) {
  case inspect_result::success:
    return import_asset_result::success;
  case inspect_result::invalid_argument:
    return import_asset_result::invalid_argument;
  case inspect_result::source_unavailable:
    return import_asset_result::source_unavailable;
  case inspect_result::invalid_source:
    return import_asset_result::invalid_source;
  case inspect_result::unsupported_feature:
    return import_asset_result::unsupported_feature;
  }
  return import_asset_result::invalid_source;
}

[[nodiscard]] import_asset_report failure(import_asset_result result, std::string diagnostic) {
  import_asset_report report;
  report.result = result;
  report.diagnostic = std::move(diagnostic);
  return report;
}

[[nodiscard]] std::vector<std::string> collect_output_uris(const import_ir& data,
                                                           std::string_view source_key) {
  const auto prefix = std::string{"asset://imported/"} + std::string{source_key} + '/';
  std::vector<std::string> outputs;
  for (std::size_t mesh_index = 0; mesh_index < data.meshes.size(); ++mesh_index) {
    for (std::size_t primitive_index = 0;
         primitive_index < data.meshes[mesh_index].primitives.size(); ++primitive_index) {
      outputs.push_back(prefix + "models/mesh-" + std::to_string(mesh_index) + "-primitive-" +
                        std::to_string(primitive_index) + ".gneiss-mesh");
    }
  }
  for (std::size_t index = 0; index < data.materials.size(); ++index) {
    outputs.push_back(prefix + "materials/material-" + std::to_string(index) + ".material.json");
  }
  const bool needs_default = std::ranges::any_of(data.meshes, [](const import_ir_mesh& mesh) {
    return std::ranges::any_of(mesh.primitives, [](const import_ir_primitive& primitive) {
      return !primitive.material_index;
    });
  });
  if (needs_default) {
    outputs.push_back(prefix + "materials/default.material.json");
  }
  for (std::size_t index = 0; index < data.images.size(); ++index) {
    const auto variants = image_variants(data, index);
    for (std::size_t variant = 0; variant < variants.size(); ++variant) {
      if (!variants[variant])
        continue;
      const auto name = "textures/image-" + std::to_string(index) + image_variant_suffixes[variant];
      outputs.push_back(prefix + name + ".png");
      outputs.push_back(prefix + name + ".texture.json");
    }
  }
  outputs.push_back(prefix + "scenes/scene.scene.json");
  return outputs;
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

} // namespace

import_asset_report import_project_asset(const import_asset_request& request) {
  if (request.source_root.empty() || request.imported_root.empty() || request.source_path.empty()) {
    return failure(import_asset_result::invalid_argument, "源目录、派生目录和源文件均不能为空");
  }

  try {
    const auto source_root = std::filesystem::weakly_canonical(request.source_root);
    const auto source_path = std::filesystem::weakly_canonical(request.source_path);
    if (!is_within(source_root, source_path)) {
      return failure(import_asset_result::invalid_argument, "源文件必须位于工程 sources 目录内");
    }
    if (!std::filesystem::is_regular_file(source_path)) {
      return failure(import_asset_result::source_unavailable, "源文件不存在或不是普通文件");
    }
    const auto extension = source_path.extension().string();
    if (extension != ".gltf" && extension != ".glb") {
      return failure(import_asset_result::unsupported_feature, "当前导入 SDK 仅支持 glTF 和 GLB");
    }

    const auto relative_source = source_path.lexically_relative(source_root);
    const auto source_key = stable_source_key(relative_source);
    const auto output_directory =
        std::filesystem::absolute(request.imported_root).lexically_normal() / source_key;
    auto inspected = inspect_gltf(source_path);
    if (inspected.result != inspect_result::success) {
      auto report = failure(map_result(inspected.result), std::move(inspected.diagnostic));
      report.summary = inspected.summary;
      report.source_key = source_key;
      report.output_directory = output_directory;
      return report;
    }
    const auto asset_uri_prefix = std::string{"asset://imported/"} + source_key + '/';
    auto written = write_assets(inspected.data, output_directory, asset_uri_prefix);
    if (!written.success) {
      auto report = failure(import_asset_result::write_failed, std::move(written.diagnostic));
      report.summary = inspected.summary;
      report.source_key = source_key;
      report.output_directory = output_directory;
      return report;
    }
    return {.result = import_asset_result::success,
            .summary = inspected.summary,
            .source_key = source_key,
            .output_directory = output_directory,
            .output_uris = collect_output_uris(inspected.data, source_key),
            .diagnostic = std::move(inspected.diagnostic)};
  } catch (const std::exception& error) {
    return failure(import_asset_result::source_unavailable,
                   std::string{"解析导入路径失败："} + error.what());
  }
}

import_asset_report import_project_asset_and_update_index(const import_asset_request& request,
                                                          const std::filesystem::path& index_path,
                                                          const import_control& control) {
  if (request.source_root.empty() || request.imported_root.empty() || request.source_path.empty() ||
      index_path.empty()) {
    return failure(import_asset_result::invalid_argument, "导入路径不能为空");
  }
  const auto cancelled = [&control] { return control.cancelled && control.cancelled(); };
  if (cancelled()) {
    return failure(import_asset_result::cancelled, "导入已取消");
  }
  asset_index index;
  const auto loaded = load_asset_index(index_path, index);
  if (loaded.result != asset_index_result::success &&
      loaded.result != asset_index_result::not_found) {
    return failure(import_asset_result::index_update_failed,
                   "读取资产索引失败：" + loaded.diagnostic);
  }
  std::string before_hash;
  if (hash_source_file(request.source_path, before_hash).result != asset_index_result::success) {
    return failure(import_asset_result::source_unavailable, "读取源资产失败");
  }
  // 独占临时目录位于工程元数据中，避免作者资产监听看到未提交的 Scene。
  struct temporary_directory {
    std::filesystem::path path;
    ~temporary_directory() {
      try {
        if (!path.empty()) {
          std::error_code ignored;
          std::filesystem::remove_all(path, ignored);
        }
      } catch (...) {
        // 清理失败不得覆盖原始导入结果，也不得在析构期间抛出。
      }
    }
  } staging;
  import_asset_report imported;
  std::filesystem::path destination;
  std::filesystem::path backup;
  bool old_moved = false;
  bool new_moved = false;
  const auto rollback = [&] {
    std::error_code error;
    if (new_moved) {
      std::filesystem::remove_all(destination, error);
      if (error) {
        return false;
      }
    }
    if (old_moved) {
      std::filesystem::rename(backup, destination, error);
    }
    return !error;
  };
  try {
    static std::atomic_uint64_t sequence{};
    const auto temporary_root = std::filesystem::absolute(index_path).parent_path() / "import-work";
    std::filesystem::create_directories(temporary_root);
    for (;;) {
      const auto name =
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
          std::to_string(sequence.fetch_add(1U));
      const auto candidate = temporary_root / name;
      if (std::filesystem::create_directory(candidate)) {
        staging.path = candidate;
        break;
      }
    }
    if (cancelled()) {
      return failure(import_asset_result::cancelled, "导入已取消");
    }
    auto staged_request = request;
    staged_request.imported_root = staging.path;
    imported = import_project_asset(staged_request);
    if (imported.result != import_asset_result::success) {
      return imported;
    }
    if (cancelled()) {
      return failure(import_asset_result::cancelled, "导入已取消，暂存产物已丢弃");
    }
    std::string after_hash;
    if (hash_source_file(request.source_path, after_hash).result != asset_index_result::success ||
        before_hash != after_hash) {
      return failure(import_asset_result::source_changed, "导入期间源文件变化，暂存产物已丢弃");
    }
    const auto source_root = std::filesystem::weakly_canonical(request.source_root);
    const auto source_path = std::filesystem::weakly_canonical(request.source_path);
    asset_index_entry entry{.source_path = path_utf8(source_path.lexically_relative(source_root)),
                            .source_key = imported.source_key,
                            .importer_id = "gneiss.gltf",
                            .importer_version = gltf_importer_version,
                            .content_hash = std::move(after_hash),
                            .state = asset_import_state::ready,
                            .output_uris = imported.output_uris};
    const auto updated = upsert_asset_index_entry(index, std::move(entry));
    if (updated.result != asset_index_result::success) {
      return failure(import_asset_result::index_update_failed, updated.diagnostic);
    }
    destination =
        std::filesystem::absolute(request.imported_root).lexically_normal() / imported.source_key;
    // 备份在目标目录同级，回滚失败时保留以便人工恢复；不由暂存清理器删除。
    backup = destination;
    backup += ".gneiss-index-backup";
    if (std::filesystem::exists(backup)) {
      return failure(import_asset_result::write_failed,
                     "存在待恢复的导入备份：" + path_utf8(backup));
    }
    if (cancelled() || (control.begin_commit && !control.begin_commit())) {
      return failure(import_asset_result::cancelled, "提交前已取消或被新任务取代");
    }
    std::filesystem::create_directories(destination.parent_path());
    if (std::filesystem::exists(destination)) {
      std::filesystem::rename(destination, backup);
      old_moved = true;
    }
    std::filesystem::rename(imported.output_directory, destination);
    new_moved = true;
    const auto saved = save_asset_index(index_path, index);
    if (saved.result != asset_index_result::success) {
      const auto restored = rollback();
      old_moved = false;
      new_moved = false;
      return failure(import_asset_result::index_update_failed,
                     saved.diagnostic +
                         (restored ? "；旧产物已恢复" : "；回滚失败，请保留并恢复备份"));
    }
    old_moved = false;
    new_moved = false;
    std::error_code ignored;
    std::filesystem::remove_all(backup, ignored);
    imported.output_directory = std::move(destination);
    return imported;
  } catch (const std::exception& error) {
    const auto restored = rollback();
    return failure(import_asset_result::write_failed,
                   std::string{"导入事务失败："} + error.what() +
                       (restored ? "" : "；回滚失败，请保留并恢复备份"));
  }
}

asset_index_report rebuild_asset_index(const std::filesystem::path& source_root,
                                       const std::filesystem::path& imported_root,
                                       const std::filesystem::path& index_path) {
  if (source_root.empty() || imported_root.empty() || index_path.empty()) {
    return {.result = asset_index_result::invalid_format,
            .diagnostic = "重建资产索引所需路径不能为空"};
  }
  asset_index rebuilt;
  try {
    if (!std::filesystem::is_directory(source_root)) {
      return {.result = asset_index_result::not_found, .diagnostic = "sources 目录不存在"};
    }
    std::vector<std::filesystem::path> sources;
    for (const auto& item : std::filesystem::recursive_directory_iterator(source_root)) {
      if (!item.is_regular_file()) {
        continue;
      }
      const auto extension = item.path().extension().string();
      if (extension == ".gltf" || extension == ".glb") {
        sources.push_back(item.path());
      }
    }
    std::ranges::sort(sources, {}, [](const auto& path) { return path.generic_u8string(); });
    const auto canonical_root = std::filesystem::weakly_canonical(source_root);
    for (const auto& source : sources) {
      const import_asset_request request{
          .source_root = canonical_root, .imported_root = imported_root, .source_path = source};
      auto imported = import_project_asset(request);
      if (imported.result != import_asset_result::success) {
        return {.result = asset_index_result::invalid_format,
                .diagnostic = "重建时导入失败：" + path_utf8(source) + "：" + imported.diagnostic};
      }
      std::string content_hash;
      auto hashed = hash_source_file(source, content_hash);
      if (hashed.result != asset_index_result::success) {
        return hashed;
      }
      asset_index_entry entry{
          .source_path = path_utf8(
              std::filesystem::weakly_canonical(source).lexically_relative(canonical_root)),
          .source_key = imported.source_key,
          .importer_id = "gneiss.gltf",
          .importer_version = gltf_importer_version,
          .content_hash = std::move(content_hash),
          .state = asset_import_state::ready,
          .output_uris = std::move(imported.output_uris)};
      auto updated = upsert_asset_index_entry(rebuilt, std::move(entry));
      if (updated.result != asset_index_result::success) {
        return updated;
      }
    }
    return save_asset_index(index_path, rebuilt);
  } catch (const std::exception& error) {
    return {.result = asset_index_result::io_error,
            .diagnostic = std::string{"重建资产索引失败："} + error.what()};
  }
}

} // namespace gneiss::tooling::asset_import
