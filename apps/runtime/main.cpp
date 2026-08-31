// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/app/project_description.h>
#include <gneiss/application.hpp>
#include <gneiss/scene.h>

#include "runtime_log.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

namespace {

struct runtime_options final {
  std::filesystem::path project_root;
  std::filesystem::path log_file;
  bool smoke = false;
};

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
  return path.generic_string();
}

[[nodiscard]] bool parse_options(int argc, char** argv, runtime_options& output) {
  runtime_options pending;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--smoke") {
      pending.smoke = true;
      continue;
    }
    if (argument == "--project" && index + 1 < argc && pending.project_root.empty()) {
      pending.project_root = argv[++index];
      continue;
    }
    if (argument == "--log-file" && index + 1 < argc && pending.log_file.empty()) {
      pending.log_file = argv[++index];
      continue;
    }
    output = std::move(pending);
    return false;
  }
  if (pending.project_root.empty()) {
    output = std::move(pending);
    return false;
  }
  output = std::move(pending);
  return true;
}

gneiss_result update_runtime(gneiss_application, const gneiss_frame_time* time, void*) {
  return time == nullptr ? GNEISS_ERROR_INVALID_ARGUMENT : GNEISS_SUCCESS;
}

void report_application_diagnostic(gneiss_application, const gneiss_diagnostic* diagnostic,
                                   void* user_data) {
  if (diagnostic == nullptr || user_data == nullptr) {
    return;
  }
  const auto level = diagnostic->severity == GNEISS_DIAGNOSTIC_INFO      ? "INFO"
                     : diagnostic->severity == GNEISS_DIAGNOSTIC_WARNING ? "WARNING"
                                                                         : "ERROR";
  const std::string_view module{diagnostic->module, diagnostic->module_length};
  const std::string_view message{diagnostic->message, diagnostic->message_length};
  static_cast<gneiss::runtime_internal::runtime_log*>(user_data)->write(
      level, module, diagnostic->result, message);
}

[[nodiscard]] int run_runtime(const runtime_options& options,
                              gneiss::runtime_internal::runtime_log& log) {
  gneiss::app::project_description project;
  gneiss::app::project_load_report project_report;
  const auto project_result =
      gneiss::app::load_project_description(options.project_root, project, project_report);
  if (project_result != gneiss::result::success) {
    const auto context = path_text(project_report.context);
    log.write("ERROR", gneiss::app::project_load_stage_name(project_report.stage),
              static_cast<gneiss_result>(project_result), "工程加载失败", context);
    return 2;
  }
  const auto asset_root = path_text(project.asset_root);
  if (project.name.size() > std::numeric_limits<std::uint32_t>::max() ||
      asset_root.size() > std::numeric_limits<std::uint32_t>::max()) {
    log.write("ERROR", "project", GNEISS_ERROR_OUT_OF_MEMORY, "工程运行描述过长");
    return 2;
  }
  log.write("INFO", "project", GNEISS_SUCCESS, "工程加载完成", path_text(project.project_root));

  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &log;
  desc.update = update_runtime;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = project.name.data();
  desc.window_title_length = static_cast<std::uint32_t>(project.name.size());
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  desc.diagnostic = report_application_diagnostic;

  gneiss::application application;
  auto operation = gneiss::application::create(desc, application);
  if (operation != gneiss::result::success) {
    log.write("ERROR", "application_create", static_cast<gneiss_result>(operation),
              "Application 创建失败", asset_root);
    return 3;
  }
  log.write("INFO", "application_create", GNEISS_SUCCESS, "Application 创建完成");

  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  auto native_result = gneiss_scene_instance_load(application.get(), project.startup_scene.data(),
                                                  project.startup_scene.size(), &scene);
  if (native_result != GNEISS_SUCCESS) {
    log.write("ERROR", "startup_scene", native_result, "启动场景加载失败", project.startup_scene);
    return 4;
  }
  log.write("INFO", "startup_scene", GNEISS_SUCCESS, "启动场景加载完成", project.startup_scene);

  operation = application.run(options.smoke ? UINT64_C(3) : UINT64_C(0));
  if (operation != gneiss::result::success) {
    log.write("ERROR", "run", static_cast<gneiss_result>(operation), "Runtime 主循环失败");
    (void)gneiss_scene_instance_unload(application.get(), scene);
    return 5;
  }
  native_result = gneiss_scene_instance_unload(application.get(), scene);
  if (native_result != GNEISS_SUCCESS) {
    log.write("ERROR", "scene_unload", native_result, "启动场景卸载失败");
    return 6;
  }
  log.write("INFO", "shutdown", GNEISS_SUCCESS, "Runtime 已正常退出");
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  try {
    runtime_options options;
    const auto has_valid_options = parse_options(argc, argv, options);
    const auto log_path = options.log_file.empty()
                              ? gneiss::runtime_internal::default_runtime_log_path()
                              : options.log_file;
    gneiss::runtime_internal::runtime_log log(log_path);
    if (!log.is_file_available()) {
      log.write("WARNING", "log_file", GNEISS_ERROR_IO, "日志文件不可用，继续使用控制台日志",
                path_text(log.path()));
    }
    if (!has_valid_options) {
      log.write("ERROR", "arguments", GNEISS_ERROR_INVALID_ARGUMENT,
                "用法：gneiss_runtime --project <工程根> [--smoke] [--log-file <路径>]");
      return 64;
    }
    return run_runtime(options, log);
  } catch (...) {
    constexpr std::string_view fatal =
        "level=FATAL process=runtime stage=unhandled_exception result=-1 "
        "message=\"Runtime 未处理异常\" context=\"\"\n";
    std::fwrite(fatal.data(), sizeof(char), fatal.size(), stderr);
    std::fflush(stderr);
    return 99;
  }
}
