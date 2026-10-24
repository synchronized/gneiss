// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_application.hpp"
#include "engine/core/rid.hpp"

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <gneiss/app/project_description.hpp>

#include "runtime_asset_reloader.hpp"
#include "runtime_ipc_session.hpp"
#include "runtime_log.hpp"
#include "runtime_property_editor.hpp"
#include "runtime_scene_inspection.hpp"
#include "runtime_scene_loader.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct runtime_options final {
  std::filesystem::path project_root;
  std::filesystem::path log_file;
  std::filesystem::path stop_file;
  std::string ipc_address;
  std::string ipc_token;
  std::uint16_t ipc_port = 0U;
  gneiss::host::game_build_profile profile = gneiss::host::game_build_profile::debug;
  bool smoke = false;
  bool hidden = false;
  bool cooperative_tasks = false;
};

struct runtime_context final {
  gneiss::host::runtime_log* log = nullptr;
  std::filesystem::path stop_file;
  std::uint64_t next_stop_check_ns = 0U;
  gneiss::runtime::application* application{};
  bool has_logged_first_frame = false;
  bool input_pending = false;
  std::string input_uri{};
  gneiss::host::runtime_ipc_session* ipc_session = nullptr;
  gneiss::host::runtime_scene_inspection* scene_inspection = nullptr;
  gneiss::host::runtime_property_editor* property_editor = nullptr;
  gneiss::host::runtime_asset_reloader* asset_reloader = nullptr;
  gneiss::engine::rid scene = gneiss::engine::null_rid;
  std::uint64_t next_inspection_ns = 0U;
  std::uint64_t inspection_session_id = 0U;
  std::uint64_t next_statistics_ns = 0U;
  std::uint64_t next_statistics_sequence = 1U;
  std::uint64_t fixed_update_count = 0U;
  bool force_full_inspection = false;
  gneiss::engine::result ipc_failure = gneiss::engine::result::success;
  gneiss::engine::task_scheduler* tasks{};
  std::deque<gneiss::host::runtime_ipc_actions::asset_reload_command> pending_assets{};
  gneiss::host::runtime_scene_loader* scene_loader{};
  std::uint64_t next_scene_progress_ns{};
  bool scene_terminal_sent{};
  bool scene_cleanup_sent{};
  bool smoke{};
  std::uint32_t smoke_frames{};
  bool scene_failed{};
  std::uint64_t logged_scene_session{};
  std::uint64_t logged_scene_revision{};
  int logged_scene_phase{-1};
  bool asset_waiting{};
  bool asset_cancelled{};
  std::uint64_t next_asset_progress_ns{};
};

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
  return path.generic_string();
}

gneiss::engine::result
apply_asset_revision(gneiss::runtime::application& application, gneiss::engine::rid scene,
                     std::string_view startup_scene,
                     std::span<const gneiss::host::ipc_asset_revision> assets) noexcept {
  try {
    if (assets.empty()) {
      return gneiss::engine::result::invalid_argument;
    }
    if (assets.front().type == gneiss::host::ipc_asset_type::scene) {
      if (assets.size() != 1U) {
        return gneiss::engine::result::invalid_argument;
      }
      if (assets.front().uri != startup_scene) {
        return gneiss::engine::result::success;
      }
      return application.reload_scene(scene, assets.front().uri);
    }
    if (assets.front().type == gneiss::host::ipc_asset_type::prefab) {
      if (assets.size() != 1U) {
        return gneiss::engine::result::invalid_argument;
      }
      const auto reloaded = application.reload_prefab(scene, assets.front().uri);
      return reloaded == gneiss::engine::result::not_found ? gneiss::engine::result::success
                                                           : reloaded;
    }
    // 网格、材质和纹理由异步入口处理；此处只保留结构重载适配。
    return gneiss::engine::result::invalid_argument;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

[[nodiscard]] bool parse_options(int argc, char** argv, runtime_options& output) {
  runtime_options pending;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--cooperative-tasks") {
      pending.cooperative_tasks = true;
      continue;
    }
    if (argument == "--hidden") {
      pending.hidden = true;
      continue;
    }
    if (argument == "--smoke") {
      pending.smoke = true;
      continue;
    }
    if (argument == "--profile" && index + 1 < argc) {
      const std::string_view profile = argv[++index];
      if (profile == "debug") {
        pending.profile = gneiss::host::game_build_profile::debug;
      } else if (profile == "development") {
        pending.profile = gneiss::host::game_build_profile::development;
      } else if (profile == "shipping") {
        pending.profile = gneiss::host::game_build_profile::shipping;
      } else {
        output = std::move(pending);
        return false;
      }
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
    if (argument == "--ipc-address" && index + 1 < argc && pending.ipc_address.empty()) {
      pending.ipc_address = argv[++index];
      continue;
    }
    if (argument == "--ipc-token" && index + 1 < argc && pending.ipc_token.empty()) {
      pending.ipc_token = argv[++index];
      continue;
    }
    if (argument == "--ipc-port" && index + 1 < argc && pending.ipc_port == 0U) {
      const std::string_view port = argv[++index];
      std::uint32_t parsed = 0U;
      const auto converted = std::from_chars(port.data(), port.data() + port.size(), parsed);
      if (converted.ec != std::errc{} || converted.ptr != port.data() + port.size() ||
          parsed == 0U || parsed > std::numeric_limits<std::uint16_t>::max()) {
        output = std::move(pending);
        return false;
      }
      pending.ipc_port = static_cast<std::uint16_t>(parsed);
      continue;
    }
    output = std::move(pending);
    return false;
  }
  const auto ipc_field_count = static_cast<unsigned int>(!pending.ipc_address.empty()) +
                               static_cast<unsigned int>(pending.ipc_port != 0U) +
                               static_cast<unsigned int>(!pending.ipc_token.empty());
  if (pending.project_root.empty() || (ipc_field_count != 0U && ipc_field_count != 3U)) {
    output = std::move(pending);
    return false;
  }
  output = std::move(pending);
  return true;
}

gneiss::engine::result update_runtime(const gneiss::engine::frame_time* time, void* user_data) try {
  if (time == nullptr || user_data == nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  auto& context = *static_cast<runtime_context*>(user_data);
  const auto request_exit = [&] {
    context.application->request_exit();
    return gneiss::engine::result::success;
  };
  if (context.tasks && context.tasks->mode() == gneiss::engine::execution_mode::cooperative) {
    (void)context.tasks->run_ready();
  }
  if (context.ipc_session != nullptr) {
    gneiss::host::runtime_ipc_actions actions;
    const auto ipc_result =
        context.ipc_session->pump(gneiss::host::runtime_ipc_session::clock::now(), actions);
    if (ipc_result != gneiss::engine::result::success) {
      context.ipc_failure = ipc_result;
      if (context.log != nullptr) {
        context.log->write("ERROR", "ipc", ipc_result, "Editor IPC 会话失败");
      }
      return request_exit();
    }
    if (actions.request_exit) {
      if (context.log != nullptr) {
        context.log->write("INFO", "ipc", gneiss::engine::result::success,
                           "收到 Editor IPC 停止请求");
      }
      return request_exit();
    }
    for (auto& command : actions.scene_commands) {
      auto accepted = gneiss::engine::result::not_ready;
      if (context.scene_loader) {
        if (command.cancel) {
          (void)context.scene_loader->cancel(command.request);
          context.next_scene_progress_ns = 0U;
          continue;
        }
        if (!context.asset_waiting && context.pending_assets.empty())
          accepted = context.scene_loader->request(command.request, command.request_id);
      }
      if (accepted != gneiss::engine::result::success) {
        const gneiss::host::ipc_scene_progress rejected{
            .source = command.request,
            .phase = gneiss::host::ipc_scene_phase::failed,
            .message = "场景切换未接受：已有事务或请求身份无效"};
        const auto sent = context.ipc_session->notify_scene_progress(rejected, command.request_id);
        if (sent != gneiss::engine::result::success)
          return sent;
      } else {
        context.scene_terminal_sent = false;
        context.scene_cleanup_sent = false;
        context.next_scene_progress_ns = 0U;
      }
    }
    if (actions.request_inspection_resync) {
      context.force_full_inspection = true;
    }
    if (!actions.asset_reloads.empty() && context.asset_reloader == nullptr) {
      context.ipc_failure = gneiss::engine::result::invalid_state;
      return request_exit();
    }
    for (auto& command : actions.asset_reloads) {
      if (context.pending_assets.size() >= 16U) {
        const gneiss::host::ipc_asset_reload_result rejected{
            .session_id = command.request.session_id,
            .revision = command.request.revision,
            .status = gneiss::host::ipc_asset_apply_status::failed,
            .message = "Runtime 资产队列已满"};
        const auto sent = context.ipc_session->notify_asset_reload_result(
            rejected, command.operation, command.request_id);
        if (sent != gneiss::engine::result::success) {
          context.ipc_failure = sent;
          return request_exit();
        }
      } else {
        context.pending_assets.push_back(std::move(command));
      }
    }
    for (const auto& cancel : actions.asset_cancels) {
      if (context.asset_waiting && !context.pending_assets.empty()) {
        const auto& active = context.pending_assets.front().request;
        if (active.session_id == cancel.session_id && active.revision == cancel.revision &&
            context.application->cancel_assets() == gneiss::engine::result::success)
          context.asset_cancelled = true;
        context.next_asset_progress_ns = 0U;
      }
    }
    if (!context.pending_assets.empty() && !context.application->visual_assets_loading() &&
        (!context.scene_loader || !context.scene_loader->busy())) {
      const auto& command = context.pending_assets.front();
      gneiss::host::ipc_asset_reload_result response;
      bool ready = true;
      auto asset_result = context.asset_waiting
                              ? context.asset_reloader->advance(response, ready)
                              : context.asset_reloader->execute(command.request, response);
      if (asset_result == gneiss::engine::result::not_ready) {
        context.asset_waiting = true;
        ready = false;
        asset_result = gneiss::engine::result::success;
      }
      if (asset_result == gneiss::engine::result::success && ready) {
        if (context.asset_cancelled) {
          response.status = gneiss::host::ipc_asset_apply_status::cancelled;
          response.message = "未完成的资产更新已取消，已生效内容保留";
        }
        context.asset_cancelled = false;
        context.next_asset_progress_ns = 0U;
        asset_result = context.ipc_session->notify_asset_reload_result(response, command.operation,
                                                                       command.request_id);
        context.pending_assets.pop_front();
        context.asset_waiting = false;
      }
      if (!ready && asset_result == gneiss::engine::result::success &&
          time->elapsed_ns >= context.next_asset_progress_ns) {
        gneiss::engine::asset_load_progress progress;
        if (context.application->asset_progress(progress)) {
          const gneiss::host::ipc_asset_reload_result event{
              .session_id = progress.session,
              .revision = progress.revision,
              .status = progress.can_cancel ? gneiss::host::ipc_asset_apply_status::preparing
                                            : gneiss::host::ipc_asset_apply_status::uploading,
              .message = progress.can_cancel ? "正在准备资产" : "正在上传资产，逐项生效",
              .can_cancel = progress.can_cancel && !context.asset_cancelled,
              .completed_assets = static_cast<std::uint32_t>(progress.completed_assets),
              .total_assets = static_cast<std::uint32_t>(progress.total_assets)};
          const auto sent = context.ipc_session->notify_asset_reload_result(
              event, gneiss::host::ipc_asset_operation::progress, 0U);
          if (sent != gneiss::engine::result::not_ready)
            asset_result = sent;
        }
        context.next_asset_progress_ns = time->elapsed_ns + UINT64_C(100000000);
      }
      if (asset_result != gneiss::engine::result::success) {
        context.ipc_failure = asset_result;
        return request_exit();
      }
    }
    if (!actions.property_writes.empty() && context.property_editor == nullptr) {
      context.ipc_failure = gneiss::engine::result::invalid_state;
      return request_exit();
    }
    for (const auto& command : actions.property_writes) {
      gneiss::host::ipc_property_write_result response;
      auto property_result = context.property_editor->execute(command, response);
      if (property_result == gneiss::engine::result::success) {
        property_result = context.ipc_session->notify_property_write_result(response);
      }
      if (property_result != gneiss::engine::result::success) {
        context.ipc_failure = property_result;
        if (context.log != nullptr) {
          context.log->write("ERROR", "property_edit", property_result,
                             "Runtime 属性写入命令执行失败");
        }
        return request_exit();
      }
    }
  }
  // 等待输入映射时仍处理 IPC、停止文件和窗口事件；场景激活会初始化游戏，必须晚于输入就绪。
  if (context.input_pending) {
    const auto input_result = context.application->action_map_status();
    if (input_result != gneiss::engine::result::not_ready) {
      context.input_pending = false;
      if (context.log)
        context.log->write(input_result == gneiss::engine::result::success ? "INFO" : "ERROR",
                           "input_map", input_result,
                           input_result == gneiss::engine::result::success ? "输入映射加载完成"
                                                                           : "输入映射加载失败",
                           context.input_uri);
      if (input_result != gneiss::engine::result::success)
        return input_result;
    }
  }
  if (context.scene_loader && !context.input_pending) {
    const auto advanced = context.scene_loader->advance();
    if (advanced != gneiss::engine::result::success)
      return advanced;
    const auto& progress = context.scene_loader->progress();
    const auto phase = static_cast<int>(progress.phase);
    if (context.log && (context.logged_scene_session != progress.source.session ||
                        context.logged_scene_revision != progress.source.revision ||
                        context.logged_scene_phase != phase)) {
      context.logged_scene_session = progress.source.session;
      context.logged_scene_revision = progress.source.revision;
      context.logged_scene_phase = phase;
      context.log->write(progress.phase == gneiss::host::ipc_scene_phase::failed ? "ERROR" : "INFO",
                         "scene_load", context.scene_loader->last_result(), progress.message,
                         progress.source.uri);
    }
    const bool terminal = gneiss::host::scene_phase_terminal(progress.phase);
    if (!terminal) {
      context.scene_terminal_sent = false;
      context.scene_cleanup_sent = false;
    }
    const bool cleaned = progress.budget && progress.budget->cleanup_complete;
    if (context.ipc_session && time->elapsed_ns >= context.next_scene_progress_ns &&
        (!terminal || !context.scene_terminal_sent || (cleaned && !context.scene_cleanup_sent))) {
      const auto sent = context.ipc_session->notify_scene_progress(
          progress,
          terminal && context.scene_terminal_sent ? 0U : context.scene_loader->request_id());
      if (sent == gneiss::engine::result::success) {
        context.next_scene_progress_ns = time->elapsed_ns + UINT64_C(100000000);
        context.scene_terminal_sent = terminal;
        context.scene_cleanup_sent = cleaned;
      } else if (sent != gneiss::engine::result::not_ready)
        return sent;
    }
    context.scene_failed = progress.phase == gneiss::host::ipc_scene_phase::failed;
    if (context.smoke && terminal &&
        (context.scene_failed ||
         (!context.application->visual_assets_loading() && ++context.smoke_frames >= 3U)))
      return request_exit();
  }
  if (!context.has_logged_first_frame) {
    context.has_logged_first_frame = true;
    constexpr std::string_view category = "lifecycle";
    constexpr std::string_view message = "Runtime 已进入首帧";
    const auto log_result =
        context.application->submit_log({.severity = gneiss::engine::level::info,
                                         .source = "application",
                                         .category = category,
                                         .message = message});
    if (log_result.failed()) {
      return log_result;
    }
  }
  if (context.ipc_session != nullptr && context.scene_inspection != nullptr &&
      context.scene != gneiss::engine::null_rid && time->elapsed_ns >= context.next_inspection_ns &&
      context.ipc_session->supports_domain(gneiss::engine::ipc_domain::inspection)) {
    context.next_inspection_ns = time->elapsed_ns + UINT64_C(100000000);
    gneiss::host::runtime_scene_snapshot snapshot;
    const auto captured = context.scene_inspection->capture_scene(
        *context.application, context.scene, context.force_full_inspection, snapshot);
    if (captured == gneiss::engine::result::success) {
      const auto sent = context.ipc_session->notify_scene_snapshot(snapshot);
      if (sent == gneiss::engine::result::success) {
        context.force_full_inspection = false;
      } else if (sent == gneiss::engine::result::not_ready) {
        context.force_full_inspection = true;
      } else {
        context.ipc_failure = sent;
        return request_exit();
      }
    } else if (captured != gneiss::engine::result::not_ready) {
      context.ipc_failure = captured;
      return request_exit();
    }
  }
  if (context.ipc_session != nullptr && context.scene != gneiss::engine::null_rid &&
      context.inspection_session_id != 0U && time->elapsed_ns >= context.next_statistics_ns) {
    context.next_statistics_ns = time->elapsed_ns + UINT64_C(250000000);
    std::uint64_t scene_node_count = 0U;
    std::uint64_t entity_count = 0U;
    auto operation =
        context.application->scenes()->get_node_count(context.scene, &scene_node_count);
    if (operation == gneiss::engine::result::success) {
      operation = context.application->native_world()->entity_count(entity_count);
    }
    if (operation != gneiss::engine::result::success) {
      context.ipc_failure = operation;
      return request_exit();
    }
    const gneiss::host::ipc_runtime_statistics statistics{
        .session_id = context.inspection_session_id,
        .sequence = context.next_statistics_sequence++,
        .frame_index = time->frame_index,
        .frame_delta_ns = time->delta_ns,
        .fixed_update_count = context.fixed_update_count,
        .scene_node_count = scene_node_count,
        .entity_count = entity_count,
        .ipc_pending_writes = context.ipc_session->pending_write_count(),
        .ipc_dropped_events = context.ipc_session->dropped_event_count()};
    const auto sent = context.ipc_session->notify_statistics(statistics);
    if (sent != gneiss::engine::result::success && sent != gneiss::engine::result::not_ready) {
      context.ipc_failure = sent;
      return request_exit();
    }
  }
  if (!context.stop_file.empty() && time->elapsed_ns >= context.next_stop_check_ns) {
    context.next_stop_check_ns = time->elapsed_ns + UINT64_C(100000000);
    std::error_code error;
    if (std::filesystem::exists(context.stop_file, error) && !error) {
      std::filesystem::remove(context.stop_file, error);
      if (context.log != nullptr) {
        context.log->write("INFO", "stop_request", gneiss::engine::result::success,
                           "收到 Editor 正常停止请求");
      }
      return request_exit();
    }
  }
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

void write_application_log(const gneiss::engine::event_view& source, runtime_context& context) {
  if (context.log != nullptr) {
    context.log->write(source);
  }
  if (context.ipc_session != nullptr) {
    (void)context.ipc_session->notify_log_event(source);
  }
}

[[nodiscard]] int run_runtime(const runtime_options& options, gneiss::host::runtime_log& log) {
  gneiss::host::project_description project;
  gneiss::host::project_load_report project_report;
  const auto project_result =
      gneiss::host::load_project_description(options.project_root, project, project_report);
  if (project_result != gneiss::engine::result::success) {
    const auto context = path_text(project_report.context);
    log.write("ERROR", gneiss::host::project_load_stage_name(project_report.stage),
              static_cast<gneiss::engine::result>(project_result), "工程加载失败", context);
    return 2;
  }
  const auto asset_root = path_text(project.asset_root);
  if (project.name.size() > std::numeric_limits<std::uint32_t>::max() ||
      asset_root.size() > std::numeric_limits<std::uint32_t>::max()) {
    log.write("ERROR", "project", gneiss::engine::result::out_of_memory, "工程运行描述过长");
    return 2;
  }
  log.write("INFO", "project", gneiss::engine::result::success, "工程加载完成",
            path_text(project.project_root));

  runtime_context context{&log, options.stop_file};
  if (!context.stop_file.empty()) {
    std::error_code error;
    std::filesystem::remove(context.stop_file, error);
  }
  gneiss::runtime::application_configuration desc;
  desc.callbacks.update = [&context](const gneiss::engine::frame_time& time) {
    return update_runtime(&time, &context);
  };
  desc.use_granit_window = true;
  desc.window.title = project.name;
  desc.window.visible =
      !options.smoke && !options.hidden; // 自动冒烟创建真实隐藏窗口，避免抢占桌面。

  desc.asset_root = asset_root;

  desc.callbacks.log = [&context](const gneiss::engine::event_view& event) {
    write_application_log(event, context);
  };
  desc.environment_asset = project.environment.asset;

  desc.environment_intensity = project.environment.intensity;
  desc.environment_rotation_radians =
      project.environment.rotation_degrees * 0.01745329251994329577F;

  gneiss::engine::task_scheduler task_scheduler(
      {.mode = options.cooperative_tasks ? gneiss::engine::execution_mode::cooperative
                                         : gneiss::engine::execution_mode::thread_pool});
  context.tasks = &task_scheduler;
  std::shared_ptr<gneiss::runtime::application> application;
  auto operation = gneiss::runtime::application::create(desc, application);
  if (operation != gneiss::engine::result::success) {
    log.write("ERROR", "application_create", static_cast<gneiss::engine::result>(operation),
              "Application 创建失败", asset_root);
    return 3;
  }
  context.application = application.get();
  log.write("INFO", "application_create", gneiss::engine::result::success, "Application 创建完成");
  if (application->attach_task_executor(task_scheduler) != gneiss::engine::result::success) {
    return 3;
  }

  if (!project.input_map.empty()) {
    auto input_result = application->begin_action_map(project.input_map);
    if (input_result == gneiss::engine::result::success) {
      context.input_pending = true;
      context.input_uri = project.input_map;
      log.write("INFO", "input_map_pending", gneiss::engine::result::success,
                "正在异步加载输入映射", project.input_map);
    }
    if (input_result != gneiss::engine::result::success) {
      log.write("ERROR", "input_map", input_result, "输入映射加载失败", project.input_map);
      return 4;
    }
  }

  gneiss::engine::rid scene = gneiss::engine::null_rid;
  auto native_result = gneiss::engine::result::success;
  std::string active_scene_uri = project.startup_scene;
  const auto inspection_serial =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  gneiss::host::runtime_scene_inspection scene_inspection(
      inspection_serial == 0U ? 1U : inspection_serial);
  context.scene_inspection = &scene_inspection;
  context.inspection_session_id = inspection_serial == 0U ? 1U : inspection_serial;
  context.scene = scene;
  gneiss::host::runtime_asset_reloader asset_reloader(
      [native_application = application.get(), &scene,
       &active_scene_uri](std::span<const gneiss::host::ipc_asset_revision> assets) {
        return apply_asset_revision(*native_application, scene, active_scene_uri, assets);
      },
      [native_application =
           application.get()](const gneiss::host::ipc_asset_reload_request& request) {
        std::vector<gneiss::engine::render_asset_reload> assets;
        for (const auto& asset : request.assets) {
          using type = gneiss::engine::render_asset_type;
          assets.push_back(
              {asset.uri, asset.type == gneiss::host::ipc_asset_type::texture    ? type::texture
                          : asset.type == gneiss::host::ipc_asset_type::material ? type::material
                                                                                 : type::mesh});
        }
        std::uint64_t accepted{};
        return native_application->request_assets(assets, request.session_id, request.revision,
                                                  accepted, true);
      },
      [native_application = application.get(),
       &log](gneiss::engine::result& result,
             std::vector<gneiss::host::ipc_asset_item_result>& items, bool& ready) {
        gneiss::engine::texture_load_completion completion;
        const auto operation = native_application->poll_assets(completion, ready);
        if (ready) {
          for (const auto& item : completion.results) {
            using type = gneiss::engine::render_asset_type;
            items.push_back({.asset = {.uri = item.source.uri,
                                       .type = item.source.type == type::texture
                                                   ? gneiss::host::ipc_asset_type::texture
                                               : item.source.type == type::material
                                                   ? gneiss::host::ipc_asset_type::material
                                                   : gneiss::host::ipc_asset_type::static_mesh},
                             .operation = item.result,
                             .applied = item.applied});
          }
          result = completion.result;
          const auto metrics = "prepare_ms=" + std::to_string(completion.prepare_ms) +
                               " commit_ms=" + std::to_string(completion.commit_ms) +
                               " prepared_bytes=" + std::to_string(completion.prepared_bytes) +
                               " upload_ms=" + std::to_string(completion.upload_ms) +
                               " queue_ms=" + std::to_string(completion.queue_ms) + " " +
                               completion.message;
          log.write(completion.result == gneiss::engine::result::success ? "INFO" : "ERROR",
                    "texture_load", completion.result, metrics);
        }
        return operation;
      });
  context.asset_reloader = &asset_reloader;

  gneiss::engine::rid world = gneiss::engine::null_rid;
  world = application->world();
  native_result = gneiss::engine::result::success;
  gneiss::host::runtime_property_editor property_editor;
  if (native_result == gneiss::engine::result::success) {
    operation = property_editor.initialize(world, scene_inspection, context.inspection_session_id);
  } else {
    operation = native_result;
  }
  if (operation != gneiss::engine::result::success) {
    log.write("ERROR", "property_edit", operation, "Runtime 属性写入执行器初始化失败");
    (void)application->scenes()->unload(scene);
    return 5;
  }
  context.property_editor = &property_editor;

  gneiss::host::runtime_scene_loader scene_loader(*application);
  context.scene_loader = &scene_loader;
  context.smoke = options.smoke;
  scene_loader.after_activate =
      [&](const gneiss::runtime::scene_load_completion& loaded) -> gneiss::engine::result {
    const auto started = std::chrono::steady_clock::now();
    scene = loaded.scene;
    context.scene = scene;
    active_scene_uri = scene_loader.progress().source.uri;
    property_editor.reset();
    ++context.inspection_session_id;
    scene_inspection.reset(context.inspection_session_id);
    if (context.ipc_session)
      context.ipc_session->discard_pending_inspection();
    context.force_full_inspection = true;
    context.next_inspection_ns = 0U;
    context.next_statistics_sequence = 1U;
    context.fixed_update_count = 0U;
    world = application->world();
    auto applied = gneiss::engine::result::success;
    if (applied != gneiss::engine::result::success)
      return applied;
    const auto initialized =
        property_editor.initialize(world, scene_inspection, context.inspection_session_id);
    if (initialized != gneiss::engine::result::success)
      return initialized;
    log.write("INFO", "startup_scene", gneiss::engine::result::success, "场景加载完成",
              active_scene_uri);
    log.write("INFO", "scene_load", gneiss::engine::result::success,
              "prepare_ms=" + std::to_string(loaded.prepare_ms) +
                  " asset_prepare_ms=" + std::to_string(loaded.asset_prepare_ms) +
                  " activation_ms=" + std::to_string(loaded.activation_ms) +
                  " inspection_rebind_ms=" +
                  std::to_string(std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - started)
                                     .count()));
    return gneiss::engine::result::success;
  };

  std::unique_ptr<gneiss::host::runtime_ipc_session> ipc_session;
  if (options.ipc_port != 0U) {
    gneiss::host::runtime_ipc_config ipc_config;
    ipc_config.endpoint = {options.ipc_address, options.ipc_port};
    ipc_config.session_token = options.ipc_token;
    ipc_session = std::make_unique<gneiss::host::runtime_ipc_session>(std::move(ipc_config));
    operation = ipc_session->start(gneiss::host::runtime_ipc_session::clock::now());
    if (operation == gneiss::engine::result::success) {
      operation = ipc_session->notify_running();
    }
    if (operation != gneiss::engine::result::success) {
      log.write("ERROR", "ipc", operation, "Editor IPC 会话启动失败");

      (void)application->scenes()->unload(scene);
      return 11;
    }
    context.ipc_session = ipc_session.get();
    log.write("INFO", "ipc", gneiss::engine::result::success, "Editor IPC 会话正在连接");
  }

  native_result =
      scene_loader.request({context.inspection_session_id, 1U, project.startup_scene}, 0U);
  if (native_result != gneiss::engine::result::success)
    return 4;
  operation = application->run(0U);
  const auto task_stats = task_scheduler.stats();
  log.write("INFO", "task_statistics", gneiss::engine::result::success,
            "submitted=" + std::to_string(task_stats.submitted) +
                " completed=" + std::to_string(task_stats.completed) +
                " retained=" + std::to_string(task_stats.retained));
  if (context.ipc_session != nullptr && context.ipc_failure == gneiss::engine::result::success) {
    (void)context.ipc_session->notify_shutdown(
        operation == gneiss::engine::result::success ? 0 : operation.code());
  }
  context.ipc_session = nullptr;
  context.asset_reloader = nullptr;
  context.property_editor = nullptr;
  context.scene_inspection = nullptr;
  context.scene = gneiss::engine::null_rid;
  if (ipc_session) {
    (void)ipc_session->stop();
  }
  if (operation != gneiss::engine::result::success) {
    log.write("ERROR", "run", static_cast<gneiss::engine::result>(operation), "Runtime 主循环失败");

    (void)application->scenes()->unload(scene);
    return 7;
  }
  if (context.ipc_failure != gneiss::engine::result::success) {
    log.write("ERROR", "ipc", context.ipc_failure, "Runtime 因 Editor IPC 会话失败而退出");

    (void)application->scenes()->unload(scene);
    return 11;
  }

  native_result = scene == gneiss::engine::null_rid ? gneiss::engine::result::success
                                                    : application->scenes()->unload(scene);
  if (native_result != gneiss::engine::result::success) {
    log.write("ERROR", "scene_unload", native_result, "启动场景卸载失败");
    return 10;
  }
  if (context.scene_failed && options.smoke)
    return 4;
  log.write("INFO", "shutdown", gneiss::engine::result::success, "Runtime 已正常退出");
  return 0;
}

} // namespace

int gneiss::host::run_runtime_application(int argc, char** argv) {
  try {
    runtime_options options;
    const auto has_valid_options = parse_options(argc, argv, options);
    const auto log_path =
        options.log_file.empty() ? gneiss::host::default_runtime_log_path() : options.log_file;
    gneiss::host::runtime_log log(log_path);
    if (!log.is_file_available()) {
      log.write("WARNING", "log_file", gneiss::engine::result::io,
                "日志文件不可用，继续使用控制台日志", path_text(log.path()));
    }
    if (!has_valid_options) {
      log.write("ERROR", "arguments", gneiss::engine::result::invalid_argument,
                "用法：gneiss_runtime --project <工程根> [--smoke] [--hidden] [--log-file <路径>] "
                "[--stop-file <路径>] [--ipc-address <地址> --ipc-port <端口> "
                "--ipc-token <令牌>]");
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
