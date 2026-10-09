// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/diagnostics/profiling.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
struct file_handle {
  HANDLE value{INVALID_HANDLE_VALUE};
  file_handle() = default;
  file_handle(const file_handle&) = delete;
  file_handle& operator=(const file_handle&) = delete;
  ~file_handle() {
    GNEISS_PROFILE_SCOPE("probe.close_handle");
    if (value != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(value);
    }
  }
};

bool canonical(const std::filesystem::path& path, std::string_view label) {
  GNEISS_PROFILE_SCOPE("probe.canonical");
  GNEISS_PROFILE_TEXT(label);
  std::error_code error;
  const auto result = std::filesystem::canonical(path, error);
  return !error && !result.empty();
}

bool native_path(const std::filesystem::path& path, std::string_view label) {
  file_handle file;
  {
    GNEISS_PROFILE_SCOPE("probe.open_attributes");
    GNEISS_PROFILE_TEXT(label);
    file.value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  }
  if (file.value == INVALID_HANDLE_VALUE) {
    return false;
  }
  std::array<wchar_t, 32768> buffer{};
  GNEISS_PROFILE_SCOPE("probe.final_path");
  GNEISS_PROFILE_TEXT(label);
  const auto length =
      GetFinalPathNameByHandleW(file.value, buffer.data(), static_cast<DWORD>(buffer.size()),
                                VOLUME_NAME_DOS | FILE_NAME_NORMALIZED);
  return length != 0U && length < buffer.size();
}
bool wait_for_profiler() {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (!TracyIsConnected && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return TracyIsConnected;
}

std::vector<std::filesystem::path> collect_paths(const std::filesystem::path& root) {
  GNEISS_PROFILE_SCOPE("probe.enumerate");
  std::vector<std::filesystem::path> paths;
  // 默认不跟随目录符号链接，不读取文件 payload，也不修改资产。
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) {
      paths.push_back(entry.path());
    }
  }
  std::ranges::sort(paths);
  return paths;
}

unsigned probe_file(const std::filesystem::path& path, const std::filesystem::path& root,
                    bool native_first) {
  const auto relative = path.lexically_relative(root).generic_u8string();
  const std::string label(relative.begin(), relative.end());
  GNEISS_PROFILE_SCOPE("probe.file");
  GNEISS_PROFILE_TEXT(label);
  GNEISS_PROFILE_TEXT(native_first ? "native_first" : "canonical_first");
  bool first{};
  bool second{};
  if (native_first) {
    first = native_path(path, label);
    second = canonical(path, label);
  } else {
    first = canonical(path, label);
    second = native_path(path, label);
  }
  return (first ? 0U : 1U) + (second ? 0U : 1U);
}
} // namespace

int wmain(int argc, wchar_t** argv) try {
  if (argc < 2 || argc > 3 || (argc == 3 && std::wstring_view(argv[2]) != L"--wait-for-profiler")) {
    std::fprintf(stderr, "用法：gneiss_filesystem_trace_probe <目录> [--wait-for-profiler]\n");
    return 2;
  }
  if (argc == 3 && !wait_for_profiler()) {
    return 3;
  }
  GNEISS_PROFILE_THREAD("Filesystem probe");
  const auto root = std::filesystem::absolute(std::filesystem::path(argv[1]));
  const auto paths = collect_paths(root);
  if (paths.empty()) {
    return 4;
  }
  unsigned failures{};
  // 相邻文件交替先后顺序，第二轮反转，避免所有首次访问都由 canonical 承担。
  for (unsigned pass = 0U; pass < 2U; ++pass) {
    bool native_first = pass != 0U;
    for (const auto& path : paths) {
      failures += probe_file(path, root, native_first);
      native_first = !native_first;
      GNEISS_PROFILE_FRAME();
    }
  }
  std::printf("files=%zu passes=2 failures=%u\n", paths.size(), failures);
  if (argc == 3) {
    // 仅诊断夹具给异步发送留出收尾时间；仍需检查捕获事件数量，不保证确认送达。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return failures == 0U ? 0 : 5;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
