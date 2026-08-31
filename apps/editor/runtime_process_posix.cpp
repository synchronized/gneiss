// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <new>
#include <string>
#include <system_error>
#include <thread>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace gneiss::editor {
namespace {

constexpr std::size_t maximum_output_size = 256U * 1024U;

void close_descriptor(int& descriptor) noexcept {
  if (descriptor >= 0) {
    (void)::close(descriptor);
  }
  descriptor = -1;
}

} // namespace

struct runtime_process::implementation final {
  pid_t process = -1;
  int output_read = -1;
  std::filesystem::path session_root;
  std::filesystem::path stop_file;
  std::string output;
  std::chrono::steady_clock::time_point stop_deadline;
  int exit_code = 0;
  bool has_started = false;

  void read_output() noexcept {
    if (output_read < 0) {
      return;
    }
    try {
      char buffer[4096]{};
      for (;;) {
        const auto count = ::read(output_read, buffer, sizeof(buffer));
        if (count > 0) {
          output.append(buffer, static_cast<std::size_t>(count));
          if (output.size() > maximum_output_size) {
            output.erase(0U, output.size() - maximum_output_size);
          }
          continue;
        }
        if (count < 0 && errno == EINTR) {
          continue;
        }
        break;
      }
    } catch (...) {
      output.append("\n[Editor] Runtime 输出缓冲区更新失败。\n");
    }
  }

  void close_process() noexcept {
    close_descriptor(output_read);
    process = -1;
    std::error_code error;
    std::filesystem::remove(stop_file, error);
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
      (void)::kill(implementation_->process, SIGKILL);
      int status = 0;
      while (::waitpid(implementation_->process, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }
  implementation_->read_output();
  implementation_->close_process();
}

result runtime_process::start(const std::filesystem::path& executable,
                              const runtime_launch_request& request) noexcept {
  if (!implementation_ || is_running() || executable.empty() || request.project_root.empty()) {
    return result::invalid_state;
  }
  try {
    std::error_code error;
    if (!std::filesystem::is_regular_file(executable, error) || error ||
        !std::filesystem::is_directory(request.project_root, error) || error) {
      return result::not_found;
    }
    implementation_->close_process();
    implementation_->output.clear();
    implementation_->exit_code = 0;
    implementation_->stop_deadline = {};
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    implementation_->session_root =
        std::filesystem::temp_directory_path() / "gneiss" /
        ("editor-runtime-" + std::to_string(::getpid()) + "-" + std::to_string(serial));
    std::filesystem::create_directories(implementation_->session_root, error);
    if (error) {
      return result::io;
    }
    implementation_->stop_file = implementation_->session_root / "stop.signal";
    const auto log_file = implementation_->session_root / "runtime.log";

    int output_pipe[2]{-1, -1};
    if (::pipe(output_pipe) != 0) {
      implementation_->close_process();
      return result::initialization_failed;
    }
    const auto flags = ::fcntl(output_pipe[0], F_GETFL, 0);
    if (flags < 0 || ::fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK) != 0) {
      close_descriptor(output_pipe[0]);
      close_descriptor(output_pipe[1]);
      implementation_->close_process();
      return result::initialization_failed;
    }

    const auto executable_text = executable.string();
    const auto project_text = request.project_root.string();
    const auto stop_text = implementation_->stop_file.string();
    const auto log_text = log_file.string();
    const auto child = ::fork();
    if (child == 0) {
      (void)::close(output_pipe[0]);
      if (::dup2(output_pipe[1], STDOUT_FILENO) < 0 || ::dup2(output_pipe[1], STDERR_FILENO) < 0) {
        ::_exit(126);
      }
      (void)::close(output_pipe[1]);
      ::execl(executable_text.c_str(), executable_text.c_str(), "--project", project_text.c_str(),
              "--stop-file", stop_text.c_str(), "--log-file", log_text.c_str(),
              static_cast<char*>(nullptr));
      ::_exit(127);
    }
    close_descriptor(output_pipe[1]);
    if (child < 0) {
      close_descriptor(output_pipe[0]);
      implementation_->close_process();
      return result::initialization_failed;
    }
    implementation_->process = child;
    implementation_->output_read = output_pipe[0];
    implementation_->has_started = true;
    return result::success;
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
  implementation_->read_output();
  if (implementation_->process < 0) {
    return;
  }
  if (implementation_->stop_deadline != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() >= implementation_->stop_deadline) {
    implementation_->output.append("\n[Editor] Runtime 未在 2 秒内退出，已强制终止。\n");
    (void)::kill(implementation_->process, SIGKILL);
  }
  int status = 0;
  const auto waited = ::waitpid(implementation_->process, &status, WNOHANG);
  if (waited != implementation_->process) {
    return;
  }
  if (WIFEXITED(status)) {
    implementation_->exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    implementation_->exit_code = 128 + WTERMSIG(status);
  } else {
    implementation_->exit_code = -1;
  }
  implementation_->stop_deadline = {};
  implementation_->read_output();
  implementation_->process = -1;
  close_descriptor(implementation_->output_read);
}

bool runtime_process::is_running() const noexcept {
  return implementation_ && implementation_->process >= 0;
}

bool runtime_process::has_started() const noexcept {
  return implementation_ && implementation_->has_started;
}

int runtime_process::exit_code() const noexcept {
  return implementation_ ? implementation_->exit_code : -1;
}

const std::string& runtime_process::output() const noexcept { return implementation_->output; }

void runtime_process::clear_output() noexcept {
  if (implementation_) {
    implementation_->output.clear();
  }
}

} // namespace gneiss::editor
