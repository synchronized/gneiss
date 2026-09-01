// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/app/project_description.h>
#include <gneiss/application.hpp>
#include <gneiss/scene.h>

#include "game/game_context_internal.h"
#include "game_module_session.h"
#include "game_update_scheduler.h"
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
  std::filesystem::path stop_file;
  bool smoke = false;
};

struct runtime_context final {
  gneiss::runtime_internal::runtime_log* log = nullptr;
  std::filesystem::path stop_file;
  std::uint64_t next_stop_check_ns = 0U;
  bool has_logged_first_frame = false;
  gneiss::game_module_session* game_module = nullptr;
  gneiss::runtime_internal::game_update_scheduler* game_scheduler = nullptr;
};

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
  return path.generic_string();
}

gneiss::result fixed_update_game(void* user_data, const gneiss_game_update_time& time) noexcept {
  return static_cast<gneiss::game_module_session*>(user_data)->fixed_update(time);
}

gneiss::result update_game(void* user_data, const gneiss_game_update_time& time) noexcept {
  return static_cast<gneiss::game_module_session*>(user_data)->update(time);
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
    if (argument == "--stop-file" && index + 1 < argc && pending.stop_file.empty()) {
      pending.stop_file = argv[++index];
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

gneiss_result update_runtime(gneiss_application application, const gneiss_frame_time* time,
                             void* user_data) {
  if (time == nullptr || user_data == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto& context = *static_cast<runtime_context*>(user_data);
  if (!context.has_logged_first_frame) {
    context.has_logged_first_frame = true;
    constexpr std::string_view category = "lifecycle";
    constexpr std::string_view message = "Runtime 已进入首帧";
    const gneiss_log_message entry = {
        .struct_size = sizeof(gneiss_log_message),
        .severity = GNEISS_LOG_INFO,
        .category = category.data(),
        .category_length = category.size(),
        .message = message.data(),
        .message_length = message.size(),
        .result = GNEISS_SUCCESS,
        .flags = 0U,
        .reserved = {},
    };
    const auto log_result = gneiss_application_log(application, &entry);
    if (log_result != GNEISS_SUCCESS) {
      return log_result;
    }
  }
  if (!context.stop_file.empty() && time->elapsed_ns >= context.next_stop_check_ns) {
    context.next_stop_check_ns = time->elapsed_ns + UINT64_C(100000000);
    std::error_code error;
    if (std::filesystem::exists(context.stop_file, error) && !error) {
      std::filesystem::remove(context.stop_file, error);
      if (context.log != nullptr) {
        context.log->write("INFO", "stop_request", GNEISS_SUCCESS, "收到 Editor 正常停止请求");
      }
      return gneiss_application_request_exit(application);
    }
  }
  if (context.game_module == nullptr || context.game_scheduler == nullptr) {
    return GNEISS_SUCCESS;
  }
  const gneiss::runtime_internal::game_update_callbacks callbacks{context.game_module,
                                                                  fixed_update_game, update_game};
  gneiss::runtime_internal::game_update_report report;
  const auto update_result = context.game_scheduler->advance(*time, callbacks, report);
  if ((report.was_frame_clamped || report.dropped_ns != 0U) && context.log != nullptr) {
    context.log->write("WARNING", "game_update", GNEISS_SUCCESS, "游戏模块更新积压已受限",
                       "accepted_ns=" + std::to_string(report.accepted_delta_ns) +
                           " dropped_ns=" + std::to_string(report.dropped_ns));
  }
  return static_cast<gneiss_result>(update_result);
}

void write_application_log(gneiss_application, const gneiss_log_event* event, void* user_data) {
  if (event == nullptr || user_data == nullptr) {
    return;
  }
  auto& context = *static_cast<runtime_context*>(user_data);
  if (context.log != nullptr) {
    context.log->write(*event);
  }
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

  runtime_context context{&log, options.stop_file};
  if (!context.stop_file.empty()) {
    std::error_code error;
    std::filesystem::remove(context.stop_file, error);
  }
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &context;
  desc.update = update_runtime;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = project.name.data();
  desc.window_title_length = static_cast<std::uint32_t>(project.name.size());
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  desc.log = write_application_log;

  gneiss::application application;
  auto operation = gneiss::application::create(desc, application);
  if (operation != gneiss::result::success) {
    log.write("ERROR", "application_create", static_cast<gneiss_result>(operation),
              "Application 创建失败", asset_root);
    return 3;
  }
  log.write("INFO", "application_create", GNEISS_SUCCESS, "Application 创建完成");

  if (!project.input_map.empty()) {
    const auto input_result = gneiss_application_load_action_map(
        application.get(), project.input_map.data(), project.input_map.size());
    if (input_result != GNEISS_SUCCESS) {
      log.write("ERROR", "input_map", input_result, "输入映射加载失败", project.input_map);
      return 4;
    }
    log.write("INFO", "input_map", GNEISS_SUCCESS, "输入映射加载完成", project.input_map);
  }

  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  auto native_result = gneiss_scene_instance_load(application.get(), project.startup_scene.data(),
                                                  project.startup_scene.size(), &scene);
  if (native_result != GNEISS_SUCCESS) {
    log.write("ERROR", "startup_scene", native_result, "启动场景加载失败", project.startup_scene);
    return 4;
  }
  log.write("INFO", "startup_scene", GNEISS_SUCCESS, "启动场景加载完成", project.startup_scene);

  gneiss_entity_id startup_root_entity = GNEISS_NULL_ENTITY_ID;
  uint64_t node_count = 0;
  native_result = gneiss_scene_instance_get_node_count(application.get(), scene, &node_count);
  for (uint64_t index = 0; native_result == GNEISS_SUCCESS && index < node_count; ++index) {
    gneiss_scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
    native_result = gneiss_scene_instance_get_node_info(application.get(), scene, index, &info);
    if (native_result == GNEISS_SUCCESS && info.parent == GNEISS_NULL_SCENE_NODE_ID) {
      startup_root_entity = info.entity;
      break;
    }
  }
  if (native_result != GNEISS_SUCCESS) {
    log.write("ERROR", "game_context", native_result, "启动场景根实体查询失败");
    (void)gneiss_scene_instance_unload(application.get(), scene);
    return 5;
  }
  gneiss_game_context game_context = GNEISS_NULL_GAME_CONTEXT;
  native_result = gneiss::game_internal::create_game_context(application.get(), startup_root_entity,
                                                             &game_context);
  if (native_result != GNEISS_SUCCESS) {
    log.write("ERROR", "game_context", native_result, "Game Context 创建失败");
    (void)gneiss_scene_instance_unload(application.get(), scene);
    return 5;
  }

  gneiss::game_module_session game_module;
  gneiss::runtime_internal::game_update_scheduler game_scheduler;
  if (!project.game_module.name.empty()) {
    std::filesystem::path module_path;
    operation = gneiss::app::resolve_game_module_path(project, module_path);
    if (operation == gneiss::result::success) {
      operation = game_module.load(module_path);
    }
    if (operation == gneiss::result::success) {
      operation = gneiss::from_native(gneiss::game_internal::set_game_context_log_source(
          game_context, game_module.module_id()));
    }
    if (operation == gneiss::result::success) {
      operation = game_module.initialize(game_context);
    }
    if (operation != gneiss::result::success) {
      log.write("ERROR", "game_module", static_cast<gneiss_result>(operation),
                "游戏模块加载或初始化失败", path_text(module_path));
      (void)gneiss::game_internal::destroy_game_context(game_context);
      (void)gneiss_scene_instance_unload(application.get(), scene);
      return 6;
    }
    context.game_module = &game_module;
    context.game_scheduler = &game_scheduler;
    log.write("INFO", "game_module", GNEISS_SUCCESS, "游戏模块初始化完成",
              std::string(game_module.module_id()));
  }

  operation = application.run(options.smoke ? UINT64_C(3) : UINT64_C(0));
  context.game_module = nullptr;
  context.game_scheduler = nullptr;
  if (operation != gneiss::result::success) {
    log.write("ERROR", "run", static_cast<gneiss_result>(operation), "Runtime 主循环失败");
    if (game_module.is_initialized()) {
      (void)game_module.shutdown();
    }
    (void)gneiss::game_internal::destroy_game_context(game_context);
    (void)gneiss_scene_instance_unload(application.get(), scene);
    return 7;
  }
  if (game_module.is_initialized()) {
    operation = game_module.shutdown();
    if (operation != gneiss::result::success) {
      log.write("ERROR", "game_module", static_cast<gneiss_result>(operation), "游戏模块关闭失败");
      (void)gneiss::game_internal::destroy_game_context(game_context);
      (void)gneiss_scene_instance_unload(application.get(), scene);
      return 8;
    }
  }
  native_result = gneiss::game_internal::destroy_game_context(game_context);
  if (native_result != GNEISS_SUCCESS) {
    log.write("ERROR", "game_context", native_result, "Game Context 销毁失败");
    (void)gneiss_scene_instance_unload(application.get(), scene);
    return 9;
  }
  native_result = gneiss_scene_instance_unload(application.get(), scene);
  if (native_result != GNEISS_SUCCESS) {
    log.write("ERROR", "scene_unload", native_result, "启动场景卸载失败");
    return 10;
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
                "用法：gneiss_runtime --project <工程根> [--smoke] [--log-file <路径>] "
                "[--stop-file <路径>]");
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
