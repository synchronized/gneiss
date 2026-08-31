// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "child_process.h"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <new>
#include <string>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace gneiss {
namespace {

constexpr std::size_t maximum_output_size = 256U * 1024U;

void close_descriptor(int& descriptor) noexcept {
  if (descriptor >= 0) {
    (void)::close(descriptor);
  }
  descriptor = -1;
}

} // namespace

struct child_process::implementation final {
  pid_t process = -1;
  int output_read = -1;
  std::string output;
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
      output.append("\n[Gneiss] 子进程输出缓冲区更新失败。\n");
    }
  }

  void close_process() noexcept {
    close_descriptor(output_read);
    process = -1;
  }
};

child_process::child_process() : implementation_(std::make_unique<implementation>()) {}

child_process::~child_process() {
  if (!implementation_) {
    return;
  }
  update();
  if (is_running()) {
    (void)terminate();
    int status = 0;
    while (::waitpid(implementation_->process, &status, 0) < 0 && errno == EINTR) {
    }
  }
  implementation_->read_output();
  implementation_->close_process();
}

result child_process::start(const child_process_start_info& info) noexcept {
  if (!implementation_ || is_running() || info.executable.empty()) {
    return result::invalid_state;
  }
  try {
    std::error_code error;
    if (!std::filesystem::is_regular_file(info.executable, error) || error ||
        (!info.working_directory.empty() &&
         (!std::filesystem::is_directory(info.working_directory, error) || error))) {
      return result::not_found;
    }
    implementation_->close_process();
    implementation_->output.clear();
    implementation_->exit_code = 0;

    int output_pipe[2]{-1, -1};
    if (::pipe(output_pipe) != 0) {
      return result::initialization_failed;
    }
    const auto flags = ::fcntl(output_pipe[0], F_GETFL, 0);
    if (flags < 0 || ::fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK) != 0) {
      close_descriptor(output_pipe[0]);
      close_descriptor(output_pipe[1]);
      return result::initialization_failed;
    }

    const auto executable = info.executable.string();
    const auto working_directory = info.working_directory.string();
    std::vector<std::string> arguments;
    arguments.reserve(info.arguments.size() + 1U);
    arguments.push_back(executable);
    for (const auto& argument : info.arguments) {
      arguments.push_back(argument.string());
    }
    std::vector<char*> argument_pointers;
    argument_pointers.reserve(arguments.size() + 1U);
    for (auto& argument : arguments) {
      argument_pointers.push_back(argument.data());
    }
    argument_pointers.push_back(nullptr);

    const auto child = ::fork();
    if (child == 0) {
      (void)::close(output_pipe[0]);
      if (::dup2(output_pipe[1], STDOUT_FILENO) < 0 || ::dup2(output_pipe[1], STDERR_FILENO) < 0) {
        ::_exit(126);
      }
      (void)::close(output_pipe[1]);
      if (!working_directory.empty() && ::chdir(working_directory.c_str()) != 0) {
        ::_exit(126);
      }
      ::execv(executable.c_str(), argument_pointers.data());
      ::_exit(127);
    }
    close_descriptor(output_pipe[1]);
    if (child < 0) {
      close_descriptor(output_pipe[0]);
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

result child_process::terminate() noexcept {
  if (!implementation_ || !is_running()) {
    return result::not_ready;
  }
  return ::kill(implementation_->process, SIGKILL) == 0 ? result::success : result::io;
}

void child_process::update() noexcept {
  if (!implementation_) {
    return;
  }
  implementation_->read_output();
  if (implementation_->process < 0) {
    return;
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
  implementation_->read_output();
  implementation_->close_process();
}

bool child_process::is_running() const noexcept {
  return implementation_ && implementation_->process >= 0;
}
bool child_process::has_started() const noexcept {
  return implementation_ && implementation_->has_started;
}
int child_process::exit_code() const noexcept {
  return implementation_ ? implementation_->exit_code : -1;
}
const std::string& child_process::output() const noexcept { return implementation_->output; }
void child_process::clear_output() noexcept {
  if (implementation_) {
    implementation_->output.clear();
  }
}
void child_process::append_output(std::string_view text) noexcept {
  if (!implementation_) {
    return;
  }
  try {
    implementation_->output.append(text);
    if (implementation_->output.size() > maximum_output_size) {
      implementation_->output.erase(0U, implementation_->output.size() - maximum_output_size);
    }
  } catch (...) {
  }
}

} // namespace gneiss
