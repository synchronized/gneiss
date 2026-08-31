// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <vector>

namespace gneiss::editor {
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

struct runtime_process::implementation final {
  HANDLE process = nullptr;
  HANDLE thread = nullptr;
  HANDLE output_read = nullptr;
  std::filesystem::path session_root;
  std::filesystem::path stop_file;
  std::string output;
  std::chrono::steady_clock::time_point stop_deadline;
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
        if (output.size() > maximum_output_size) {
          output.erase(0U, output.size() - maximum_output_size);
        }
      }
    } catch (...) {
      output.append("\n[Editor] Runtime 输出缓冲区更新失败。\n");
    }
  }

  void close_process() noexcept {
    close_handle(thread);
    close_handle(process);
    close_handle(output_read);
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
    if (WaitForSingleObject(implementation_->process, 2000U) == WAIT_TIMEOUT) {
      TerminateProcess(implementation_->process, 70U);
      WaitForSingleObject(implementation_->process, 1000U);
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
        std::filesystem::temp_directory_path() / "Gneiss" /
        ("editor-runtime-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(serial));
    std::filesystem::create_directories(implementation_->session_root, error);
    if (error) {
      return result::io;
    }
    implementation_->stop_file = implementation_->session_root / "stop.signal";
    const auto log_file = implementation_->session_root / "runtime.log";

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
    std::wstring command = quote_argument(executable.wstring()) + L" --project " +
                           quote_argument(request.project_root.wstring()) + L" --stop-file " +
                           quote_argument(implementation_->stop_file.wstring()) + L" --log-file " +
                           quote_argument(log_file.wstring());
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const auto created =
        CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
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
  if (implementation_->process == nullptr) {
    return;
  }
  if (implementation_->stop_deadline != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() >= implementation_->stop_deadline) {
    implementation_->output.append("\n[Editor] Runtime 未在 2 秒内退出，已强制终止。\n");
    TerminateProcess(implementation_->process, 70U);
  }
  DWORD code = STILL_ACTIVE;
  if (GetExitCodeProcess(implementation_->process, &code) != 0 && code != STILL_ACTIVE) {
    implementation_->exit_code =
        code > static_cast<DWORD>(std::numeric_limits<int>::max()) ? -1 : static_cast<int>(code);
    implementation_->stop_deadline = {};
    implementation_->read_output();
    close_handle(implementation_->thread);
    close_handle(implementation_->process);
    close_handle(implementation_->output_read);
  }
}

bool runtime_process::is_running() const noexcept {
  return implementation_ && implementation_->process != nullptr;
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
