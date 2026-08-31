// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_PROJECT_DESCRIPTION_H_
#define GNEISS_APPS_COMMON_PROJECT_DESCRIPTION_H_

#include <gneiss/core/result.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace gneiss::app {

struct game_module_description final {
  std::string name;
  std::filesystem::path directory;
  std::string build_preset;
  std::string build_target;
};

/** 经过校验的工程运行描述；路径均为规范绝对路径。 */
struct project_description final {
  std::filesystem::path project_file;
  std::filesystem::path project_root;
  std::filesystem::path asset_root;
  std::string name;
  std::string startup_scene;
  game_module_description game_module;
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
  game_module,
};

/** 工程加载报告；失败时保留结果、阶段和相关路径。 */
struct project_load_report final {
  result operation = result::success;
  project_load_stage stage = project_load_stage::none;
  std::filesystem::path context;
};

/** 返回适合结构化日志的稳定阶段名称。 */
[[nodiscard]] std::string_view project_load_stage_name(project_load_stage stage) noexcept;

/** 将平台无关模块基名映射为工程根内的本机动态库路径，并验证已有产物。 */
[[nodiscard]] result resolve_game_module_path(const project_description& project,
                                              std::filesystem::path& output) noexcept;

/** 从工程根目录加载并校验固定名称的 gneiss.project.json。 */
[[nodiscard]] result load_project_description(const std::filesystem::path& project_root,
                                              project_description& output,
                                              project_load_report& report) noexcept;

/** 忽略分阶段报告并加载工程描述。 */
[[nodiscard]] inline result load_project_description(const std::filesystem::path& project_root,
                                                     project_description& output) noexcept {
  project_load_report report;
  return load_project_description(project_root, output, report);
}

} // namespace gneiss::app

#endif
