// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include "child_process.h"

#include <chrono>
#include <fstream>
#include <new>
#include <system_error>
#include <thread>

namespace gneiss::editor {

struct runtime_process::implementation final {
  child_process process;
  std::filesystem::path session_root;
  std::filesystem::path stop_file;
  std::filesystem::path log_file;
  std::chrono::steady_clock::time_point stop_deadline;
  bool forced_termination_reported = false;

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
  if (!implementation_ || is_running() || executable.empty() || request.project_root.empty()) {
    return result::invalid_state;
  }
  try {
    std::error_code error;
    if (!std::filesystem::is_directory(request.project_root, error) || error) {
      return result::not_found;
    }
    implementation_->clean_stop_file();
    implementation_->process.clear_output();
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
    if (started != result::success) {
      implementation_->discard_session();
    }
    return started;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result runtime_process::request_stop() noexcept {
  if (!implementation_ || !is_running()) {
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
  implementation_->process.update();
  if (!implementation_->process.is_running()) {
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
bool runtime_process::has_started() const noexcept {
  return implementation_ && implementation_->process.has_started();
}
int runtime_process::exit_code() const noexcept {
  return implementation_ ? implementation_->process.exit_code() : -1;
}
const std::string& runtime_process::output() const noexcept {
  return implementation_->process.output();
}
const std::filesystem::path& runtime_process::log_file() const noexcept {
  return implementation_->log_file;
}
void runtime_process::clear_output() noexcept {
  if (implementation_) {
    implementation_->process.clear_output();
  }
}

} // namespace gneiss::editor
