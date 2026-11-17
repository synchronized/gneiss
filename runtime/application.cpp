// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/logging/default_service.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/scene/scene_instance.hpp>
#include <deque>

#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/project/project_description.hpp>

#include "file_sink.hpp"
#include "assets/runtime_asset_reloader.hpp"
#include "ipc/runtime_ipc_session.hpp"
#include "runtime_logging.hpp"
#include "inspection/runtime_property_editor.hpp"
#include "inspection/runtime_scene_inspection.hpp"
#include "scene/runtime_scene_loader.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct runtime_options final {
  std::filesystem::path project_root;
  std::filesystem::path log_file;
  std::filesystem::path stop_file;
  std::string ipc_address;
  std::string ipc_token;
  std::uint16_t ipc_port = 0U;
  bool smoke = false;
  bool hidden = false;
  bool cooperative_tasks = false;
};

struct runtime_context final {
  gneiss::logging::logger* log = nullptr;
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
  gneiss::scene_instance scene = {};
  std::uint64_t next_inspection_ns = 0U;
  std::uint64_t inspection_session_id = 0U;
  std::uint64_t next_statistics_ns = 0U;
  std::uint64_t next_statistics_sequence = 1U;
  bool force_full_inspection = false;
  gneiss::error ipc_failure = gneiss::error::success;
  gneiss::tasks::task_scheduler* tasks{};
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
  gneiss::asset_batch_request asset_request{};
  bool asset_waiting{};
  bool asset_cancelled{};
  std::uint64_t next_asset_progress_ns{};
};

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
  return path.generic_string();
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

gneiss::error update_runtime(const gneiss::frame_time* time, void* user_data) {
  if (time == nullptr || user_data == nullptr) {
    return gneiss::error::invalid_argument;
  }
  auto& context = *static_cast<runtime_context*>(user_data);
  const auto request_exit = [&] {
    context.application->request_exit();
    return gneiss::error::success;
  };
  if (context.tasks && context.tasks->mode() == gneiss::tasks::execution_mode::cooperative) {
    (void)context.tasks->run_ready();
  }
  if (context.ipc_session != nullptr) {
    gneiss::host::runtime_ipc_actions actions;
    const auto ipc_result =
        context.ipc_session->pump(gneiss::host::runtime_ipc_session::clock::now(), actions);
    if (ipc_result != gneiss::error::success) {
      context.ipc_failure = ipc_result;
      if (context.log != nullptr) {
        context.log->write({.severity = gneiss::logging::level::error,
                            .category = "ipc",
                            .message = "Editor IPC 会话失败",
                            .result = ipc_result});
      }
      return request_exit();
    }
    if (actions.request_exit) {
      if (context.log != nullptr) {
        context.log->write({.severity = gneiss::logging::level::info,
                            .category = "ipc",
                            .message = "收到 Editor IPC 停止请求",
                            .result = gneiss::error::success});
      }
      return request_exit();
    }
    for (auto& command : actions.scene_commands) {
      auto accepted = gneiss::error::not_ready;
      if (context.scene_loader) {
        if (command.cancel) {
          (void)context.scene_loader->cancel(command.request);
          context.next_scene_progress_ns = 0U;
          continue;
        }
        if (!context.asset_waiting && context.pending_assets.empty())
          accepted = context.scene_loader->request(command.request, command.request_id);
      }
      if (accepted != gneiss::error::success) {
        const gneiss::host::ipc_scene_progress rejected{
            .source = command.request,
            .phase = gneiss::host::ipc_scene_phase::failed,
            .message = "场景切换未接受：已有事务或请求身份无效"};
        const auto sent = context.ipc_session->notify_scene_progress(rejected, command.request_id);
        if (sent != gneiss::error::success)
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
      context.ipc_failure = gneiss::error::invalid_state;
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
        if (sent != gneiss::error::success) {
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
            bool(context.asset_request) && context.asset_request.cancel() == gneiss::error::success)
          context.asset_cancelled = true;
        context.next_asset_progress_ns = 0U;
      }
    }
    if (!context.pending_assets.empty() &&
        (!context.scene_loader ||
         (!context.scene_loader->busy() && !context.scene_loader->visual_loading()))) {
      const auto& command = context.pending_assets.front();
      gneiss::host::ipc_asset_reload_result response;
      bool ready = true;
      auto asset_result = context.asset_waiting
                              ? context.asset_reloader->advance(response, ready)
                              : context.asset_reloader->execute(command.request, response);
      if (asset_result == gneiss::error::not_ready) {
        context.asset_waiting = true;
        ready = false;
        asset_result = gneiss::error::success;
      }
      if (asset_result == gneiss::error::success && ready) {
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
      if (!ready && asset_result == gneiss::error::success &&
          time->elapsed_ns >= context.next_asset_progress_ns) {
        gneiss::asset_load_progress progress;
        if (auto observed = context.asset_request.progress()) {
          progress = *observed;
          const gneiss::host::ipc_asset_reload_result event{
              .session_id = command.request.session_id,
              .revision = command.request.revision,
              .status = progress.can_cancel ? gneiss::host::ipc_asset_apply_status::preparing
                                            : gneiss::host::ipc_asset_apply_status::uploading,
              .message = progress.can_cancel ? "正在准备资产" : "正在上传资产，逐项生效",
              .can_cancel = progress.can_cancel && !context.asset_cancelled,
              .completed_assets = static_cast<std::uint32_t>(progress.completed_assets),
              .total_assets = static_cast<std::uint32_t>(progress.total_assets)};
          const auto sent = context.ipc_session->notify_asset_reload_result(
              event, gneiss::host::ipc_asset_operation::progress, 0U);
          if (sent != gneiss::error::not_ready)
            asset_result = sent;
        }
        context.next_asset_progress_ns = time->elapsed_ns + UINT64_C(100000000);
      }
      if (asset_result != gneiss::error::success) {
        context.ipc_failure = asset_result;
        return request_exit();
      }
    }
    if (!actions.property_writes.empty() && context.property_editor == nullptr) {
      context.ipc_failure = gneiss::error::invalid_state;
      return request_exit();
    }
    for (const auto& command : actions.property_writes) {
      gneiss::host::ipc_property_write_result response;
      auto property_result = context.property_editor->execute(command, response);
      if (property_result == gneiss::error::success) {
        property_result = context.ipc_session->notify_property_write_result(response);
      }
      if (property_result != gneiss::error::success) {
        context.ipc_failure = property_result;
        if (context.log != nullptr) {
          context.log->write({.severity = gneiss::logging::level::error,
                              .category = "property_edit",
                              .message = "Runtime 属性写入命令执行失败",
                              .result = property_result});
        }
        return request_exit();
      }
    }
  }
  // 等待输入映射时仍处理 IPC、停止文件和窗口事件；场景激活会初始化游戏，必须晚于输入就绪。
  if (context.input_pending) {
    const auto input_result = context.application->action_map_status();
    if (input_result != gneiss::error::not_ready) {
      context.input_pending = false;
      if (context.log)
        context.log->write(
            {.severity = input_result == gneiss::error::success ? gneiss::logging::level::info
                                                                : gneiss::logging::level::error,
             .category = "input_map",
             .message = std::format("{} ({})",
                                    input_result == gneiss::error::success ? "输入映射加载完成"
                                                                           : "输入映射加载失败",
                                    context.input_uri),
             .result = input_result});
      if (input_result != gneiss::error::success)
        return input_result;
    }
  }
  if (context.scene_loader && !context.input_pending) {
    const auto advanced = context.scene_loader->advance();
    if (advanced != gneiss::error::success)
      return advanced;
    const auto& progress = context.scene_loader->progress();
    const auto phase = static_cast<int>(progress.phase);
    if (context.log && (context.logged_scene_session != progress.source.session ||
                        context.logged_scene_revision != progress.source.revision ||
                        context.logged_scene_phase != phase)) {
      context.logged_scene_session = progress.source.session;
      context.logged_scene_revision = progress.source.revision;
      context.logged_scene_phase = phase;
      context.log->write({.severity = progress.phase == gneiss::host::ipc_scene_phase::failed
                                          ? gneiss::logging::level::error
                                          : gneiss::logging::level::info,
                          .category = "scene_load",
                          .message = std::format("{} ({})", progress.message, progress.source.uri),
                          .result = context.scene_loader->last_result()});
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
      if (sent == gneiss::error::success) {
        context.next_scene_progress_ns = time->elapsed_ns + UINT64_C(100000000);
        context.scene_terminal_sent = terminal;
        context.scene_cleanup_sent = cleaned;
      } else if (sent != gneiss::error::not_ready)
        return sent;
    }
    context.scene_failed = progress.phase == gneiss::host::ipc_scene_phase::failed;
    if (context.smoke && terminal &&
        (context.scene_failed ||
         (!context.scene_loader->visual_loading() && ++context.smoke_frames >= 3U)))
      return request_exit();
  }
  if (!context.has_logged_first_frame) {
    context.has_logged_first_frame = true;
    constexpr std::string_view category = "lifecycle";
    constexpr std::string_view message = "Runtime 已进入首帧";
    gneiss::logging::logger{"application"}.write({.severity = gneiss::logging::level::info,
                                                  .source = "application",
                                                  .category = category,
                                                  .message = message});
  }
  if (context.ipc_session != nullptr && context.scene_inspection != nullptr &&
      context.scene != gneiss::scene_instance{} && time->elapsed_ns >= context.next_inspection_ns &&
      context.ipc_session->supports_domain(gneiss::ipc_domain::inspection)) {
    context.next_inspection_ns = time->elapsed_ns + UINT64_C(100000000);
    gneiss::host::runtime_scene_snapshot snapshot;
    const auto captured = context.scene_inspection->capture_scene(
        *context.application, context.scene, context.force_full_inspection, snapshot);
    if (captured == gneiss::error::success) {
      const auto sent = context.ipc_session->notify_scene_snapshot(snapshot);
      if (sent == gneiss::error::success) {
        context.force_full_inspection = false;
      } else if (sent == gneiss::error::not_ready) {
        context.force_full_inspection = true;
      } else {
        context.ipc_failure = sent;
        return request_exit();
      }
    } else if (captured != gneiss::error::not_ready) {
      context.ipc_failure = captured;
      return request_exit();
    }
  }
  if (context.ipc_session != nullptr && context.scene != gneiss::scene_instance{} &&
      context.inspection_session_id != 0U && time->elapsed_ns >= context.next_statistics_ns) {
    context.next_statistics_ns = time->elapsed_ns + UINT64_C(250000000);
    if (!context.scene.is_valid()) {
      context.ipc_failure = gneiss::error::invalid_handle;
      return request_exit();
    }
    const auto scene_node_count = context.scene.size();
    const auto entity_count = context.application->native_world()->entity_count();
    const gneiss::host::ipc_runtime_statistics statistics{
        .session_id = context.inspection_session_id,
        .sequence = context.next_statistics_sequence++,
        .frame_index = time->frame_index,
        .frame_delta_ns = time->delta_ns,
        .scene_node_count = scene_node_count,
        .entity_count = entity_count,
        .ipc_pending_writes = context.ipc_session->pending_write_count(),
        .ipc_dropped_events = context.ipc_session->dropped_event_count()};
    const auto sent = context.ipc_session->notify_statistics(statistics);
    if (sent != gneiss::error::success && sent != gneiss::error::not_ready) {
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
        context.log->write({.severity = gneiss::logging::level::info,
                            .category = "stop_request",
                            .message = "收到 Editor 正常停止请求",
                            .result = gneiss::error::success});
      }
      return request_exit();
    }
  }
  return gneiss::error::success;
}

class ipc_log_sink final : public gneiss::logging::sink {
public:
  explicit ipc_log_sink(gneiss::host::runtime_ipc_session& session) : session_(session) {}
  gneiss::error write(const gneiss::logging::record_view& value) override {
    return session_.notify_log_event(value);
  }
  gneiss::error flush() override { return gneiss::error::success; }

private:
  gneiss::host::runtime_ipc_session& session_;
};

[[nodiscard]] int run_runtime(const runtime_options& options, gneiss::logging::service& logs,
                              gneiss::logging::logger& log) {
  gneiss::host::project_description project;
  gneiss::host::project_load_report project_report;
  const auto project_result =
      gneiss::host::load_project_description(options.project_root, project, project_report);
  if (project_result != gneiss::error::success) {
    const auto context = path_text(project_report.context);
    log.write({.severity = gneiss::logging::level::error,
               .category = gneiss::host::project_load_stage_name(project_report.stage),
               .message = std::format("{} ({})", "工程加载失败", context),
               .result = static_cast<gneiss::error>(project_result)});
    return 2;
  }
  const auto asset_root = path_text(project.asset_root);
  if (project.name.size() > std::numeric_limits<std::uint32_t>::max() ||
      asset_root.size() > std::numeric_limits<std::uint32_t>::max()) {
    log.write({.severity = gneiss::logging::level::error,
               .category = "project",
               .message = "工程运行描述过长",
               .result = gneiss::error::out_of_memory});
    return 2;
  }
  log.write({.severity = gneiss::logging::level::info,
             .category = "project",
             .message = std::format("{} ({})", "工程加载完成", path_text(project.project_root)),
             .result = gneiss::error::success});

  runtime_context context{&log, options.stop_file};
  if (!context.stop_file.empty()) {
    std::error_code error;
    std::filesystem::remove(context.stop_file, error);
  }
  gneiss::runtime::application_configuration desc;
  desc.callbacks.update = [&context](const gneiss::frame_time& time) {
    return update_runtime(&time, &context);
  };
  desc.enable_window = true;
  desc.window.title = project.name;
  desc.window.visible =
      !options.smoke && !options.hidden; // 自动冒烟创建真实隐藏窗口，避免抢占桌面。

  desc.asset_root = asset_root;

  desc.environment_asset = project.environment.asset;

  desc.environment_intensity = project.environment.intensity;
  desc.environment_rotation_radians =
      project.environment.rotation_degrees * 0.01745329251994329577F;

  gneiss::tasks::task_scheduler task_scheduler(
      {.mode = options.cooperative_tasks ? gneiss::tasks::execution_mode::cooperative
                                         : gneiss::tasks::execution_mode::thread_pool});
  context.tasks = &task_scheduler;
  std::shared_ptr<gneiss::runtime::application> application;
  auto created = gneiss::runtime::application::create(desc);
  auto operation = created ? gneiss::error{gneiss::error::success} : created.error();
  if (operation != gneiss::error::success) {
    log.write({.severity = gneiss::logging::level::error,
               .category = "application_create",
               .message = std::format("{} ({})", "Application 创建失败", asset_root),
               .result = static_cast<gneiss::error>(operation)});
    return 3;
  }
  application = std::move(*created);
  context.application = application.get();
  log.write({.severity = gneiss::logging::level::info,
             .category = "application_create",
             .message = "Application 创建完成",
             .result = gneiss::error::success});
  if (application->attach_task_executor(task_scheduler) != gneiss::error::success) {
    return 3;
  }

  if (!project.input_map.empty()) {
    auto input_result = application->begin_action_map(project.input_map);
    if (input_result == gneiss::error::success) {
      context.input_pending = true;
      context.input_uri = project.input_map;
      log.write({.severity = gneiss::logging::level::info,
                 .category = "input_map_pending",
                 .message = std::format("{} ({})", "正在异步加载输入映射", project.input_map),
                 .result = gneiss::error::success});
    }
    if (input_result != gneiss::error::success) {
      log.write({.severity = gneiss::logging::level::error,
                 .category = "input_map",
                 .message = std::format("{} ({})", "输入映射加载失败", project.input_map),
                 .result = input_result});
      return 4;
    }
  }

  gneiss::scene_instance scene = {};
  auto native_result = gneiss::error::success;
  std::string active_scene_uri = project.startup_scene;
  // 宿主只记录加载回执的依赖 URI，不从运行对象反推作者描述。
  std::set<std::string, std::less<>> active_prefab_uris;
  const auto inspection_serial =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  gneiss::host::runtime_scene_inspection scene_inspection(
      inspection_serial == 0U ? 1U : inspection_serial);
  context.scene_inspection = &scene_inspection;
  context.inspection_session_id = inspection_serial == 0U ? 1U : inspection_serial;
  context.scene = scene;
  std::optional<gneiss::host::ipc_asset_revision> structural_reload;
  bool structural_ignored{};
  gneiss::ref<gneiss::host::scene_reload_state> scene_source;
  gneiss::host::runtime_scene_loader scene_loader(*application);
  context.scene_loader = &scene_loader;
  gneiss::host::runtime_asset_reloader asset_reloader(
      [&, native_application =
              application.get()](const gneiss::host::ipc_asset_reload_request& request) {
        const auto& first = request.assets.front();
        if (first.type == gneiss::host::ipc_asset_type::scene ||
            first.type == gneiss::host::ipc_asset_type::prefab) {
          structural_ignored =
              first.type == gneiss::host::ipc_asset_type::scene && first.uri != active_scene_uri;
          if (first.type == gneiss::host::ipc_asset_type::prefab)
            structural_ignored = !active_prefab_uris.contains(first.uri);
          // 先准备可分配的回执，再提交请求，避免接受后因分配失败丢失观察状态。
          structural_reload = first;
          auto accepted = gneiss::error{gneiss::error::success};
          if (!structural_ignored) {
            accepted = scene_loader.reload(scene_source, first.uri,
                                           first.type == gneiss::host::ipc_asset_type::prefab);
          }
          if (accepted.failed())
            structural_reload.reset();
          return accepted;
        }
        std::vector<gneiss::render_resource_source> assets;
        for (const auto& asset : request.assets) {
          using type = gneiss::render_resource_kind;
          assets.push_back(
              {asset.uri, asset.type == gneiss::host::ipc_asset_type::texture    ? type::texture
                          : asset.type == gneiss::host::ipc_asset_type::material ? type::material
                                                                                 : type::mesh});
        }
        auto submitted = native_application->load_assets(assets, true);
        if (!submitted)
          return submitted.error();
        context.asset_request = std::move(*submitted);
        return gneiss::error{gneiss::error::success};
      },
      [&, native_application = application.get()](
          gneiss::error& result, std::vector<gneiss::host::ipc_asset_item_result>& items,
          bool& ready) {
        try {
          if (structural_reload) {
            auto operation = gneiss::error::success;
            if (structural_ignored) {
              result = gneiss::error::success;
              ready = true;
            } else {
              auto completed = scene_loader.take_reload();
              if (!completed && completed.error() != gneiss::error::not_ready)
                operation = completed.error();
              else if (completed) {
                ready = true;
                result = completed->result;
                if (result.ok())
                  result = scene_inspection.set_source_bindings(completed->objects);
              } else {
                ready = false;
              }
            }
            if (ready)
              items.push_back({*structural_reload, result, result.ok()});
            if (ready || (operation.failed() && operation != gneiss::error::not_ready))
              structural_reload.reset();
            return operation;
          }
          ready = false;
          auto polled = context.asset_request.take();
          if (!polled) {
            if (polled.error() == gneiss::error::not_ready)
              return gneiss::error{gneiss::error::success};
            context.asset_request.reset();
            return polled.error();
          }
          ready = true;
          auto completion = std::move(*polled);
          if (ready) {
            for (const auto& item : completion.results) {
              using type = gneiss::render_resource_kind;
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
                                 " queue_ms=" + std::to_string(completion.queue_ms) + " " +
                                 completion.message;
            log.write({.severity = completion.result == gneiss::error::success
                                       ? gneiss::logging::level::info
                                       : gneiss::logging::level::error,
                       .category = "texture_load",
                       .message = metrics,
                       .result = completion.result});
          }
          return gneiss::error{gneiss::error::success};
        } catch (...) {
          context.asset_request.reset();
          structural_reload.reset();
          throw;
        }
      });
  context.asset_reloader = &asset_reloader;

  gneiss::world* world = nullptr;
  world = application->native_world();
  native_result = gneiss::error::success;
  gneiss::host::runtime_property_editor property_editor;
  if (native_result == gneiss::error::success) {
    operation = property_editor.initialize(*world, scene_inspection, context.inspection_session_id);
  } else {
    operation = native_result;
  }
  if (operation != gneiss::error::success) {
    log.write({.severity = gneiss::logging::level::error,
               .category = "property_edit",
               .message = "Runtime 属性写入执行器初始化失败",
               .result = operation});
    if (scene.is_valid())
      (void)scene.unload();
    return 5;
  }
  context.property_editor = &property_editor;

  context.smoke = options.smoke;
  scene_loader.after_activate =
      [&](const gneiss::host::scene_open_result& loaded) -> gneiss::error {
    const auto started = std::chrono::steady_clock::now();
    if (!loaded.content)
      return gneiss::error::invalid_argument;
    std::set<std::string, std::less<>> prefab_uris;
    for (const auto& [uri, dependency] : loaded.content->prefabs)
      prefab_uris.insert(uri);
    active_prefab_uris.swap(prefab_uris);
    scene_source = loaded.reload_state;
    scene = loaded.scene;
    context.scene = scene;
    active_scene_uri = scene_loader.progress().source.uri;
    property_editor.reset();
    ++context.inspection_session_id;
    scene_inspection.reset(context.inspection_session_id);
    const auto associated = scene_inspection.set_source_bindings(loaded.objects);
    if (associated.failed())
      return associated;
    if (context.ipc_session)
      context.ipc_session->discard_pending_inspection();
    context.force_full_inspection = true;
    context.next_inspection_ns = 0U;
    context.next_statistics_sequence = 1U;
    world = application->native_world();
    auto applied = gneiss::error::success;
    if (applied != gneiss::error::success)
      return applied;
    const auto initialized =
        property_editor.initialize(*world, scene_inspection, context.inspection_session_id);
    if (initialized != gneiss::error::success)
      return initialized;
    log.write({.severity = gneiss::logging::level::info,
               .category = "startup_scene",
               .message = std::format("{} ({})", "场景加载完成", active_scene_uri),
               .result = gneiss::error::success});
    log.write({.severity = gneiss::logging::level::info,
               .category = "scene_load",
               .message = "inspection_rebind_ms=" +
                          std::to_string(std::chrono::duration<double, std::milli>(
                                             std::chrono::steady_clock::now() - started)
                                             .count()),
               .result = gneiss::error::success});
    return gneiss::error::success;
  };

  std::unique_ptr<gneiss::host::runtime_ipc_session> ipc_session;
  struct ipc_log_subscription {
    gneiss::logging::service& logs;
    gneiss::logging::sink_connection connection;
    ~ipc_log_subscription() {
      connection.disconnect();
      logs.flush();
    }
  } ipc_logging{logs, {}};

  if (options.ipc_port != 0U) {
    gneiss::host::runtime_ipc_config ipc_config;
    ipc_config.endpoint = {options.ipc_address, options.ipc_port};
    ipc_config.session_token = options.ipc_token;
    ipc_session = std::make_unique<gneiss::host::runtime_ipc_session>(std::move(ipc_config));
    operation = ipc_session->start(gneiss::host::runtime_ipc_session::clock::now());
    if (operation == gneiss::error::success) {
      operation = ipc_session->notify_running();
    }
    if (operation != gneiss::error::success) {
      log.write({.severity = gneiss::logging::level::error,
                 .category = "ipc",
                 .message = "Editor IPC 会话启动失败",
                 .result = operation});

      if (scene.is_valid())
        (void)scene.unload();
      return 11;
    }
    context.ipc_session = ipc_session.get();
    ipc_logging.connection = logs.attach(std::make_unique<ipc_log_sink>(*ipc_session));
    log.write({.severity = gneiss::logging::level::info,
               .category = "ipc",
               .message = "Editor IPC 会话正在连接",
               .result = gneiss::error::success});
  }

  native_result =
      scene_loader.request({context.inspection_session_id, 1U, project.startup_scene}, 0U);
  if (native_result != gneiss::error::success)
    return 4;
  operation = application->run(0U);
  const auto task_stats = task_scheduler.stats();
  log.write({.severity = gneiss::logging::level::info,
             .category = "task_statistics",
             .message = "submitted=" + std::to_string(task_stats.submitted) +
                        " completed=" + std::to_string(task_stats.completed) +
                        " retained=" + std::to_string(task_stats.retained),
             .result = gneiss::error::success});
  if (context.ipc_session != nullptr && context.ipc_failure == gneiss::error::success) {
    (void)context.ipc_session->notify_shutdown(
        operation == gneiss::error::success ? 0 : operation.code());
  }
  ipc_logging.connection.disconnect();
  logs.flush();
  context.ipc_session = nullptr;
  if (context.asset_request) {
    context.asset_request.reset();
  }
  context.asset_reloader = nullptr;
  context.property_editor = nullptr;
  context.scene_inspection = nullptr;
  context.scene = gneiss::scene_instance{};
  if (ipc_session) {
    (void)ipc_session->stop();
  }
  auto shutdown_result = application->request_shutdown();
  if (shutdown_result.ok()) {
    do {
      if (task_scheduler.mode() == gneiss::tasks::execution_mode::cooperative)
        (void)task_scheduler.run_ready();
      shutdown_result = application->advance_shutdown();
      if (shutdown_result == gneiss::error::not_ready)
        std::this_thread::yield();
    } while (shutdown_result == gneiss::error::not_ready);
  }
  if (shutdown_result.failed())
    log.write({.severity = gneiss::logging::level::error,
               .category = "shutdown",
               .message = "Runtime 关闭失败",
               .result = shutdown_result});
  if (operation != gneiss::error::success) {
    log.write({.severity = gneiss::logging::level::error,
               .category = "run",
               .message = "Runtime 主循环失败",
               .result = static_cast<gneiss::error>(operation)});

    return 7;
  }
  if (context.ipc_failure != gneiss::error::success) {
    log.write({.severity = gneiss::logging::level::error,
               .category = "ipc",
               .message = "Runtime 因 Editor IPC 会话失败而退出",
               .result = context.ipc_failure});

    return 11;
  }

  if (shutdown_result.failed())
    return 10;
  if (context.scene_failed && options.smoke)
    return 4;
  log.write({.severity = gneiss::logging::level::info,
             .category = "shutdown",
             .message = "Runtime 已正常退出",
             .result = gneiss::error::success});
  return 0;
}

} // namespace

int gneiss::host::run_runtime_application(int argc, char** argv) {
  try {
    runtime_options options;
    const auto has_valid_options = parse_options(argc, argv, options);
    const auto log_path =
        options.log_file.empty() ? gneiss::host::default_runtime_log_path() : options.log_file;
    gneiss::logging::service logs;
    logs.attach(gneiss::host::create_runtime_protocol_sink());
    gneiss::logging::default_service_binding logging_binding{logs};
    auto log = logs.get_logger("runtime");
    auto file = gneiss::logging::create_rotating_file_sink(log_path);
    if (file)
      logs.attach(std::move(*file));
    if (!file) {
      log.write({.severity = gneiss::logging::level::warning,
                 .category = "log_file",
                 .message = std::format("{} ({})", "日志文件不可用，继续使用控制台日志",
                                        path_text(log_path)),
                 .result = gneiss::error::io});
    }
    if (!has_valid_options) {
      log.write(
          {.severity = gneiss::logging::level::error,
           .category = "arguments",
           .message =
               "用法：gneiss_runtime --project <工程根> [--smoke] [--hidden] [--log-file <路径>] "
               "[--stop-file <路径>] [--ipc-address <地址> --ipc-port <端口> "
               "--ipc-token <令牌>]",
           .result = gneiss::error::invalid_argument});
      return 64;
    }
    return run_runtime(options, logs, log);
  } catch (...) {
    gneiss::fatal_current_exception();
  }
}
