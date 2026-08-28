// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_import_controller.h"

#include <string_view>

namespace gneiss::editor {
namespace {

[[nodiscard]] bool is_within(const std::filesystem::path& root,
                             const std::filesystem::path& candidate) {
  const auto relative = candidate.lexically_relative(root);
  return !relative.empty() && relative != "." && *relative.begin() != "..";
}

[[nodiscard]] bool is_supported(const std::filesystem::path& source) {
  const auto extension = source.extension().string();
  return extension == ".gltf" || extension == ".glb";
}

[[nodiscard]] editor_import_report failure(editor_import_result result, std::string diagnostic) {
  editor_import_report report;
  report.result = result;
  report.diagnostic = std::move(diagnostic);
  return report;
}

[[nodiscard]] std::filesystem::path unique_destination(const std::filesystem::path& source_root,
                                                       const std::filesystem::path& source) {
  auto destination = source_root / source.filename();
  for (std::uint32_t suffix = 1U; std::filesystem::exists(destination); ++suffix) {
    destination = source_root / (source.stem().string() + '-' + std::to_string(suffix) +
                                 source.extension().string());
  }
  return destination;
}

} // namespace

editor_import_report reimport_source_asset(const std::filesystem::path& project_root,
                                           const std::filesystem::path& asset_root,
                                           const std::filesystem::path& source_path) {
  if (project_root.empty() || asset_root.empty() || source_path.empty()) {
    return failure(editor_import_result::invalid_argument, "工程、资产和源文件路径不能为空");
  }
  try {
    const auto project = std::filesystem::weakly_canonical(project_root);
    const auto assets = std::filesystem::weakly_canonical(asset_root);
    const auto source_root = std::filesystem::weakly_canonical(project / "sources");
    const auto source = std::filesystem::weakly_canonical(source_path);
    if (!is_within(project, assets) || !is_within(project, source_root) ||
        !is_within(source_root, source) || !std::filesystem::is_regular_file(source)) {
      return failure(editor_import_result::invalid_argument, "源资产不在工程 sources 目录内");
    }
    if (!is_supported(source)) {
      return failure(editor_import_result::unsupported, "当前仅支持 glTF 和 GLB");
    }
    editor_import_report report;
    report.source_path = source;
    report.import = gneiss::tooling::asset_import::import_project_asset_and_update_index(
        {.source_root = source_root, .imported_root = assets / "imported", .source_path = source},
        project / ".gneiss" / "asset-index.json");
    if (report.import.result != gneiss::tooling::asset_import::import_asset_result::success) {
      report.result = editor_import_result::import_failed;
      report.diagnostic = report.import.diagnostic;
      return report;
    }
    report.result = editor_import_result::success;
    return report;
  } catch (const std::exception& error) {
    return failure(editor_import_result::io_error,
                   std::string{"重新导入源资产失败："} + error.what());
  }
}

editor_import_report import_external_asset(const std::filesystem::path& project_root,
                                           const std::filesystem::path& asset_root,
                                           const std::filesystem::path& external_source) {
  if (project_root.empty() || asset_root.empty() || external_source.empty()) {
    return failure(editor_import_result::invalid_argument, "工程、资产和外部源路径不能为空");
  }
  try {
    const auto project = std::filesystem::weakly_canonical(project_root);
    const auto source = std::filesystem::weakly_canonical(external_source);
    if (!std::filesystem::is_regular_file(source)) {
      return failure(editor_import_result::invalid_argument, "外部源文件不存在");
    }
    if (!is_supported(source)) {
      return failure(editor_import_result::unsupported, "当前仅支持 glTF 和 GLB");
    }
    const auto source_root = project / "sources";
    std::filesystem::create_directories(source_root);
    const auto canonical_source_root = std::filesystem::weakly_canonical(source_root);
    if (is_within(canonical_source_root, source)) {
      return reimport_source_asset(project, asset_root, source);
    }
    const auto destination = unique_destination(canonical_source_root, source);
    auto temporary = destination;
    temporary += ".gneiss-importing";
    std::error_code error;
    std::filesystem::remove(temporary, error);
    error.clear();
    std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::none, error);
    if (error) {
      return failure(editor_import_result::io_error, "复制源资产到暂存文件失败");
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
      std::filesystem::remove(temporary, error);
      return failure(editor_import_result::io_error, "提交工程源资产失败");
    }
    return reimport_source_asset(project, asset_root, destination);
  } catch (const std::exception& error) {
    return failure(editor_import_result::io_error,
                   std::string{"导入外部源资产失败："} + error.what());
  }
}

} // namespace gneiss::editor
