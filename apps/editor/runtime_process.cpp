// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include "child_process.h"

#include <chrono>
#include <fstream>
#include <new>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace gneiss::editor {

struct runtime_process::implementation final {
  child_process process;
  child_process build_process;
  console_model console;
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
  result last_result = result::success;

  void append_console_lines(std::vector<app::runtime_log_line>& lines) noexcept {
    for (auto& line : lines) {
      app::runtime_log_record event;
      if (!line.was_truncated &&
          app::parse_runtime_log_line(line.text, event) == app::runtime_log_parse_result::success) {
        (void)console.append_event(runtime_session_id, std::move(event));
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
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    implementation_->session_root = std::filesystem::temp_directory_path() / "Gneiss" /
                                    ("editor-runtime-" + std::to_string(serial));
    std::filesystem::create_directories(implementation_->session_root, error);
    if (error) {
      return result::io;
    }
    implementation_->stop_file = implementation_->session_root / "stop.signal";
    implementation_->log_file = implementation_->session_root / "runtime.log";
    child_process_start_info info;
    info.executable = executable;
    info.arguments = {"--project",   request.project_root,
                      "--stop-file", implementation_->stop_file,
                      "--log-file",  implementation_->log_file};
    const auto started = implementation_->process.start(info);
    implementation_->last_result = started;
    if (started != result::success) {
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
    implementation_->combined_output += "\n[Editor] 游戏模块构建已停止。\n";
    return operation;
  }
  if (!is_running()) {
    return result::not_ready;
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
