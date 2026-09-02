// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include "child_process.h"
#include "ipc_inspection_protocol.h"
#include "ipc_property_edit_protocol.h"
#include "ipc_protocol.h"
#include "ipc_statistics_protocol.h"
#include "ipc_transport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <fstream>
#include <new>
#include <random>
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
  app::runtime_log_line_decoder line_decoder;
  std::uint64_t runtime_session_id = 0U;
  bool output_finished = true;
  std::string combined_output;
  std::filesystem::path pending_runtime;
  runtime_launch_request pending_request;
  app::project_description pending_project;
  std::filesystem::path session_root;
  std::filesystem::path stop_file;
  std::filesystem::path log_file;
  std::chrono::steady_clock::time_point stop_deadline;
  bool forced_termination_reported = false;
  bool is_building = false;
  bool ipc_authenticated = false;
  bool ipc_shutdown_complete = false;
  bool inspection_resync_pending = false;
  bool property_editing_negotiated = false;
  std::deque<ipc_frame> pending_inspection_input;
  std::string ipc_token;
  ipc_transport ipc_server;
  runtime_control_state control_state = runtime_control_state::stopped;
  ipc_timeout_tracker ipc_heartbeat{std::chrono::seconds(10)};
  ipc_timeout_tracker ipc_handshake{std::chrono::seconds(5)};
  std::chrono::steady_clock::time_point next_ping;
  std::uint64_t next_ping_nonce = 1U;
  result last_result = result::success;

  void append_event_unique(app::runtime_log_record event) noexcept {
    const auto duplicate = std::ranges::any_of(console.entries(), [&](const auto& entry) {
      return entry.kind == console_entry_kind::structured &&
             entry.session_id == runtime_session_id && entry.event.sequence == event.sequence &&
             entry.event.source == event.source;
    });
    if (!duplicate) {
      (void)console.append_event(runtime_session_id, std::move(event));
    }
  }

  static std::string make_session_token() {
    constexpr char digits[] = "0123456789abcdef";
    std::array<unsigned char, 32U> bytes{};
    std::random_device random;
    std::string token;
    token.resize(bytes.size() * 2U);
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
      bytes[index] = static_cast<unsigned char>(random());
      token[index * 2U] = digits[bytes[index] >> 4U];
      token[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
    }
    return token;
  }

  result send_ipc(ipc_message message) noexcept {
    ipc_frame frame;
    const auto encoded = encode_ipc_message(message, frame);
    return encoded == result::success ? ipc_server.send(frame) : encoded;
  }

  void request_inspection_resync() noexcept {
    if (inspection_resync_pending || !ipc_authenticated) {
      return;
    }
    ipc_message request;
    request.type = ipc_message_type::inspection_resync;
    if (send_ipc(std::move(request)) == result::success) {
      inspection_resync_pending = true;
    }
  }

  void apply_inspection_frame(const ipc_frame& frame) noexcept {
    ipc_inspection_batch batch;
    const auto decoded = decode_ipc_inspection_batch(frame, batch);
    if (decoded != result::success) {
      last_result = decoded;
      return;
    }
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
    if (ipc_server.state() != ipc_transport_state::stopped) {
      (void)ipc_server.stop();
    }
    ipc_authenticated = false;
    property_editing_negotiated = false;
    property_edits.disconnect();
    ipc_token.clear();
    next_ping = {};
  }

  void fail_ipc(result operation) noexcept {
    last_result = operation;
    control_state = runtime_control_state::failed;
    property_editing_negotiated = false;
    property_edits.disconnect();
    if (stop_deadline == std::chrono::steady_clock::time_point{}) {
      stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
  }

  void update_ipc() noexcept {
    const auto now = std::chrono::steady_clock::now();
    std::vector<ipc_transport_event> events;
    (void)ipc_server.poll_events(events);
    for (auto& event : events) {
      if (event.type == ipc_transport_event_type::error ||
          event.type == ipc_transport_event_type::disconnected) {
        if (process.is_running()) {
          fail_ipc(event.operation == result::success ? result::io : event.operation);
        }
        continue;
      }
      if (event.type != ipc_transport_event_type::frame_received) {
        continue;
      }
      if (!ipc_authenticated) {
        ipc_frame acknowledgment;
        std::vector<std::string> negotiated;
        const std::vector<std::string> supported{
            "control", "heartbeat", "logs", std::string(ipc_capability_runtime_inspection_v1),
            std::string(ipc_capability_runtime_property_edit_v1)};
        auto accepted =
            accept_ipc_hello(event.frame, ipc_token, supported, acknowledgment, negotiated);
        if (accepted == result::success) {
          accepted = ipc_server.send(acknowledgment);
        }
        if (accepted != result::success) {
          fail_ipc(accepted);
          continue;
        }
        ipc_authenticated = true;
        property_editing_negotiated =
            std::ranges::find(negotiated, ipc_capability_runtime_property_edit_v1) !=
            negotiated.end();
        ipc_heartbeat.reset(now);
        next_ping = now;
        continue;
      }

      if (event.frame.message_type ==
          static_cast<std::uint16_t>(ipc_message_type::inspection_snapshot)) {
        constexpr std::size_t maximum_pending_inspection_frames = 256U;
        if (pending_inspection_input.size() >= maximum_pending_inspection_frames) {
          pending_inspection_input.clear();
          scene_mirror.invalidate();
          last_result = result::not_ready;
          request_inspection_resync();
        } else {
          pending_inspection_input.push_back(std::move(event.frame));
        }
        ipc_heartbeat.reset(now);
        continue;
      }
      if (event.frame.message_type ==
          static_cast<std::uint16_t>(ipc_message_type::statistics_snapshot)) {
        ipc_runtime_statistics decoded_statistics;
        const auto decoded = decode_ipc_runtime_statistics(event.frame, decoded_statistics);
        if (decoded != result::success) {
          last_result = decoded;
        } else if (decoded_statistics.session_id == scene_mirror.session_id() &&
                   decoded_statistics.sequence > statistics.sequence) {
          statistics = decoded_statistics;
        }
        ipc_heartbeat.reset(now);
        continue;
      }
      if (event.frame.message_type ==
          static_cast<std::uint16_t>(ipc_message_type::property_write_result)) {
        ipc_property_write_result response;
        const auto decoded = decode_ipc_property_write_result(event.frame, response);
        if (decoded != result::success) {
          last_result = decoded;
        } else {
          const auto accepted = property_edits.accept(std::move(response));
          if (accepted != result::success && accepted != result::not_found &&
              accepted != result::invalid_state) {
            last_result = accepted;
          }
        }
        ipc_heartbeat.reset(now);
        continue;
      }

      ipc_message message;
      const auto decoded = decode_ipc_message(event.frame, message);
      if (decoded == result::unsupported) {
        continue;
      }
      if (decoded != result::success) {
        fail_ipc(decoded);
        continue;
      }
      ipc_heartbeat.reset(now);
      if (message.type == ipc_message_type::state_changed) {
        switch (message.runtime_state) {
        case ipc_runtime_state::running:
          control_state = runtime_control_state::running;
          break;
        case ipc_runtime_state::paused:
          control_state = runtime_control_state::paused;
          break;
        case ipc_runtime_state::stopping:
          control_state = runtime_control_state::stopping;
          break;
        case ipc_runtime_state::loading:
        case ipc_runtime_state::ready:
          control_state = runtime_control_state::connecting;
          break;
        }
      } else if (message.type == ipc_message_type::error) {
        last_result = from_native(message.code);
      } else if (message.type == ipc_message_type::log_event) {
        while (!message.text.empty() &&
               (message.text.back() == '\n' || message.text.back() == '\r')) {
          message.text.pop_back();
        }
        app::runtime_log_record record;
        if (app::parse_runtime_log_line(message.text, record) ==
            app::runtime_log_parse_result::success) {
          append_event_unique(std::move(record));
        }
      } else if (message.type == ipc_message_type::shutdown_complete) {
        ipc_shutdown_complete = true;
        control_state = runtime_control_state::stopping;
      }
    }
    constexpr std::size_t inspection_apply_budget = 8U;
    for (std::size_t count = 0U;
         count < inspection_apply_budget && !pending_inspection_input.empty(); ++count) {
      apply_inspection_frame(pending_inspection_input.front());
      pending_inspection_input.pop_front();
    }
    if (!ipc_authenticated) {
      if (process.is_running() && ipc_handshake.expired(now)) {
        fail_ipc(result::not_ready);
      }
      return;
    }
    if (control_state == runtime_control_state::failed) {
      return;
    }
    if (now >= next_ping) {
      ipc_message ping;
      ping.type = ipc_message_type::ping;
      ping.nonce = next_ping_nonce++;
      if (send_ipc(std::move(ping)) != result::success) {
        fail_ipc(result::not_ready);
        return;
      }
      next_ping = now + std::chrono::seconds(1);
    }
    if (ipc_heartbeat.expired(now)) {
      fail_ipc(result::not_ready);
    }
    property_edits.expire(now, std::chrono::seconds(2));
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
    implementation_->pending_inspection_input.clear();
    implementation_->scene_mirror.reset();
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
    implementation_->ipc_token = implementation::make_session_token();
    auto started = implementation_->ipc_server.start_server();
    if (started != result::success) {
      implementation_->control_state = runtime_control_state::failed;
      implementation_->discard_session();
      return started;
    }
    const auto endpoint = implementation_->ipc_server.endpoint();
    implementation_->ipc_handshake.reset(std::chrono::steady_clock::now());
    child_process_start_info info;
    info.executable = executable;
    info.arguments = {
        "--project",  request.project_root,          "--stop-file",   implementation_->stop_file,
        "--log-file", implementation_->log_file,     "--ipc-address", endpoint.address,
        "--ipc-port", std::to_string(endpoint.port), "--ipc-token",   implementation_->ipc_token};
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
    info.arguments = {"--build", "--preset", project.game_module.build_preset, "--target",
                      project.game_module.build_target};
    implementation_->build_process.clear_output();
    implementation_->combined_output.clear();
    implementation_->pending_runtime = runtime_executable;
    implementation_->pending_request = request;
    implementation_->pending_project = project;
    const auto operation = implementation_->build_process.start(info);
    implementation_->last_result = operation;
    implementation_->is_building = operation == result::success;
    implementation_->control_state = operation == result::success ? runtime_control_state::building
                                                                  : runtime_control_state::failed;
    if (operation == result::success) {
      implementation_->combined_output = "[Editor] 正在构建游戏模块。\n";
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
  if (implementation_->ipc_authenticated) {
    ipc_message stop;
    stop.type = ipc_message_type::stop;
    const auto operation = implementation_->send_ipc(std::move(stop));
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
  if (!implementation_ || !implementation_->ipc_authenticated ||
      implementation_->control_state != runtime_control_state::running) {
    return result::not_ready;
  }
  ipc_message pause;
  pause.type = ipc_message_type::pause;
  return implementation_->send_ipc(std::move(pause));
}

result runtime_process::request_resume() noexcept {
  if (!implementation_ || !implementation_->ipc_authenticated ||
      implementation_->control_state != runtime_control_state::paused) {
    return result::not_ready;
  }
  ipc_message resume;
  resume.type = ipc_message_type::resume;
  return implementation_->send_ipc(std::move(resume));
}

result runtime_process::request_property_write(runtime_property_key key,
                                               std::uint64_t expected_revision,
                                               ipc_property_value value) noexcept {
  if (!implementation_ || !implementation_->ipc_authenticated ||
      !implementation_->property_editing_negotiated ||
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
  ipc_frame frame;
  operation = encode_ipc_property_write(command, frame);
  if (operation == result::success) {
    operation = implementation_->ipc_server.send(frame);
  }
  if (operation != result::success) {
    implementation_->fail_ipc(operation);
  }
  return operation;
}

void runtime_process::update() noexcept {
  if (!implementation_) {
    return;
  }
  if (implementation_->is_building) {
    implementation_->build_process.update();
    implementation_->combined_output =
        "[Editor] 正在构建游戏模块。\n" + implementation_->build_process.output();
    if (implementation_->build_process.is_running()) {
      return;
    }
    implementation_->is_building = false;
    if (implementation_->build_process.exit_code() != 0) {
      implementation_->last_result = result::dependency_failed;
      implementation_->control_state = runtime_control_state::failed;
      implementation_->combined_output += "\n[Editor] 游戏模块构建失败，未启动 Runtime。\n";
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
  return implementation_ && implementation_->property_editing_negotiated;
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
