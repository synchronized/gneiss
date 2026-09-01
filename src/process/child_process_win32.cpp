// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "child_process.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

namespace gneiss {
namespace {

constexpr std::size_t maximum_output_size = 256U * 1024U;

[[nodiscard]] std::wstring quote_argument(std::wstring_view argument) {
  std::wstring result{L'"'};
  std::size_t backslashes = 0U;
  for (const auto character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'"') {
      result.append((backslashes * 2U) + 1U, L'\\');
      result.push_back(character);
      backslashes = 0U;
      continue;
    }
    result.append(backslashes, L'\\');
    backslashes = 0U;
    result.push_back(character);
  }
  result.append(backslashes * 2U, L'\\');
  result.push_back(L'"');
  return result;
}

void close_handle(HANDLE& handle) noexcept {
  if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle);
  }
  handle = nullptr;
}

} // namespace

struct child_process::implementation final {
  HANDLE process = nullptr;
  HANDLE thread = nullptr;
  HANDLE output_read = nullptr;
  std::string output;
  std::string pending_output;
  int exit_code = 0;
  bool has_started = false;

  void read_output() noexcept {
    if (output_read == nullptr) {
      return;
    }
    try {
      for (;;) {
        DWORD available = 0U;
        if (PeekNamedPipe(output_read, nullptr, 0U, nullptr, &available, nullptr) == 0 ||
            available == 0U) {
          break;
        }
        char buffer[4096]{};
        DWORD read = 0U;
        if (ReadFile(output_read, buffer, std::min<DWORD>(available, sizeof(buffer)), &read,
                     nullptr) == 0 ||
            read == 0U) {
          break;
        }
        output.append(buffer, read);
        pending_output.append(buffer, read);
        if (output.size() > maximum_output_size) {
          output.erase(0U, output.size() - maximum_output_size);
        }
      }
    } catch (...) {
      constexpr std::string_view warning = "\n[Gneiss] 子进程输出缓冲区更新失败。\n";
      output.append(warning);
      pending_output.append(warning);
    }
  }

  void close_process() noexcept {
    close_handle(thread);
    close_handle(process);
    close_handle(output_read);
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
    (void)WaitForSingleObject(implementation_->process, 1000U);
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
    implementation_->pending_output.clear();
    implementation_->exit_code = 0;

    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output_write = nullptr;
    if (CreatePipe(&implementation_->output_read, &output_write, &security, 0U) == 0 ||
        SetHandleInformation(implementation_->output_read, HANDLE_FLAG_INHERIT, 0U) == 0) {
      close_handle(output_write);
      implementation_->close_process();
      return result::initialization_failed;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring command = quote_argument(info.executable.wstring());
    for (const auto& argument : info.arguments) {
      command += L' ';
      command += quote_argument(argument.wstring());
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const auto working_directory = info.working_directory.wstring();
    const auto created = CreateProcessW(
        info.executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, working_directory.empty() ? nullptr : working_directory.c_str(), &startup,
        &process);
    close_handle(output_write);
    if (created == 0) {
      implementation_->close_process();
      return result::initialization_failed;
    }
    implementation_->process = process.hProcess;
    implementation_->thread = process.hThread;
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
  return TerminateProcess(implementation_->process, 70U) != 0 ? result::success : result::io;
}

void child_process::update() noexcept {
  if (!implementation_) {
    return;
  }
  implementation_->read_output();
  if (implementation_->process == nullptr) {
    return;
  }
  DWORD code = STILL_ACTIVE;
  if (GetExitCodeProcess(implementation_->process, &code) != 0 && code != STILL_ACTIVE) {
    implementation_->exit_code =
        code > static_cast<DWORD>(std::numeric_limits<int>::max()) ? -1 : static_cast<int>(code);
    implementation_->read_output();
    implementation_->close_process();
  }
}

bool child_process::is_running() const noexcept {
  return implementation_ && implementation_->process != nullptr;
}
bool child_process::has_started() const noexcept {
  return implementation_ && implementation_->has_started;
}
int child_process::exit_code() const noexcept {
  return implementation_ ? implementation_->exit_code : -1;
}
const std::string& child_process::output() const noexcept { return implementation_->output; }
void child_process::consume_output(std::string& output) noexcept {
  output.clear();
  if (implementation_) {
    output.swap(implementation_->pending_output);
  }
}
void child_process::clear_output() noexcept {
  if (implementation_) {
    implementation_->output.clear();
    implementation_->pending_output.clear();
  }
}
void child_process::append_output(std::string_view text) noexcept {
  if (!implementation_) {
    return;
  }
  try {
    implementation_->output.append(text);
    implementation_->pending_output.append(text);
    if (implementation_->output.size() > maximum_output_size) {
      implementation_->output.erase(0U, implementation_->output.size() - maximum_output_size);
    }
  } catch (...) {
  }
}

} // namespace gneiss
