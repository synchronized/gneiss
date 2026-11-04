// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_PROJECT_DESCRIPTION_H_
#define GNEISS_APPS_COMMON_PROJECT_DESCRIPTION_H_

#include "engine/core/error.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::host {

enum class project_build_profile : std::uint8_t { debug, development, shipping };

/** 返回构建配置的稳定小写名称。 */
[[nodiscard]] std::string_view project_build_profile_name(project_build_profile profile) noexcept;

/** 工程环境光配置；asset 为空时使用内建中性环境。 */
struct environment_description final {
  std::string asset;
  float intensity{1.0F};
  float rotation_degrees{};
};

/** 经过校验的工程运行描述；路径均为规范绝对路径。 */
struct project_description final {
  std::filesystem::path project_file;
  std::filesystem::path project_root;
  std::filesystem::path asset_root;
  std::string name;
  std::string startup_scene;
  std::string input_map;
  environment_description environment;
  std::vector<std::string> retained_assets;
};

/** 工程加载失败阶段；用于宿主日志，不属于公共 Runtime ABI。 */
enum class project_load_stage {
  none,
  argument,
  project_root,
  project_file,
  document,
  schema,
  asset_root,
  startup_scene,
  input_map,
  environment,
  asset_build,
};

/** 工程加载报告；失败时保留结果、阶段和相关路径。 */
struct project_load_report final {
  gneiss::engine::error operation = gneiss::engine::error::success;
  project_load_stage stage = project_load_stage::none;
  std::filesystem::path context;
};

/** 返回适合结构化日志的稳定阶段名称。 */
[[nodiscard]] std::string_view project_load_stage_name(project_load_stage stage) noexcept;

/** 从工程根目录加载并校验固定名称的 gneiss.project.json。 */
[[nodiscard]] gneiss::engine::error
load_project_description(const std::filesystem::path& project_root, project_description& output,
                         project_load_report& report) noexcept;

/** 忽略分阶段报告并加载工程描述。 */
[[nodiscard]] inline gneiss::engine::error
load_project_description(const std::filesystem::path& project_root,
                         project_description& output) noexcept {
  project_load_report report;
  return load_project_description(project_root, output, report);
}

} // namespace gneiss::host

#endif
