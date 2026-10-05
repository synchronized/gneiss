// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include "child_process.hpp"
#include "editor_ipc_event.h"
#include "editor_ipc_session.h"
#include "ipc_asset_protocol.h"
#include "ipc_statistics_protocol.h"
#include "runtime_log_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <new>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace gneiss::editor {

struct runtime_process::implementation final {
  child_process process;
  child_process build_process;
  console_model console;
  runtime_scene_mirror scene_mirror;
  runtime_property_edits property_edits;
  ipc_runtime_statistics statistics;
  ipc_scene_progress scene_load;
  std::uint64_t scene_revision{};
  bool scene_requested{};
  app::runtime_log_line_decoder line_decoder;
  std::uint64_t runtime_session_id = 0U;
  bool output_finished = true;
  std::string combined_output;
  std::filesystem::path pending_runtime;
  std::filesystem::path pending_cmake;
  runtime_launch_request pending_request;
  app::project_description pending_project;
  std::filesystem::path session_root;
  std::filesystem::path stop_file;
  std::filesystem::path log_file;
  std::chrono::steady_clock::time_point stop_deadline;
  bool forced_termination_reported = false;
  bool is_building = false;
  bool is_configuring = false;
  bool ipc_shutdown_complete = false;
  bool inspection_resync_pending = false;
  std::map<std::string, ipc_asset_type> known_assets;
  struct asset_batch final {
    std::uint64_t revision = 0U;
    ipc_asset_operation operation{ipc_asset_operation::reload};
    std::vector<ipc_asset_revision> assets;
  };
  std::deque<asset_batch> pending_asset_batches;
  std::uint64_t asset_revision_in_flight = 0U;
  runtime_asset_reload_status asset_reload;
  std::uint64_t asset_session_id = 1U;
  bool asset_resync_required = true;
  std::deque<ipc_inspection_batch> pending_inspection_input;
  editor_ipc_session ipc_session;
  runtime_control_state control_state = runtime_control_state::stopped;
  result last_result = result::success;

  [[nodiscard]] std::uint64_t next_asset_revision() noexcept {
    ++asset_reload.revision;
    if (asset_reload.revision == 0U) {
      asset_reload.revision = 1U;
      ++asset_session_id;
      if (asset_session_id == 0U) {
        asset_session_id = 1U;
      }
    }
    return asset_reload.revision;
  }

  static constexpr std::size_t asset_queue_limit = 1024U;
  static constexpr std::size_t asset_queue_bytes = 128U * 1024U;
  bool asset_queue_fits(std::span<const std::string> incoming) const {
    std::size_t count = incoming.size();
    std::size_t bytes{};
    for (const auto& uri : incoming) {
      if (uri.size() > asset_queue_bytes - bytes) {
        return false;
      }
      bytes += uri.size();
    }
    for (const auto& batch : pending_asset_batches) {
      count += batch.assets.size();
      for (const auto& asset : batch.assets) {
        if (asset.uri.size() > asset_queue_bytes - bytes) {
          return false;
        }
        bytes += asset.uri.size();
      }
    }
    return count <= asset_queue_limit;
  }

  void queue_asset_batch(std::vector<ipc_asset_revision> assets, ipc_asset_operation operation) {
    if (assets.empty()) {
      return;
    }
    const auto revision = next_asset_revision();
    const bool render_batch = assets.front().type == ipc_asset_type::texture ||
                              assets.front().type == ipc_asset_type::material ||
                              assets.front().type == ipc_asset_type::static_mesh;
    if (operation == ipc_asset_operation::reload && render_batch &&
        !pending_asset_batches.empty() &&
        pending_asset_batches.back().operation == ipc_asset_operation::reload &&
        (pending_asset_batches.back().assets.front().type == ipc_asset_type::texture ||
         pending_asset_batches.back().assets.front().type == ipc_asset_type::material ||
         pending_asset_batches.back().assets.front().type == ipc_asset_type::static_mesh)) {
      auto& pending = pending_asset_batches.back();
      const auto texture_only = [](const auto& asset) {
        return asset.type == ipc_asset_type::texture;
      };
      if (std::ranges::all_of(assets, texture_only) &&
          std::ranges::all_of(pending.assets, texture_only) &&
          assets.size() + pending.assets.size() > 16U) {
        pending_asset_batches.push_back(
            {.revision = revision, .operation = operation, .assets = std::move(assets)});
        return;
      }
      pending.revision = revision;
      for (auto& asset : assets) {
        const auto found = std::ranges::find(pending.assets, asset.uri, &ipc_asset_revision::uri);
        if (found == pending.assets.end()) {
          pending.assets.push_back(std::move(asset));
        } else {
          found->type = asset.type;
        }
      }
      return;
    }
    pending_asset_batches.push_back(
        {.revision = revision, .operation = operation, .assets = std::move(assets)});
  }

  bool queue_asset_resync() {
    const auto textures = std::ranges::count_if(
        known_assets, [](const auto& item) { return item.second == ipc_asset_type::texture; });
    const auto mixed_render = std::ranges::any_of(known_assets, [](const auto& item) {
      return item.second == ipc_asset_type::material || item.second == ipc_asset_type::static_mesh;
    });
    if (!mixed_render && textures > 16) {
      return false;
    }
    std::size_t bytes{};
    for (const auto& [uri, type] : known_assets) {
      (void)type;
      if (uri.size() > asset_queue_bytes - bytes) {
        return false;
      }
      bytes += uri.size();
    }
    if (known_assets.size() > asset_queue_limit) {
      return false;
    }
    pending_asset_batches.clear();
    std::vector<ipc_asset_revision> render_assets;
    std::vector<ipc_asset_revision> structural_assets;
    for (const auto& [uri, type] : known_assets) {
      if (type == ipc_asset_type::texture || type == ipc_asset_type::material ||
          type == ipc_asset_type::static_mesh) {
        render_assets.push_back({.uri = uri, .type = type});
      } else {
        structural_assets.push_back({.uri = uri, .type = type});
      }
    }
    queue_asset_batch(std::move(render_assets), ipc_asset_operation::resync);
    for (auto& asset : structural_assets) {
      queue_asset_batch({std::move(asset)}, ipc_asset_operation::resync);
    }
    return true;
  }

  void append_event_unique(app::runtime_log_record event) noexcept {
    const auto duplicate = std::ranges::any_of(console.entries(), [&](const auto& entry) {
      return entry.kind == console_entry_kind::structured &&
             entry.session_id == runtime_session_id && entry.event.sequence == event.sequence &&
             entry.event.source == event.source;
    });
    if (!duplicate) {
      (void)console.append_event(runtime_session_id, to_console_event(std::move(event)));
    }
  }

  void request_inspection_resync() noexcept {
    if (inspection_resync_pending || !ipc_session.is_authenticated()) {
      return;
    }
    if (ipc_session.request_inspection_resync() == result::success) {
      inspection_resync_pending = true;
    }
  }

  void apply_inspection_batch(const ipc_inspection_batch& batch) noexcept {
    const auto applied = scene_mirror.apply(batch);
    if (applied != result::success) {
      last_result = applied;
      if (scene_mirror.needs_full_snapshot()) {
        request_inspection_resync();
      }
    } else if (batch.is_full && !scene_mirror.needs_full_snapshot()) {
      inspection_resync_pending = false;
    }
    if (applied == result::success && scene_mirror.session_id() != 0U) {
      property_edits.begin_session(scene_mirror.session_id());
    }
  }

  void stop_ipc() noexcept {
    (void)ipc_session.stop();
    property_edits.disconnect();
  }

  void fail_ipc(result operation) noexcept {
    last_result = operation;
    control_state = runtime_control_state::failed;
    property_edits.disconnect();
    if (stop_deadline == std::chrono::steady_clock::time_point{}) {
      stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
  }

  void update_ipc() noexcept {
    const auto now = std::chrono::steady_clock::now();
    std::vector<runtime_ipc_event> events;
    const auto updated = ipc_session.update(process.is_running(), events);
    if (updated != result::success) {
      fail_ipc(updated);
      return;
    }
    for (auto& decoded_event : events) {
      if (std::holds_alternative<runtime_inspection_event>(decoded_event)) {
        constexpr std::size_t maximum_pending_inspection_frames = 256U;
        if (pending_inspection_input.size() >= maximum_pending_inspection_frames) {
          pending_inspection_input.clear();
          scene_mirror.invalidate();
          last_result = result::not_ready;
          request_inspection_resync();
        } else {
          pending_inspection_input.push_back(
              std::move(std::get<runtime_inspection_event>(decoded_event).value));
        }
        continue;
      }
      if (const auto* value = std::get_if<runtime_statistics_event>(&decoded_event)) {
        if (value->value.session_id == scene_mirror.session_id() &&
            value->value.sequence > statistics.sequence) {
          statistics = value->value;
        }
        continue;
      }
      if (auto* value = std::get_if<runtime_property_result_event>(&decoded_event)) {
        const auto accepted = property_edits.accept(std::move(value->value));
        if (accepted != result::success && accepted != result::not_found &&
            accepted != result::invalid_state) {
          last_result = accepted;
        }
        continue;
      }
      if (const auto* value = std::get_if<runtime_scene_progress_event>(&decoded_event)) {
        if (scene_requested && (value->value.source.session != scene_load.source.session ||
                                value->value.source.revision != scene_load.source.revision))
          continue;
        if (scene_load.source.revision != 0U &&
            value->value.source.session == scene_load.source.session &&
            value->value.source.revision == scene_load.source.revision &&
            scene_phase_terminal(scene_load.phase))
          continue;
        scene_load = value->value;
        if (scene_load.phase == ipc_scene_phase::applied) {
          pending_inspection_input.clear();
          scene_mirror.reset();
          property_edits.begin_session(0U);
          statistics = {};
          inspection_resync_pending = false;
          request_inspection_resync();
        }
        continue;
      }
      if (const auto* value = std::get_if<runtime_asset_result_event>(&decoded_event)) {
        if (value->value.session_id == asset_session_id &&
            value->value.revision == asset_revision_in_flight) {
          asset_reload.can_cancel = value->value.can_cancel;
          asset_reload.completed_assets = value->value.completed_assets;
          asset_reload.total_assets = value->value.total_assets;
          if (value->value.status != ipc_asset_apply_status::preparing &&
              value->value.status != ipc_asset_apply_status::uploading)
            asset_revision_in_flight = 0U;
          asset_reload.message = value->value.message;
          switch (value->value.status) {
          case ipc_asset_apply_status::preparing:
          case ipc_asset_apply_status::uploading:
            asset_reload.state = runtime_asset_reload_state::applying;
            break;
          case ipc_asset_apply_status::applied:
          case ipc_asset_apply_status::stale:
            asset_reload.state = pending_asset_batches.empty()
                                     ? runtime_asset_reload_state::applied
                                     : runtime_asset_reload_state::waiting;
            break;
          case ipc_asset_apply_status::cancelled:
          case ipc_asset_apply_status::failed:
            asset_reload.state = runtime_asset_reload_state::failed;
            pending_asset_batches.clear();
            break;
          case ipc_asset_apply_status::restart_required:
            asset_reload.state = runtime_asset_reload_state::restart_required;
            pending_asset_batches.clear();
            break;
          }
        }
        continue;
      }
      if (const auto* state_event = std::get_if<runtime_state_event>(&decoded_event)) {
        switch (state_event->value) {
        case ipc_control_state::running:
          control_state = runtime_control_state::running;
          break;
        case ipc_control_state::paused:
          control_state = runtime_control_state::paused;
          break;
        case ipc_control_state::stopping:
          control_state = runtime_control_state::stopping;
          break;
        case ipc_control_state::loading:
        case ipc_control_state::ready:
          control_state = runtime_control_state::connecting;
          break;
        case ipc_control_state::invalid:
          fail_ipc(result::invalid_argument);
          break;
        }
      } else if (const auto* protocol_error =
                     std::get_if<runtime_protocol_error_event>(&decoded_event)) {
        last_result = from_native(protocol_error->value.code);
      } else if (auto* log_event = std::get_if<runtime_log_event>(&decoded_event)) {
        while (!log_event->value.empty() &&
               (log_event->value.back() == '\n' || log_event->value.back() == '\r')) {
          log_event->value.pop_back();
        }
        app::runtime_log_record record;
        if (app::parse_runtime_log_line(log_event->value, record) ==
            app::runtime_log_parse_result::success) {
          append_event_unique(std::move(record));
        }
      } else if (std::holds_alternative<runtime_shutdown_event>(decoded_event)) {
        ipc_shutdown_complete = true;
        control_state = runtime_control_state::stopping;
      }
    }
    constexpr std::size_t inspection_apply_budget = 8U;
    for (std::size_t count = 0U;
         count < inspection_apply_budget && !pending_inspection_input.empty(); ++count) {
      apply_inspection_batch(pending_inspection_input.front());
      pending_inspection_input.pop_front();
    }
    property_edits.expire(now, std::chrono::seconds(2));
    if (asset_resync_required) {
      if (!queue_asset_resync()) {
        asset_reload.state = runtime_asset_reload_state::failed;
        asset_reload.publish_result = result::not_ready;
        asset_reload.message = "资产重同步超过队列预算，请重启 Runtime";
      }
      asset_resync_required = false;
    }
    if (ipc_session.is_authenticated() && ipc_session.supports_asset_reload() &&
        asset_revision_in_flight == 0U && !pending_asset_batches.empty()) {
      auto& batch = pending_asset_batches.front();
      const ipc_asset_reload_request request{
          .session_id = asset_session_id, .revision = batch.revision, .assets = batch.assets};
      const auto sent = ipc_session.send_asset_reload(request, batch.operation);
      if (sent != result::success) {
        fail_ipc(sent);
        return;
      }
      asset_revision_in_flight = batch.revision;
      pending_asset_batches.pop_front();
      asset_reload.state = runtime_asset_reload_state::applying;
    }
  }

  void append_console_lines(std::vector<app::runtime_log_line>& lines) noexcept {
    for (auto& line : lines) {
      app::runtime_log_record event;
      if (!line.was_truncated &&
          app::parse_runtime_log_line(line.text, event) == app::runtime_log_parse_result::success) {
        append_event_unique(std::move(event));
      } else {
        (void)console.append_raw(runtime_session_id, line.text, line.was_truncated);
      }
    }
  }

  void consume_runtime_output() noexcept {
    std::string bytes;
    process.consume_output(bytes);
    std::vector<app::runtime_log_line> lines;
    if (!bytes.empty() && line_decoder.append(bytes, lines) == result::success) {
      append_console_lines(lines);
    }
  }

  void finish_runtime_output() noexcept {
    if (output_finished) {
      return;
    }
    consume_runtime_output();
    std::vector<app::runtime_log_line> lines;
    if (line_decoder.finish(lines) == result::success) {
      append_console_lines(lines);
    }
    output_finished = true;
  }

  void clean_stop_file() noexcept {
    std::error_code error;
    std::filesystem::remove(stop_file, error);
  }

  void discard_session() noexcept {
    std::error_code error;
    std::filesystem::remove_all(session_root, error);
  }
};

runtime_process::runtime_process() : implementation_(std::make_unique<implementation>()) {}

runtime_process::~runtime_process() {
  if (!implementation_) {
    return;
  }
  implementation_->build_process.update();
  if (implementation_->build_process.is_running()) {
    (void)implementation_->build_process.terminate();
  }
  update();
  if (is_running()) {
    (void)request_stop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (is_running() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      update();
    }
    if (is_running()) {
      (void)implementation_->process.terminate();
    }
  }
  implementation_->clean_stop_file();
  implementation_->stop_ipc();
}

result runtime_process::start(const std::filesystem::path& executable,
                              const runtime_launch_request& request) noexcept {
  if (!implementation_ || is_busy() || executable.empty() || request.project_root.empty()) {
    return result::invalid_state;
  }
  try {
    std::error_code error;
    if (!std::filesystem::is_directory(request.project_root, error) || error) {
      return result::not_found;
    }
    implementation_->clean_stop_file();
    implementation_->process.clear_output();
    implementation_->line_decoder.reset();
    implementation_->combined_output.clear();
    implementation_->stop_deadline = {};
    implementation_->forced_termination_reported = false;
    implementation_->ipc_shutdown_complete = false;
    implementation_->inspection_resync_pending = false;
    implementation_->asset_resync_required = true;
    implementation_->asset_revision_in_flight = 0U;
    if (!implementation_->known_assets.empty()) {
      implementation_->asset_reload.state = runtime_asset_reload_state::waiting;
      implementation_->asset_reload.message = "等待 Runtime 全量同步资产修订";
    }
    implementation_->pending_inspection_input.clear();
    implementation_->scene_mirror.reset();
    implementation_->scene_load = {};
    implementation_->scene_requested = false;
    implementation_->property_edits.begin_session(0U);
    implementation_->statistics = {};
    implementation_->control_state = runtime_control_state::connecting;
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    implementation_->session_root = std::filesystem::temp_directory_path() / "Gneiss" /
                                    ("editor-runtime-" + std::to_string(serial));
    std::filesystem::create_directories(implementation_->session_root, error);
    if (error) {
      return result::io;
    }
    implementation_->stop_file = implementation_->session_root / "stop.signal";
    implementation_->log_file = implementation_->session_root / "runtime.log";
    auto started = implementation_->ipc_session.start();
    if (started != result::success) {
      implementation_->control_state = runtime_control_state::failed;
      implementation_->discard_session();
      return started;
    }
    const auto endpoint = implementation_->ipc_session.endpoint();
    child_process_start_info info;
    info.executable = executable;
    info.arguments = {"--project",     request.project_root,
                      "--stop-file",   implementation_->stop_file,
                      "--log-file",    implementation_->log_file,
                      "--ipc-address", endpoint.address,
                      "--ipc-port",    std::to_string(endpoint.port),
                      "--ipc-token",   implementation_->ipc_session.token()};
    started = implementation_->process.start(info);
    implementation_->last_result = started;
    if (started != result::success) {
      implementation_->stop_ipc();
      implementation_->control_state = runtime_control_state::failed;
      implementation_->discard_session();
    } else {
      implementation_->runtime_session_id = implementation_->console.begin_session();
      implementation_->output_finished = false;
    }
    return started;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result runtime_process::build_and_start(const std::filesystem::path& cmake_executable,
                                        const std::filesystem::path& runtime_executable,
                                        const runtime_launch_request& request,
                                        const app::project_description& project) noexcept {
  if (!implementation_ || is_busy() || cmake_executable.empty() || runtime_executable.empty() ||
      request.project_root.empty() || project.game_module.name.empty()) {
    return result::invalid_state;
  }
  try {
    child_process_start_info info;
    info.executable = cmake_executable;
    info.working_directory = project.project_root;
    const auto& profile = app::game_build_profile_description_for(project.game_module,
                                                                  app::game_build_profile::debug);
    info.arguments = {"--preset", profile.configure_preset,
                      "-DCMAKE_PREFIX_PATH=" +
                          runtime_executable.parent_path().parent_path().string()};
    implementation_->build_process.clear_output();
    implementation_->combined_output.clear();
    implementation_->pending_runtime = runtime_executable;
    implementation_->pending_cmake = cmake_executable;
    implementation_->pending_request = request;
    implementation_->pending_project = project;
    const auto operation = implementation_->build_process.start(info);
    implementation_->last_result = operation;
    implementation_->is_building = operation == result::success;
    implementation_->is_configuring = operation == result::success;
    implementation_->control_state = operation == result::success ? runtime_control_state::building
                                                                  : runtime_control_state::failed;
    if (operation == result::success) {
      implementation_->combined_output = "[Editor] 正在配置游戏工程。\n";
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result runtime_process::request_stop() noexcept {
  if (!implementation_) {
    return result::not_ready;
  }
  if (is_building()) {
    const auto operation = implementation_->build_process.terminate();
    implementation_->is_building = false;
    implementation_->last_result = operation;
    implementation_->control_state = operation == result::success ? runtime_control_state::stopped
                                                                  : runtime_control_state::failed;
    implementation_->combined_output += "\n[Editor] 游戏模块构建已停止。\n";
    return operation;
  }
  if (!is_running()) {
    return result::not_ready;
  }
  if (implementation_->ipc_session.is_authenticated()) {
    const auto operation = implementation_->ipc_session.request_stop();
    if (operation == result::success) {
      implementation_->control_state = runtime_control_state::stopping;
      if (implementation_->stop_deadline == std::chrono::steady_clock::time_point{}) {
        implementation_->stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      }
      return result::success;
    }
  }
  try {
    std::ofstream signal(implementation_->stop_file, std::ios::binary | std::ios::trunc);
    signal.flush();
    if (!signal) {
      return result::io;
    }
    if (implementation_->stop_deadline == std::chrono::steady_clock::time_point{}) {
      implementation_->stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
    return result::success;
  } catch (...) {
    return result::io;
  }
}

result runtime_process::request_pause() noexcept {
  if (!implementation_ || !implementation_->ipc_session.is_authenticated() ||
      implementation_->control_state != runtime_control_state::running) {
    return result::not_ready;
  }
  return implementation_->ipc_session.request_pause();
}

result runtime_process::request_resume() noexcept {
  if (!implementation_ || !implementation_->ipc_session.is_authenticated() ||
      implementation_->control_state != runtime_control_state::paused) {
    return result::not_ready;
  }
  return implementation_->ipc_session.request_resume();
}

result runtime_process::request_property_write(runtime_property_key key,
                                               std::uint64_t expected_revision,
                                               ipc_property_value value) noexcept {
  if (!implementation_ || !implementation_->ipc_session.is_authenticated() ||
      !implementation_->ipc_session.supports_property_editing() ||
      (implementation_->control_state != runtime_control_state::running &&
       implementation_->control_state != runtime_control_state::paused)) {
    return result::not_ready;
  }
  ipc_property_write command;
  auto operation =
      implementation_->property_edits.prepare(std::move(key), expected_revision, std::move(value),
                                              std::chrono::steady_clock::now(), command);
  if (operation != result::success) {
    return operation;
  }
  operation = implementation_->ipc_session.send_property_write(command);
  if (operation != result::success) {
    implementation_->fail_ipc(operation);
  }
  return operation;
}

result runtime_process::publish_asset_revision(std::span<const std::string> output_uris) noexcept {
  if (!implementation_) {
    return result::invalid_argument;
  }
  const auto publish = [&]() -> result {
    if (output_uris.empty()) {
      return result::invalid_argument;
    }
    if (!implementation_->asset_queue_fits(output_uris)) {
      return result::not_ready;
    }
    try {
      std::vector<ipc_asset_revision> render_assets;
      std::vector<ipc_asset_revision> structural_assets;
      for (const auto& uri : output_uris) {
        std::optional<ipc_asset_type> type;
        if (uri.ends_with(".texture.json")) {
          type = ipc_asset_type::texture;
        } else if (uri.ends_with(".material.json")) {
          type = ipc_asset_type::material;
        } else if (uri.ends_with(".gneiss-mesh") || uri.ends_with(".mesh.json")) {
          type = ipc_asset_type::static_mesh;
        } else if (uri.ends_with(".scene.json")) {
          type = ipc_asset_type::scene;
        } else if (uri.ends_with(".prefab.json")) {
          type = ipc_asset_type::prefab;
        }
        if (type) {
          auto& destination = *type == ipc_asset_type::scene || *type == ipc_asset_type::prefab
                                  ? structural_assets
                                  : render_assets;
          destination.push_back({.uri = uri, .type = *type});
        }
      }
      if (render_assets.empty() && structural_assets.empty()) {
        return result::unsupported;
      }
      if (render_assets.size() > 16U && std::ranges::all_of(render_assets, [](const auto& asset) {
            return asset.type == ipc_asset_type::texture;
          })) {
        return result::invalid_argument;
      }
      for (const auto& asset : render_assets) {
        implementation_->known_assets.insert_or_assign(asset.uri, asset.type);
      }
      for (const auto& asset : structural_assets) {
        implementation_->known_assets.insert_or_assign(asset.uri, asset.type);
      }
      implementation_->queue_asset_batch(std::move(render_assets), ipc_asset_operation::reload);
      for (auto& asset : structural_assets) {
        implementation_->queue_asset_batch({std::move(asset)}, ipc_asset_operation::reload);
      }
      implementation_->asset_reload.state = runtime_asset_reload_state::waiting;
      implementation_->asset_reload.message = "等待 Runtime 应用资产修订";
      return result::success;
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::internal;
    }
  };
  const auto operation = publish();
  implementation_->asset_reload.publish_result = operation;
  return operation;
}

bool runtime_process::supports_scene_loading() const noexcept {
  return implementation_ && implementation_->ipc_session.is_authenticated() &&
         implementation_->ipc_session.supports_scene_loading();
}
const ipc_scene_progress& runtime_process::scene_load_status() const noexcept {
  return implementation_->scene_load;
}
result runtime_process::load_scene(std::string_view uri) noexcept try {
  if (!supports_scene_loading())
    return result::not_ready;
  auto& state = *implementation_;
  if (state.scene_load.source.revision != 0U && !scene_phase_terminal(state.scene_load.phase))
    return result::not_ready;
  ipc_scene_request source{1U, ++state.scene_revision, std::string(uri)};
  const auto sent = state.ipc_session.send_scene_request(source, false);
  if (sent != result::success)
    return sent;
  state.scene_load = {
      .source = std::move(source), .can_cancel = true, .message = "等待 Runtime 接受场景切换"};
  state.scene_requested = true;
  return result::success;
} catch (...) {
  return result::out_of_memory;
}
result runtime_process::cancel_scene_load() noexcept {
  if (!implementation_ || !implementation_->scene_load.can_cancel)
    return result::not_ready;
  const auto result =
      implementation_->ipc_session.send_scene_request(implementation_->scene_load.source, true);
  if (result == result::success)
    implementation_->scene_load.can_cancel = false;
  return result;
}
result runtime_process::retry_scene_load() noexcept {
  if (!implementation_ || (implementation_->scene_load.phase != ipc_scene_phase::failed &&
                           implementation_->scene_load.phase != ipc_scene_phase::cancelled))
    return result::not_ready;
  return load_scene(implementation_->scene_load.source.uri);
}

result runtime_process::cancel_asset_reload() noexcept {
  if (!implementation_ || !implementation_->asset_reload.can_cancel ||
      implementation_->asset_revision_in_flight == 0U)
    return result::not_ready;
  const auto result = implementation_->ipc_session.send_asset_cancel(
      implementation_->asset_session_id, implementation_->asset_revision_in_flight);
  if (result == result::success) {
    implementation_->asset_reload.can_cancel = false;
    implementation_->asset_reload.message = "取消已请求，等待 Runtime 确认";
  }
  return result;
}

result runtime_process::retry_asset_reload() noexcept {
  if (!implementation_ ||
      implementation_->asset_reload.state != runtime_asset_reload_state::failed ||
      implementation_->asset_revision_in_flight != 0U || implementation_->known_assets.empty() ||
      !implementation_->ipc_session.is_authenticated() ||
      (implementation_->control_state != runtime_control_state::running &&
       implementation_->control_state != runtime_control_state::paused)) {
    return result::not_ready;
  }
  try {
    if (!implementation_->queue_asset_resync()) {
      return result::not_ready;
    }
    implementation_->asset_reload.message = "等待 Runtime 重新同步资产";
    implementation_->asset_reload.state = runtime_asset_reload_state::waiting;
    return result::success;
  } catch (const std::bad_alloc&) {
    implementation_->pending_asset_batches.clear();
    return result::out_of_memory;
  } catch (...) {
    implementation_->pending_asset_batches.clear();
    return result::internal;
  }
}

void runtime_process::update() noexcept {
  if (!implementation_) {
    return;
  }
  if (implementation_->is_building) {
    implementation_->build_process.update();
    implementation_->combined_output =
        (implementation_->is_configuring ? "[Editor] 正在配置游戏工程。\n"
                                         : "[Editor] 正在构建游戏模块。\n") +
        implementation_->build_process.output();
    if (implementation_->build_process.is_running()) {
      return;
    }
    implementation_->is_building = false;
    if (implementation_->build_process.exit_code() != 0) {
      implementation_->last_result = result::dependency_failed;
      implementation_->control_state = runtime_control_state::failed;
      implementation_->combined_output += implementation_->is_configuring
                                              ? "\n[Editor] 游戏工程配置失败，未启动 Runtime。\n"
                                              : "\n[Editor] 游戏模块构建失败，未启动 Runtime。\n";
      implementation_->is_configuring = false;
      return;
    }
    if (implementation_->is_configuring) {
      implementation_->is_configuring = false;
      child_process_start_info info;
      info.executable = implementation_->pending_cmake;
      info.working_directory = implementation_->pending_project.project_root;
      const auto& profile = app::game_build_profile_description_for(
          implementation_->pending_project.game_module, app::game_build_profile::debug);
      info.arguments = {"--build", "--preset", profile.build_preset, "--target",
                        implementation_->pending_project.game_module.build_target};
      implementation_->build_process.clear_output();
      const auto operation = implementation_->build_process.start(info);
      implementation_->last_result = operation;
      implementation_->is_building = operation == result::success;
      implementation_->control_state = operation == result::success
                                           ? runtime_control_state::building
                                           : runtime_control_state::failed;
      implementation_->combined_output = operation == result::success
                                             ? "[Editor] 工程配置完成，正在构建游戏模块。\n"
                                             : "[Editor] 工程配置完成，但构建进程启动失败。\n";
      return;
    }
    std::filesystem::path module_path;
    implementation_->last_result =
        app::resolve_game_module_path(implementation_->pending_project, module_path);
    if (implementation_->last_result != result::success) {
      implementation_->combined_output += "\n[Editor] 构建完成但模块产物验证失败。\n";
      return;
    }
    const auto build_output =
        implementation_->combined_output + "\n[Editor] 游戏模块构建完成，正在启动 Runtime。\n";
    const auto operation =
        start(implementation_->pending_runtime, implementation_->pending_request);
    implementation_->combined_output = build_output;
    implementation_->last_result = operation;
    if (operation != result::success) {
      implementation_->combined_output += "[Editor] Runtime 启动失败。\n";
      return;
    }
  }
  implementation_->process.update();
  implementation_->update_ipc();
  implementation_->consume_runtime_output();
  if (!implementation_->process.output().empty()) {
    const auto marker = implementation_->combined_output.find("[Runtime]\n");
    if (marker == std::string::npos) {
      implementation_->combined_output += "[Runtime]\n";
    }
    const auto prefix_length = implementation_->combined_output.find("[Runtime]\n") + 10U;
    implementation_->combined_output.resize(prefix_length);
    implementation_->combined_output += implementation_->process.output();
  }
  if (!implementation_->process.is_running()) {
    implementation_->finish_runtime_output();
    implementation_->stop_deadline = {};
    implementation_->stop_ipc();
    if (implementation_->control_state != runtime_control_state::failed) {
      implementation_->control_state = implementation_->process.exit_code() == 0
                                           ? runtime_control_state::stopped
                                           : runtime_control_state::failed;
    }
    return;
  }
  if (implementation_->stop_deadline != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() >= implementation_->stop_deadline) {
    if (!implementation_->forced_termination_reported) {
      implementation_->forced_termination_reported = true;
      implementation_->process.append_output("\n[Editor] Runtime 未在 2 秒内退出，已强制终止。\n");
    }
    (void)implementation_->process.terminate();
  }
}

bool runtime_process::is_running() const noexcept {
  return implementation_ && implementation_->process.is_running();
}
bool runtime_process::is_building() const noexcept {
  return implementation_ && implementation_->is_building;
}
bool runtime_process::is_busy() const noexcept { return is_building() || is_running(); }
runtime_control_state runtime_process::control_state() const noexcept {
  return implementation_ ? implementation_->control_state : runtime_control_state::failed;
}
bool runtime_process::received_shutdown_complete() const noexcept {
  return implementation_ && implementation_->ipc_shutdown_complete;
}
bool runtime_process::has_started() const noexcept {
  return implementation_ &&
         (implementation_->build_process.has_started() || implementation_->process.has_started());
}
int runtime_process::exit_code() const noexcept {
  return implementation_ ? implementation_->process.exit_code() : -1;
}
const std::string& runtime_process::output() const noexcept {
  return implementation_->combined_output;
}
const console_model& runtime_process::console() const noexcept { return implementation_->console; }
const runtime_scene_mirror& runtime_process::scene_mirror() const noexcept {
  static const runtime_scene_mirror empty;
  return implementation_ ? implementation_->scene_mirror : empty;
}
const ipc_runtime_statistics& runtime_process::statistics() const noexcept {
  static const ipc_runtime_statistics empty;
  return implementation_ ? implementation_->statistics : empty;
}
const runtime_property_edit*
runtime_process::property_edit(const runtime_property_key& key) const noexcept {
  return implementation_ ? implementation_->property_edits.find(key) : nullptr;
}
bool runtime_process::supports_property_editing() const noexcept {
  return implementation_ && implementation_->ipc_session.supports_property_editing();
}
const runtime_asset_reload_status& runtime_process::asset_reload_status() const noexcept {
  static const runtime_asset_reload_status empty;
  return implementation_ ? implementation_->asset_reload : empty;
}
const std::filesystem::path& runtime_process::log_file() const noexcept {
  return implementation_->log_file;
}
result runtime_process::last_result() const noexcept {
  return implementation_ ? implementation_->last_result : result::invalid_state;
}
void runtime_process::clear_output() noexcept {
  if (implementation_) {
    implementation_->process.clear_output();
    implementation_->build_process.clear_output();
    implementation_->combined_output.clear();
    implementation_->console.clear();
  }
}

} // namespace gneiss::editor
